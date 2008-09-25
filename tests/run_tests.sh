#! /bin/bash

export QUIET_TEST=1
unset HUGETLB_ELF
unset HUGETLB_MORECORE
HUGETLBFS_MOUNTS=""

if [ -z "$HUGETLB_VERBOSE" ]; then
	HUGETLB_VERBOSE=0
fi
export HUGETLB_VERBOSE

ENV=/usr/bin/env

for BITS in 32 64; do
    tot_tests[$BITS]=0
    tot_pass[$BITS]=0
    tot_fail[$BITS]=0
    tot_config[$BITS]=0
    tot_signal[$BITS]=0
    tot_strange[$BITS]=0
    tot_skip[$BITS]=0
done

function free_hpages() {
	H=$(grep 'HugePages_Free:' /proc/meminfo | cut -f2 -d:)
	[ -z "$H" ] && H=0
	echo "$H"
}

# Check for valid hugetlbfs mountpoints
# On error, adjust tests to be run or exit immediately.  We must check for
# mounts using both the 32 bit and 64 bit helpers because it is possible that
# a mount point will only be usable with a certain word size.  For example, a
# mount with a 16GB configured page size is usable by 64 bit programs only.
function check_hugetlbfs_path() {
    newbits=""
    skipbits=""

    for b in $WORDSIZES; do
        MP=$(PATH="obj$b:$PATH" LD_LIBRARY_PATH="$LD_LIBRARY_PATH:../obj$b" \
            get_hugetlbfs_path)
        if [ $? -ne 0 ]; then
            skipbits="$skipbits $b"
        else
            HUGETLBFS_MOUNTS="$HUGETLBFS_MOUNTS $MP"
            newbits="$newbits $b"
        fi
    done

    if [ -z "$newbits" ]; then
        echo "run_tests.sh: unable to find hugetlbfs mountpoint"
        exit 1
    fi
    for b in $skipbits; do
        echo -n "run_tests.sh: No suitable mountpoint exists for $b bit "
        echo "programs.  Disabling the $b word size."
    done
    WORDSIZES="$newbits"
}

function clear_hpages() {
    # It is not straightforward to know which mountpoint was used so clean
    # up share files in all possible mount points
    for dir in $HUGETLBFS_MOUNTS; do
        rm -rf "$dir"/elflink-uid-`id -u`
    done
}

TOTAL_HPAGES=$(grep 'HugePages_Total:' /proc/meminfo | cut -f2 -d:)
[ -z "$TOTAL_HPAGES" ] && TOTAL_HPAGES=0
HPAGE_SIZE=$(grep 'Hugepagesize:' /proc/meminfo | awk '{print $2}')
[ -z "$HPAGE_SIZE" ] && HPAGE_SIZE=0
HPAGE_SIZE=$(( $HPAGE_SIZE * 1024 ))

# Up-front checks for the remapping test cases:
function check_linkhuge_tests() {
    # In some circumstances, our linker scripts are known to be broken and
    # they will produce binaries with undefined runtime behavior.  In those
    # cases don't bother running the xNNN.linkhuge tests.  This checks if the
    # system linker scripts use the SPECIAL keyword (for placing the got and
    # plt).  Our linker scripts do not use SPECIAL and are thus broken when the
    # system scripts use it.
    ld --verbose | grep -q SPECIAL
    if [ $? -eq 0 ]; then
        LINKHUGE_SKIP=1
    fi
}

run_test_bits () {
    BITS=$1
    shift

    if [ -d obj$BITS ]; then
	tot_tests[$BITS]=$[tot_tests[$BITS] + 1]
	echo -n "$@ ($BITS):	"
	if PATH="obj$BITS:$PATH" LD_LIBRARY_PATH="$LD_LIBRARY_PATH:../obj$BITS:obj$BITS" $ENV "$@"; then
	    tot_pass[$BITS]=$[tot_pass[$BITS] + 1]
	else
	    rc="$?"
	    if [ "$rc" == "1" ]; then
		tot_config[$BITS]=$[tot_config[$BITS] + 1]
            elif [ "$rc" == "2" ]; then
		tot_fail[$BITS]=$[tot_fail[$BITS] + 1]
	    elif [ "$rc" -gt 127 ]; then
		tot_signal[$BITS]=$[tot_signal[$BITS] + 1]
            else
		tot_strange[$BITS]=$[tot_strange[$BITS] + 1]
            fi
	fi
    fi
}

