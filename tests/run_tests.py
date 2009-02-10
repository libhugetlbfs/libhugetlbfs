#! /usr/bin/env python

import subprocess
import os
import sys
import getopt

# The superset of wordsizes that should be tested (default 32, 64)
wordsizes = set()

# The super set of page sizes that should be tested.  Defaults to all supported
# huge page sizes with an active mount and at least one huge page allocated
pagesizes = set()

# Each page size may have a subset of valid wordsizes
# This is a dictionary (indexed by page size) of sets
wordsizes_by_pagesize = {}

# The linkhuge tests may only be valid on a subset of word sizes
# This set contains the wordsizes valid for linkhuge tests
linkhuge_wordsizes = set()

# A list of all discovered mountpoints that may be used by libhugetlbfs for
# this run of tests.  This is used for cleaning up left-over share files.
mounts = []

# Results matrix:  This 3-D dictionary is indexed as follows:
#   [type]     - Test results fall into one of the 'result_types' categories
#   [pagesize] - a page size from the set 'pagesizes'
#   [bits]     - a word size from the set 'wordsizes'
#   The indexed value is the number of tests matching the above traits
R = {}
result_types = ("total", "pass", "config", "fail", "xfail", "xpass",
                "signal", "strange", "skip")

def bash(cmd, extra_env={}):
    """
    Run 'cmd' in the shell and return the exit code and output.
    """
    local_env = os.environ.copy()
    for key,value in extra_env.items():
        local_env[key] = value
    p = subprocess.Popen(cmd, shell=True, env=local_env, \
                         stdout=subprocess.PIPE)
    try:
        rc = p.wait()
    except KeyboardInterrupt:
        # Abort and mark this a strange test result
        return (127, "")
    out = p.stdout.read().strip()
    return (rc, out)

def setup_env(override, defaults):
    """
    Set up the environment for running commands in the shell.
    """
    # All items in override are unconditionally set or unset
    for (var, val) in override.items():
        if val == None:
            if var in os.environ:
                del os.environ[var]
        else:
            os.environ[var] = val
    # If not already set, these variables are given default values
    for (var, val) in defaults.items():
        if var not in os.environ or os.environ[var] == "":
            os.environ[var] = val

def init_results():
    """
    Define the structure of the results matrix and initialize all results to 0.
    """
    global R

    for t in result_types:
        R[t] = {}
        for p in pagesizes:
            R[t][p] = {}
            for bits in (32, 64):
                R[t][p][bits] = 0

def pretty_page_size(size):
    """
    Convert a page size to a formatted string

    Given a page size in bytes, return a string that expresses the size in
    a sensible unit (K, M, or G).
    """
    factor = 0
    while size > 1024:
        factor += 1
        size /= 1024

    if   factor == 0: return "%iB" % size
    elif factor == 1: return "%iK" % size
    elif factor == 2: return "%iM" % size
    elif factor == 3: return "%iG" % size

def print_per_size(title, values):
    """
    Print one line of test results

    Print the results of a given result type on one line.  The results for all
    page sizes and word sizes are written in a table format.
    """
    print "*%20s: " % title,
    for sz in pagesizes:
        print "%4s   %4s   " % (values[sz][32], values[sz][64]),
    print

def results_summary():
    """
    Display a summary of the test results
    """
    print "********** TEST SUMMARY"
    print "*%21s" % "",
    for p in pagesizes: print "%-13s " % pretty_page_size(p),
    print
    print "*%21s" % "",
    for p in pagesizes: print "32-bit 64-bit ",
    print

    print_per_size("Total testcases", R["total"])
    print_per_size("Skipped", R["skip"])
    print_per_size("PASS", R["pass"])
    print_per_size("FAIL", R["fail"])
    print_per_size("Killed by signal", R["signal"])
    print_per_size("Bad configuration", R["config"])
    print_per_size("Expected FAIL", R["xfail"])
    print_per_size("Unexpected PASS", R["xpass"])
    print_per_size("Strange test result", R["strange"])
    print "**********"

def free_hpages():
    """
    Return the number of free huge pages.

    Parse /proc/meminfo to obtain the number of free huge pages for
    the default page size.
    XXX: This function is not multi-size aware yet.
    """
    (rc, out) = bash("grep 'HugePages_Free:' /proc/meminfo | cut -f2 -d:")
    return (rc, int(out))

