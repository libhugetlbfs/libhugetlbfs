#! /bin/sh

cd tests

export QUIET_TEST=1
export HUGETLB_VERBOSE=2
unset HUGETLB_ELF
unset HUGETLB_MORECORE

ENV=/usr/bin/env

while [ $# -gt 0 ]; do
    case "$1" in
	'-v')
	    shift
	    unset QUIET_TEST
	    ;;
	'-V')
	    shift
	    export HUGETLB_VERBOSE=99
	    ;;
    esac
done

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
    run_test "$@"
    # Test we don't blow up if not linked for hugepage
    preload_test "$@" 
    run_test "xB.$@"
    run_test "xBDT.$@"
}

#run_test dummy
run_test zero_filesize_segment
run_test gethugepagesize
run_test test_root
run_test find_path
run_test unlinked_fd
run_test empty_mounts
run_test readback
run_test truncate
run_test shared
run_test private
run_test malloc
run_test ptrace-write-hugepage
preload_test HUGETLB_MORECORE=yes malloc
run_test malloc_manysmall
preload_test HUGETLB_MORECORE=yes malloc_manysmall
run_test_bits 64 straddle_4GB
run_test_bits 64 huge_at_4GB_normal_below
run_test_bits 64 huge_below_4GB_normal_above
elflink_test HUGETLB_VERBOSE=0 linkhuge_nofd # Lib error msgs expected
elflink_test linkhuge