run_test () {
    for bits in $WORDSIZES; do
	run_test_bits $bits "$@"
    done
}

# To manually disable a test (e.g., one that panics an older kernel),
# replace "run_test <options>" with "skip_test <options>".
skip_test () {
    echo "$@:	SKIPPED"
    for bits in $WORDSIZES; do
	tot_tests[$bits]=$[tot_tests[$bits] + 1]
	tot_skip[$bits]=$[tot_skip[$bits] + 1]
    done
}

maybe_run_linkhuge_test () {
    if [ "$LINKHUGE_SKIP" != "1" ]; then
        run_test "$@"
    else
        skip_test "$@"
    fi
}

preload_test () {
    run_test LD_PRELOAD=libhugetlbfs.so "$@"
}

elflink_test () {
    args=("$@")
    N="$[$#-1]"
    baseprog="${args[$N]}"
    unset args[$N]
    set -- "${args[@]}"
    run_test "$@" "$baseprog"
    # Test we don't blow up if not linked for hugepage
    preload_test "$@" "$baseprog"
    maybe_run_linkhuge_test "$@" "xB.$baseprog"
    maybe_run_linkhuge_test "$@" "xBDT.$baseprog"
    # Test we don't blow up if HUGETLB_MINIMAL_COPY is diabled
    maybe_run_linkhuge_test HUGETLB_MINIMAL_COPY=no "$@" "xB.$baseprog"
    maybe_run_linkhuge_test HUGETLB_MINIMAL_COPY=no "$@" "xBDT.$baseprog"
    # Test that HUGETLB_ELFMAP=no inhibits remapping as intended
    maybe_run_linkhuge_test HUGETLB_ELFMAP=no "$@" "xB.$baseprog"
    maybe_run_linkhuge_test HUGETLB_ELFMAP=no "$@" "xBDT.$baseprog"
}

elflink_rw_test() {
    # Basic tests: None, Read-only, Write-only, Read-Write, exlicit disable
    run_test linkhuge_rw
    run_test HUGETLB_ELFMAP=R linkhuge_rw
    run_test HUGETLB_ELFMAP=W linkhuge_rw
    run_test HUGETLB_ELFMAP=RW linkhuge_rw
    run_test HUGETLB_ELFMAP=no linkhuge_rw

    # Test we don't blow up if HUGETLB_MINIMAL_COPY is disabled
    run_test HUGETLB_MINIMAL_COPY=no HUGETLB_ELFMAP=R linkhuge_rw
    run_test HUGETLB_MINIMAL_COPY=no HUGETLB_ELFMAP=W linkhuge_rw
    run_test HUGETLB_MINIMAL_COPY=no HUGETLB_ELFMAP=RW linkhuge_rw
}

elfshare_test () {
    args=("$@")
    N="$[$#-1]"
    baseprog="${args[$N]}"
    unset args[$N]
    set -- "${args[@]}"
    # Run each elfshare test invocation independently - clean up the
    # sharefiles before and after in the first set of runs, but leave
    # them there in the second:
    clear_hpages
    maybe_run_linkhuge_test HUGETLB_SHARE=1 "$@" "xB.$baseprog"
    clear_hpages
    maybe_run_linkhuge_test HUGETLB_SHARE=1 "$@" "xBDT.$baseprog"
    clear_hpages
    maybe_run_linkhuge_test HUGETLB_SHARE=1 "$@" "xB.$baseprog"
    maybe_run_linkhuge_test HUGETLB_SHARE=1 "$@" "xBDT.$baseprog"
    clear_hpages
}