def total_hpages():
    """
    Return the total number of huge pages in the pool.

    Parse /proc/meminfo to obtain the number of huge pages for the default
    page size.
    XXX: This function is not multi-size aware yet.
    """
    (rc, out) = bash("grep 'HugePages_Total:' /proc/meminfo | cut -f2 -d:")
    return (rc, int(out))

def hpage_size():
    """
    Return the size of the default huge page size in bytes.

    Parse /proc/meminfo to obtain the default huge page size.  This number is
    reported in Kb so multiply it by 1024 to convert it to bytes.
    XXX: This function is not multi-size aware yet.
    """
    (rc, out) = bash("grep 'Hugepagesize:' /proc/meminfo | awk '{print $2}'")
    if out == "": out = 0
    out = int(out) * 1024
    return (rc, out)

def clear_hpages():
    """
    Remove stale hugetlbfs files after sharing tests.

    Traverse the mount points that are in use during testing to find left-over
    files that were created by the elflink sharing tests.  These are not
    cleaned up automatically and must be removed to free up the huge pages.
    """
    for mount in mounts:
        dir = mount + "/elflink-uid-" + `os.getuid()`
        for root, dirs, files in os.walk(dir, topdown=False):
            for name in files:
                os.remove(os.path.join(root, name))
            for name in dirs:
                os.rmdir(os.path.join(root, name))
        try:
            os.rmdir(dir)
        except OSError:
            pass

def cmd_env(bits, pagesize=""):
    """
    Construct test-specific environment settings.

    Set PATH and LD_LIBRARY_PATH so that the executable and libraries for a
    libhugetlbfs test or utility that is run from within the source tree can
    be found.  Additionally, tell libhugetlbfs to use the requested page size.
    """
    str = "PATH=$PATH:./obj%s:../obj%s " \
          "LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../obj%s:obj%s " % \
          (`bits`, `bits`, `bits`, `bits`)
    if len(`pagesize`) > 0: str += "HUGETLB_DEFAULT_PAGE_SIZE=%s " % pagesize
    return str

def get_pagesizes():
    """
    Get a list of configured huge page sizes.

    Use libhugetlbfs' hugeadm utility to get a list of page sizes that have
    active mount points and at least one huge page allocated to the pool.
    """
    sizes = set()
    (rc, out) = bash(cmd_env('') + "hugeadm --page-sizes")
    if rc != 0: return sizes

    for size in out.split("\n"): sizes.add(int(size))
    return sizes

def check_hugetlbfs_path():
    """
    Check each combination of page size and word size for validity.

    Some word sizes may not be valid for all page sizes.  For example, a 16G
    page is too large to be used in a 32 bit process.  Use a helper program to
    weed out invalid combinations and print informational messages as required.
    """
    global wordsizes, pagesizes, mounts, wordsizes_by_pagesize

    for p in pagesizes:
        okbits = []
        for b in wordsizes:
            cmd = cmd_env(b, p) + "get_hugetlbfs_path"
            (rc, out) = bash(cmd)
            if rc == 0:
                okbits.append(b)
                mounts.append(out)
        if len(okbits) == 0:
            print "run_tests.py: No mountpoints available for page size %s" % \
                  pretty_page_size(p)
            wordsizes_by_pagesize[p] = set()
            continue
        for b in wordsizes - set(okbits):
            print "run_tests.py: The %i bit word size is not compatible with " \
                  "%s pages" % (b, pretty_page_size(p))
        wordsizes_by_pagesize[p] = set(okbits)

def check_linkhuge_tests():
    """
    Check if the linkhuge tests are safe to run on this system.

    Newer versions of binutils (>= 2.18) are known to be incompatible with the
    linkhuge tests and running them may cause unreliable behavior.  Determine
    which word sizes can be tested with linkhuge.  The others will be skipped.
    NOTE: The linhuge_rw tests are always safe to run and will not be skipped.
    """
    okbits = []

    for bits in wordsizes:
        cmd = "gcc -m%i -Wl,--verbose 2> /dev/null | grep -q SPECIAL" % bits
        (rc, out) = bash(cmd)
        if rc != 0: okbits.append(bits)
    return set(okbits)

