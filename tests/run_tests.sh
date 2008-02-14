#! /bin/bash

export QUIET_TEST=1
export HUGETLB_VERBOSE=2
unset HUGETLB_ELF
unset HUGETLB_MORECORE

ENV=/usr/bin/env

function free_hpages() {
	H=$(grep 'HugePages_Free:' /proc/meminfo | cut -f2 -d:)
	[ -z "$H" ] && H=0
	echo "$H"
}

function get_and_set_hugetlbfs_path() {
	HUGETLB_PATH=$(PATH="obj32:obj64:$PATH" LD_LIBRARY_PATH="$LD_LIBRARY_PATH:../obj32:../obj64" get_hugetlbfs_path)
    	if [ $? != 0 ]; then
		echo "run_tests.sh: unable to find hugetlbfs mountpoint"
		exit 1
    	fi
}

function clear_hpages() {
    rm -rf "$HUGETLB_PATH"/elflink-uid-`id -u`
}

TOTAL_HPAGES=$(grep 'HugePages_Total:' /proc/meminfo | cut -f2 -d:)
[ -z "$TOTAL_HPAGES" ] && TOTAL_HPAGES=0
HPAGE_SIZE=$(grep 'Hugepagesize:' /proc/meminfo | awk '{print $2}')
[ -z "$HPAGE_SIZE" ] && HPAGE_SIZE=0
HPAGE_SIZE=$(( $HPAGE_SIZE * 1024 ))

run_test_bits () {
    BITS=$1
    shift

    if [ -d obj$BITS ]; then
	echo -n "$@ ($BITS):	"
	PATH="obj$BITS:$PATH" LD_LIBRARY_PATH="$LD_LIBRARY_PATH:../obj$BITS:obj$BITS" $ENV "$@"
    fi
}

run_test () {
    for bits in $WORDSIZES; do
	run_test_bits $bits "$@"
    done
}

skip_test () {
    echo "$@:	SKIPPED"
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
    # Run each elfshare test invocation independently - clean up the
    # sharefiles before and after in the first set of runs, but leave
    # them there in the second:
    clear_hpages
    run_test HUGETLB_SHARE=1 "$@" "xB.$baseprog"
    clear_hpages
    run_test HUGETLB_SHARE=1 "$@" "xBDT.$baseprog"
    clear_hpages
    run_test HUGETLB_SHARE=1 "$@" "xB.$baseprog"
    run_test HUGETLB_SHARE=1 "$@" "xBDT.$baseprog"
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
    run_test HUGETLB_SHARE=1 "$@" "xB.$baseprog"
    run_test HUGETLB_SHARE=1 "$@" "xB.$baseprog"
    clear_hpages
    run_test HUGETLB_SHARE=1 "$@" "xBDT.$baseprog"
    run_test HUGETLB_SHARE=1 "$@" "xBDT.$baseprog"
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

setup_dynamic_pool_sysctl() {
    if [ -f /proc/sys/vm/nr_overcommit_hugepages ]; then
    	DYNAMIC_POOL=`cat /proc/sys/vm/nr_overcommit_hugepages`
    	echo 10 > /proc/sys/vm/nr_overcommit_hugepages
    fi
}

restore_dynamic_pool_sysctl() {
    if [ -f /proc/sys/vm/nr_overcommit_hugepages ]; then
        echo "$DYNAMIC_POOL" > /proc/sys/vm/nr_overcommit_hugepages
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
    elflink_test HUGETLB_VERBOSE=0 linkhuge_nofd # Lib error msgs expected
    elflink_test linkhuge

# Sharing tests
    elfshare_test linkshare
    elflink_and_share_test linkhuge

# Accounting bug tests
# reset free hpages because sharing will have held some
# alternatively, use
    run_test chunk-overcommit
    run_test alloc-instantiate-race shared
    run_test alloc-instantiate-race private
    run_test truncate_reserve_wraparound
    run_test truncate_sigbus_versus_oom

# Test hugetlbfs filesystem quota accounting
    run_test quota

# Test accounting of HugePages_{Total|Free|Resv|Surp}
#  Alters the size of the hugepage pool so should probably be run last
    setup_dynamic_pool_sysctl
    run_test counters
    restore_dynamic_pool_sysctl
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

get_and_set_hugetlbfs_path

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