elflink_and_share_test () {
    args=("$@")
    N="$[$#-1]"
    baseprog="${args[$N]}"
    unset args[$N]
    set -- "${args[@]}"
    # Run each elflink test pair independently - clean up the sharefiles
    # before and after each pair
    clear_hpages
    maybe_run_linkhuge_test HUGETLB_SHARE=1 "$@" "xB.$baseprog"
    maybe_run_linkhuge_test HUGETLB_SHARE=1 "$@" "xB.$baseprog"
    clear_hpages
    maybe_run_linkhuge_test HUGETLB_SHARE=1 "$@" "xBDT.$baseprog"
    maybe_run_linkhuge_test HUGETLB_SHARE=1 "$@" "xBDT.$baseprog"
    clear_hpages
}

elflink_rw_and_share_test () {
    clear_hpages
    run_test HUGETLB_ELFMAP=R HUGETLB_SHARE=1 linkhuge_rw
    run_test HUGETLB_ELFMAP=R HUGETLB_SHARE=1 linkhuge_rw
    clear_hpages
    run_test HUGETLB_ELFMAP=W HUGETLB_SHARE=1 linkhuge_rw
    run_test HUGETLB_ELFMAP=W HUGETLB_SHARE=1 linkhuge_rw
    clear_hpages
    run_test HUGETLB_ELFMAP=RW HUGETLB_SHARE=1 linkhuge_rw
    run_test HUGETLB_ELFMAP=RW HUGETLB_SHARE=1 linkhuge_rw
    clear_hpages
}

setup_shm_sysctl() {
    if [ $UID == 0 ]; then
	SHMMAX=`cat /proc/sys/kernel/shmmax`
	SHMALL=`cat /proc/sys/kernel/shmall`
	LIMIT=$(( $HPAGE_SIZE * $TOTAL_HPAGES ))
	echo "$LIMIT" > /proc/sys/kernel/shmmax
	echo "set shmmax limit to $LIMIT"
	echo "$LIMIT" > /proc/sys/kernel/shmall
    fi
}

restore_shm_sysctl() {
    if [ $UID == 0 ]; then
	echo "$SHMMAX" > /proc/sys/kernel/shmmax
	echo "$SHMALL" > /proc/sys/kernel/shmall
    fi
}

functional_tests () {
# Kernel background tests not requiring hugepage support
    run_test zero_filesize_segment

# Library background tests not requiring hugepage support
    run_test test_root
    run_test meminfo_nohuge

# Library tests requiring kernel hugepage support
    run_test gethugepagesize
    run_test HUGETLB_VERBOSE=1 empty_mounts
    run_test HUGETLB_VERBOSE=1 large_mounts

# Tests requiring an active and usable hugepage mount
    run_test find_path
    run_test unlinked_fd
    run_test readback
    run_test truncate
    run_test shared
    run_test mprotect
    run_test mlock
    run_test misalign

# Specific kernel bug tests
    run_test ptrace-write-hugepage
    run_test icache-hygiene
    run_test slbpacaflush
    run_test_bits 64 straddle_4GB
    run_test_bits 64 huge_at_4GB_normal_below
    run_test_bits 64 huge_below_4GB_normal_above
    run_test map_high_truncate_2
    run_test misaligned_offset
    run_test truncate_above_4GB
    run_test brk_near_huge
    run_test task-size-overrun
    run_test stack_grow_into_huge

# Tests requiring an active mount and hugepage COW
    run_test private
    run_test fork-cow
    run_test direct
    run_test malloc
    preload_test HUGETLB_MORECORE=yes malloc
    run_test malloc_manysmall
    preload_test HUGETLB_MORECORE=yes malloc_manysmall
    run_test heapshrink
    run_test LD_PRELOAD=libheapshrink.so heapshrink
    preload_test HUGETLB_MORECORE=yes heapshrink
    run_test LD_PRELOAD="libhugetlbfs.so libheapshrink.so" HUGETLB_MORECORE=yes heapshrink
    preload_test HUGETLB_MORECORE=yes HUGETLB_MORECORE_SHRINK=yes heapshrink
    run_test LD_PRELOAD="libhugetlbfs.so libheapshrink.so" HUGETLB_MORECORE=yes HUGETLB_MORECORE_SHRINK=yes heapshrink
    run_test HUGETLB_VERBOSE=1 HUGETLB_MORECORE=yes heap-overflow # warnings expected

# Run the remapping tests' up-front checks
check_linkhuge_tests
# Original elflink tests
    elflink_test HUGETLB_VERBOSE=0 linkhuge_nofd # Lib error msgs expected
    elflink_test linkhuge
# Original elflink sharing tests
    elfshare_test linkshare
    elflink_and_share_test linkhuge

# elflink_rw tests
    elflink_rw_test
# elflink_rw sharing tests
    elflink_rw_and_share_test

# Accounting bug tests
# reset free hpages because sharing will have held some
# alternatively, use
    run_test chunk-overcommit
    run_test alloc-instantiate-race shared
    run_test alloc-instantiate-race private
    run_test truncate_reserve_wraparound
    run_test truncate_sigbus_versus_oom

# Test direct allocation API
    run_test get_huge_pages

# Test overriding of shmget()
    run_test shmoverride_linked
    run_test LD_PRELOAD=libhugetlbfs.so shmoverride_unlinked

# Test hugetlbfs filesystem quota accounting
    run_test quota

# Test accounting of HugePages_{Total|Free|Resv|Surp}
#  Alters the size of the hugepage pool so should probably be run last
    run_test counters
}

