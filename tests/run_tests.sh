#! /bin/sh

export QUIET_TEST=1
export HUGETLB_VERBOSE=2
unset HUGETLB_ELF
unset HUGETLB_MORECORE

ENV=/usr/bin/env

TOTAL_HPAGES=$(grep 'HugePages_Total:' /proc/meminfo | cut -f2 -d:)
[ -z "$TOTAL_HPAGES" ] && TOTAL_HPAGES=0
FREE_HPAGES=$(grep 'HugePages_Free:' /proc/meminfo | cut -f2 -d:)
[ -z "$FREE_HPAGES" ] && FREE_HPAGES=0
HPAGE_SIZE=$(grep 'Hugepagesize:' /proc/meminfo | awk '{print $2}')
[ -z "$HPAGE_SIZE" ] && HPAGE_SIZE=0
HPAGE_SIZE=$(( $HPAGE_SIZE * 1024 ))

run_test_bits () {
    BITS=$1
    shift

    if [ -d obj$BITS ]; then
	echo -n "$@ ($BITS):	"
	PATH="obj$BITS" LD_LIBRARY_PATH="../obj$BITS" $ENV "$@"
    fi
}

run_test () {
    for bits in 32 64; do
	run_test_bits $bits "$@"
    done
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
    run_test "$@" "xB.$baseprog"
    run_test "$@" "xBDT.$baseprog"
    # Test we don't blow up if HUGETLB_MINIMAL_COPY is diabled
    run_test HUGETLB_MINIMAL_COPY=no "$@" "xB.$baseprog"
    run_test HUGETLB_MINIMAL_COPY=no "$@" "xBDT.$baseprog"
    # Test that HUGETLB_ELFMAP=no inhibits remapping as intended
    run_test HUGETLB_ELFMAP=no "$@" "xB.$baseprog"
    run_test HUGETLB_ELFMAP=no "$@" "xBDT.$baseprog"
}

elfshare_test () {
    args=("$@")
    N="$[$#-1]"
    baseprog="${args[$N]}"
    unset args[$N]
    set -- "${args[@]}"
    killall -HUP hugetlbd
    run_test HUGETLB_SHARE=2 "$@" "xB.$baseprog" 10
    killall -HUP hugetlbd
    run_test HUGETLB_SHARE=1 "$@" "xB.$baseprog" 10
    killall -HUP hugetlbd
    run_test HUGETLB_SHARE=2 "$@" "xBDT.$baseprog" 10
    killall -HUP hugetlbd
    run_test HUGETLB_SHARE=1 "$@" "xBDT.$baseprog" 10
}

setup_shm_sysctl() {
    SHMMAX=`cat /proc/sys/kernel/shmmax`
    SHMALL=`cat /proc/sys/kernel/shmall`
    LIMIT=$(( $HPAGE_SIZE * $TOTAL_HPAGES ))
    echo "$LIMIT" > /proc/sys/kernel/shmmax
    echo "set shmmax limit to $LIMIT"
    echo "$LIMIT" > /proc/sys/kernel/shmall
}

restore_shm_sysctl() {
    echo "$SHMMAX" > /proc/sys/kernel/shmmax
    echo "$SHMALL" > /proc/sys/kernel/shmall
}

functional_tests () {
    #run_test dummy
# Kernel background tests not requiring hugepage support
    run_test zero_filesize_segment

# Library background tests not requiring hugepage support
    run_test test_root
    run_test meminfo_nohuge

# Library tests requiring kernel hugepage support
    run_test gethugepagesize
    run_test empty_mounts

# Tests requiring an active and usable hugepage mount
    run_test find_path
    run_test unlinked_fd
    run_test readback
    run_test truncate
    run_test shared
    run_test mprotect
    run_test mlock $FREE_HPAGES

# Specific kernel bug tests
    run_test ptrace-write-hugepage
    run_test icache-hygeine
    run_test slbpacaflush
    run_test_bits 64 straddle_4GB
    run_test_bits 64 huge_at_4GB_normal_below
    run_test_bits 64 huge_below_4GB_normal_above

# Tests requiring an active mount and hugepage COW
    run_test private
    run_test malloc
    preload_test HUGETLB_MORECORE=yes malloc
    run_test malloc_manysmall
    preload_test HUGETLB_MORECORE=yes malloc_manysmall
    elflink_test HUGETLB_VERBOSE=0 linkhuge_nofd # Lib error msgs expected
    elflink_test linkhuge

# Sharing tests
    # stop all running instances for clean testing
    killall -INT hugetlbd
    # start the daemon in the bg
    PATH=../obj32:../obj64:$PATH hugetlbd
    # XXX: Wait for daemon to start
    sleep 5
    elfshare_test linkshare
    # stop our instance of the daemon
    killall -INT hugetlbd

# Accounting bug tests
# reset free hpages because sharing will have held some
# alternatively, use
# killall -HUP hugetlbd
# to make the sharing daemon give up the files
    FREE_HPAGES=$(grep 'HugePages_Free:' /proc/meminfo | cut -f2 -d:)
    [ -z "$FREE_HPAGES" ] && FREE_HPAGES=0
    run_test chunk-overcommit $FREE_HPAGES
    run_test alloc-instantiate-race $FREE_HPAGES
}

stress_tests () {
    ITERATIONS=10           # Number of iterations for looping tests
    NRPAGES=$FREE_HPAGES

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

while getopts "vVdt:" ARG ; do
    case $ARG in
	"v")
	    unset QUIET_TEST=1
	    ;;
	"V")
	    export HUGETLB_VERBOSE=99
	    ;;
	"t")
	    TESTSETS=$OPTARG
	    ;;
    esac
done

if [ -z "$TESTSETS" ]; then
    TESTSETS="func stress"
fi

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
