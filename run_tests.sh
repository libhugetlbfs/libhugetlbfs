#! /bin/sh

cd tests

export LD_LIBRARY_PATH=..

export QUIET_TEST=1
ENV=/usr/bin/env

while [ $# -gt 0 ]; do
    case "$1" in
	'-v')
	    shift
	    unset QUIET_TEST
	    ;;
	'-V')
	    shift
	    export HUGETLB_VERBOSE=2
	    ;;
    esac
done

run_test () {
    echo -n "$@:	"
    PATH="." $ENV "$@"
}

preload_test () {
    run_test LD_PRELOAD=libhugetlbfs.so "$@"
}

run_test gethugepagesize
run_test test_root
run_test find_path
run_test unlinked_fd
run_test readback
run_test truncate
run_test shared
run_test private
run_test malloc
preload_test HUGETLB_MORECORE=0 malloc
#run_test malloc_manysmall
#preload_test HUGETLB_MORECORE=auto malloc_manysmall