def run_test(pagesize, bits, cmd, pre, desc):
    """
    Execute a test, print the output and log the result

    Run a test using the specified page size and word size.  The parameter
    'pre' may contain additional environment settings and will be prepended to
    cmd.  A line showing info about the test is printed and after completion
    the test output is printed.  The result is recorded in the result matrix.
    """
    global R

    objdir = "obj%i" % bits
    if not os.path.isdir(objdir):
        return

    cmd_str = "%s %s %s" % (cmd_env(bits, pagesize), pre, cmd)
    if desc != "": print desc,
    if pre != "": print pre,
    print "%s (%s: %i):\t" % (cmd, pretty_page_size(pagesize), bits),
    sys.stdout.flush()

    (rc, out) = bash(cmd_str)
    print out

    R["total"][pagesize][bits] += 1
    if rc == 0:    R["pass"][pagesize][bits] += 1
    elif rc == 1:  R["config"][pagesize][bits] += 1
    elif rc == 2:  R["fail"][pagesize][bits] += 1
    elif rc == 3:  R["xfail"][pagesize][bits] += 1
    elif rc == 4:  R["xpass"][pagesize][bits] += 1
    elif rc > 127: R["signal"][pagesize][bits] += 1
    else:          R["strange"][pagesize][bits] += 1

def skip_test(pagesize, bits, cmd, pre, desc):
    """
    Skip a test, print test information, and log that it was skipped.
    """
    global tot_tests, tot_skip
    R["total"][pagesize][bits] += 1
    R["skip"][pagesize][bits] += 1
    if desc != "": print desc,
    if pre != "": print pre,
    print "%s (%s: %i):\tSKIPPED" % (cmd, pretty_page_size(pagesize), bits)

def do_test(cmd, pre="", bits=None, desc=""):
    """
    Run a test case, testing each page size and each indicated word size.
    """
    if bits == None: bits = wordsizes
    for p in pagesizes:
        for b in (set(bits) & wordsizes_by_pagesize[p]):
            run_test(p, b, cmd, pre, desc)

def do_elflink_test(cmd, pre="", desc=""):
    """
    Run an elflink test case, skipping known-bad configurations.
    """
    for p in pagesizes:
        for b in wordsizes_by_pagesize[p]:
            if b in linkhuge_wordsizes: run_test(p, b, cmd, pre, desc)
            else: skip_test(p, b, cmd, pre, desc)

def combine(a, b):
    """
    Concatenate strings a and b with a space between only if needed.
    """
    if len(b) != 0:
        return a + " " + b
    else:
        return a

def elflink_test(cmd, pre=""):
    """
    Run an elflink test case with different configuration combinations.

    Test various combinations of: preloading libhugetlbfs, B vs. BDT link
    modes, minimal copying on or off, and disabling segment remapping.
    """
    do_test(cmd, pre)
    # Test we don't blow up if not linked for hugepage
    do_test(cmd, combine("LD_PRELOAD=libhugetlbfs.so", pre))
    do_elflink_test("xB." + cmd)
    do_elflink_test("xBDT." + cmd)
    # Test we don't blow up if HUGETLB_MINIMAL_COPY is diabled
    do_elflink_test("xB." + cmd, combine("HUGETLB_MINIMAL_COPY=no", pre))
    do_elflink_test("xBDT." + cmd, combine("HUGETLB_MINIMAL_COPY=no", pre))
    # Test that HUGETLB_ELFMAP=no inhibits remapping as intended
    do_elflink_test("xB." + cmd, combine("HUGETLB_ELFMAP=no", pre))
    do_elflink_test("xBDT." + cmd, combine("HUGETLB_ELFMAP=no", pre))

def elflink_rw_test(cmd, pre=""):
    """
    Run the elflink_rw test with different configuration combinations.

    Test various combinations of: remapping modes and minimal copy on or off.
    """
    # Basic tests: None, Read-only, Write-only, Read-Write, exlicit disable
    do_test(cmd, pre)
    do_test(cmd, combine("HUGETLB_ELFMAP=R", pre))
    do_test(cmd, combine("HUGETLB_ELFMAP=W", pre))
    do_test(cmd, combine("HUGETLB_ELFMAP=RW", pre))
    do_test(cmd, combine("HUGETLB_ELFMAP=no", pre))

    # Test we don't blow up if HUGETLB_MINIMAL_COPY is disabled
    do_test(cmd, combine("HUGETLB_MINIMAL_COPY=no HUGETLB_ELFMAP=R", pre))
    do_test(cmd, combine("HUGETLB_MINIMAL_COPY=no HUGETLB_ELFMAP=W", pre))
    do_test(cmd, combine("HUGETLB_MINIMAL_COPY=no HUGETLB_ELFMAP=RW", pre))