stress_tests () {
    ITERATIONS=10           # Number of iterations for looping tests

    # Don't update NRPAGES every time like above because we want to catch the
    # failures that happen when the kernel doesn't release all of the huge pages
    # after a stress test terminates
    NRPAGES=`free_hpages`

    run_test mmap-gettest ${ITERATIONS} ${NRPAGES}

    # mmap-cow needs a hugepages for each thread plus one extra
    run_test mmap-cow $[NRPAGES-1] ${NRPAGES}

    setup_shm_sysctl
    THREADS=10    # Number of threads for shm-fork
    # Run shm-fork once using half available hugepages, then once using all
    # This is to catch off-by-ones or races in the kernel allocated that
    # can make allocating all hugepages a problem
    if [ ${NRPAGES} -gt 1 ]; then
	run_test shm-fork ${THREADS} $[NRPAGES/2]
    fi
    run_test shm-fork ${THREADS} $[NRPAGES]

    run_test shm-getraw ${NRPAGES} /dev/full
    restore_shm_sysctl
}

while getopts "vVdt:b:" ARG ; do
    case $ARG in
	"v")
	    unset QUIET_TEST
            export HUGETLB_VERBOSE=2
	    ;;
	"V")
	    export HUGETLB_VERBOSE=99
	    ;;
	"t")
	    TESTSETS=$OPTARG
	    ;;
	"b")
	    WORDSIZES=$OPTARG
	    ;;
    esac
done

if [ -z "$TESTSETS" ]; then
    TESTSETS="func stress"
fi

if [ -z "$WORDSIZES" ]; then
    WORDSIZES="32 64"
fi

check_hugetlbfs_path

for set in $TESTSETS; do
    case $set in
	"func")
	    functional_tests
	    ;;
	"stress")
	    stress_tests
	    ;;
    esac
done

echo -e "********** TEST SUMMARY"
echo -e "*                      32-bit\t64-bit"
echo -e "*     Total testcases:	${tot_tests[32]}\t${tot_tests[64]}"
echo -e "*             Skipped:	${tot_skip[32]}\t${tot_skip[64]}"
echo -e "*                PASS:	${tot_pass[32]}\t${tot_pass[64]}"
echo -e "*                FAIL:	${tot_fail[32]}\t${tot_fail[64]}"
echo -e "*    Killed by signal:	${tot_signal[32]}\t${tot_signal[64]}"
echo -e "*   Bad configuration:	${tot_config[32]}\t${tot_config[64]}"
echo -e "* Strange test result:	${tot_strange[32]}\t${tot_strange[64]}"
echo -e "**********"
