#! /bin/sh

cd tests

export QUIET_TEST=1
export HUGETLB_VERBOSE=2

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

run_test () {
    for bits in 32 64; do
	if [ -d obj$bits ]; then
	    echo -n "$@ ($bits):	"
	    PATH="obj$bits" LD_LIBRARY_PATH="../obj$bits" $ENV "$@"
	fi
    done
}

preload_test () {
    run_test LD_PRELOAD=libhugetlbfs.so "$@"
}

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
preload_test HUGETLB_MORECORE=yes malloc
run_test malloc_manysmall
preload_test HUGETLB_MORECORE=yes malloc_manysmall