def elfshare_test(cmd, pre=""):
    """
    Test segment sharing with multiple configuration variations.
    """
    # Run each elfshare test invocation independently - clean up the
    # sharefiles before and after in the first set of runs, but leave
    # them there in the second:
    clear_hpages()
    do_elflink_test("xB." + cmd, combine("HUGETLB_SHARE=1", pre))
    clear_hpages()
    do_elflink_test("xBDT." + cmd, combine("HUGETLB_SHARE=1", pre))
    clear_hpages()
    do_elflink_test("xB." + cmd, combine("HUGETLB_SHARE=1", pre))
    do_elflink_test("xBDT." + cmd, combine("HUGETLB_SHARE=1", pre))
    clear_hpages()

def elflink_and_share_test(cmd, pre=""):
    """
    Run the ordinary linkhuge tests with sharing enabled
    """
    # Run each elflink test pair independently - clean up the sharefiles
    # before and after each pair
    clear_hpages()
    for link_str in ("xB.", "xBDT."):
        for i in range(2):
            do_elflink_test(link_str + cmd, combine("HUGETLB_SHARE=1", pre))
        clear_hpages()

def elflink_rw_and_share_test(cmd, pre=""):
    """
    Run the ordinary linkhuge_rw tests with sharing enabled
    """
    clear_hpages()
    for mode in ("R", "W", "RW"):
        for i in range(2):
            do_test(cmd, combine("HUGETLB_ELFMAP=" + mode + " " + \
                                 "HUGETLB_SHARE=1", pre))
        clear_hpages()

def setup_shm_sysctl(limit):
    """
    Adjust the kernel shared memory limits to accomodate a desired size.

    The original values are returned in a dictionary that can be passed to
    restore_shm_sysctl() to restore the system state.
    """
    if os.getuid() != 0: return {}
    sysctls = {}
    files = [ "/proc/sys/kernel/shmmax", "/proc/sys/kernel/shmall"]
    for f in files:
        fh = open(f, "r")
        sysctls[f] = fh.read()
        fh.close()
        fh = open(f, "w")
        fh.write(`limit`)
        fh.close()
    print "set shmmax limit to %s" % limit
    return sysctls

def restore_shm_sysctl(sysctls):
    """
    Restore the sysctls named in 'sysctls' to the given values.
    """
    if os.getuid() != 0: return
    for (file, val) in sysctls.items():
        fh = open(file, "w")
        fh.write(val)
        fh.close()

def functional_tests():
    """
    Run the set of functional tests.
    """
    global linkhuge_wordsizes

    # Kernel background tests not requiring hugepage support
    do_test("zero_filesize_segment")

    # Library background tests not requiring hugepage support
    do_test("test_root")
    do_test("meminfo_nohuge")

    # Library tests requiring kernel hugepage support
    do_test("gethugepagesize")
    do_test("gethugepagesizes")
    do_test("empty_mounts", "HUGETLB_VERBOSE=1")
    do_test("large_mounts", "HUGETLB_VERBOSE=1")

    # Tests requiring an active and usable hugepage mount
    do_test("find_path")
    do_test("unlinked_fd")
    do_test("readback")
    do_test("truncate")
    do_test("shared")
    do_test("mprotect")
    do_test("mlock")
    do_test("misalign")

    # Specific kernel bug tests
    do_test("ptrace-write-hugepage")
    do_test("icache-hygiene")
    do_test("slbpacaflush")
    do_test("straddle_4GB", bits=(64,))
    do_test("huge_at_4GB_normal_below", bits=(64,))
    do_test("huge_below_4GB_normal_above", bits=(64,))
    do_test("map_high_truncate_2")
    do_test("misaligned_offset")
    do_test("truncate_above_4GB")
    do_test("brk_near_huge")
    do_test("task-size-overrun")
    do_test("stack_grow_into_huge")

    # Tests requiring an active mount and hugepage COW
    do_test("private")
    do_test("fork-cow")
    do_test("direct")
    do_test("malloc")
    do_test("malloc", "LD_PRELOAD=libhugetlbfs.so HUGETLB_MORECORE=yes")
    do_test("malloc_manysmall")
    do_test("malloc_manysmall", \
            "LD_PRELOAD=libhugetlbfs.so HUGETLB_MORECORE=yes")
    do_test("heapshrink")
    do_test("heapshrink", "LD_PRELOAD=libheapshrink.so")
    do_test("heapshrink", "LD_PRELOAD=libhugetlbfs.so HUGETLB_MORECORE=yes")
    do_test("heapshrink", "LD_PRELOAD=\"libhugetlbfs.so libheapshrink.so\" " + \
                          "HUGETLB_MORECORE=yes")
    do_test("heapshrink", "LD_PRELOAD=libheapshrink.so HUGETLB_MORECORE=yes " +\
                          "HUGETLB_MORECORE_SHRINK=yes")
    do_test("heapshrink", "LD_PRELOAD=\"libhugetlbfs.so libheapshrink.so\" " + \
                          "HUGETLB_MORECORE=yes HUGETLB_MORECORE_SHRINK=yes")
    do_test("heap-overflow", "HUGETLB_VERBOSE=1 HUGETLB_MORECORE=yes")

    # Run the remapping tests' up-front checks
    linkhuge_wordsizes = check_linkhuge_tests()
    # Original elflink tests
    elflink_test("linkhuge_nofd", "HUGETLB_VERBOSE=0")
    elflink_test("linkhuge")

    # Original elflink sharing tests
    elfshare_test("linkshare")
    elflink_and_share_test("linkhuge")

    # elflink_rw tests
    elflink_rw_test("linkhuge_rw")
    # elflink_rw sharing tests
    elflink_rw_and_share_test("linkhuge_rw")

    # Accounting bug tests
    # reset free hpages because sharing will have held some
    # alternatively, use
    do_test("chunk-overcommit")
    do_test("alloc-instantiate-race shared")
    do_test("alloc-instantiate-race private")
    do_test("truncate_reserve_wraparound")
    do_test("truncate_sigbus_versus_oom")

    # Test direct allocation API
    do_test("get_huge_pages")

    # Test overriding of shmget()
    do_test("shmoverride_linked")
    do_test("shmoverride_unlinked", "LD_PRELOAD=libhugetlbfs.so")

    # Test hugetlbfs filesystem quota accounting
    do_test("quota.sh")

    # Test accounting of HugePages_{Total|Free|Resv|Surp}
    #  Alters the size of the hugepage pool so should probably be run last
    do_test("counters")

def stress_tests():
    """
    Run the set of stress tests.
    """
    iterations = 10	# Number of iterations for looping tests

    # Don't update NRPAGES every time like above because we want to catch the
    # failures that happen when the kernel doesn't release all of the huge pages
    # after a stress test terminates
    (rc, nr_pages) = free_hpages()

    do_test("mmap-gettest %i %i" % (iterations, nr_pages))

    # mmap-cow needs a hugepages for each thread plus one extra
    do_test("mmap-cow %i %i" % (nr_pages - 1, nr_pages))

    (rc, tot_pages) = total_hpages()
    (rc, size) = hpage_size()
    sysctls = setup_shm_sysctl(tot_pages * size)
    threads = 10	# Number of threads for shm-fork
    # Run shm-fork once using half available hugepages, then once using all
    # This is to catch off-by-ones or races in the kernel allocated that
    # can make allocating all hugepages a problem
    if nr_pages > 1:
        do_test("shm-fork.sh %i %i" % (threads, nr_pages / 2))
    do_test("shm-fork.sh %i %i" % (threads, nr_pages))

    do_test("shm-getraw.sh %i %s" % (nr_pages, "/dev/full"))
    restore_shm_sysctl(sysctls)



def main():
    global wordsizes, pagesizes
    testsets = set()
    env_override = {"QUIET_TEST": "1", "HUGETLBFS_MOUNTS": "",
                    "HUGETLB_ELFMAP": None, "HUGETLB_MORECORE": None}
    env_defaults = {"HUGETLB_VERBOSE": "0"}

    try:
        opts, args = getopt.getopt(sys.argv[1:], "vVdt:b:p:")
    except getopt.GetoptError, err:
        print str(err)
        sys.exit(1)
    for opt, arg in opts:
       if opt == "-v":
           env_override["QUIET_TEST"] = None
           env_defaults["HUGETLB_VERBOSE"] = "2"
       elif opt == "-V":
           env_defaults["HUGETLB_VERBOSE"] = "99"
       elif opt == "-t":
           for t in arg.split(): testsets.add(t)
       elif opt == "-b":
           for b in arg.split(): wordsizes.add(int(b))
       elif opt == "-p":
           for p in arg.split(): pagesizes.add(int(p))
       else:
           assert False, "unhandled option"
    if len(testsets) == 0: testsets = set(["func", "stress"])
    if len(wordsizes) == 0: wordsizes = set([32, 64])
    if len(pagesizes) == 0: pagesizes = get_pagesizes()

    setup_env(env_override, env_defaults)
    init_results()
    check_hugetlbfs_path()

    if "func" in testsets: functional_tests()
    if "stress" in testsets: stress_tests()

    results_summary()

if __name__ == "__main__":
    main()
