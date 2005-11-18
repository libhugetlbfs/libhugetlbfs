#! /bin/sh

cd tests

export LD_LIBRARY_PATH=..

if [ "$1" != "-v" ] ; then
    export QUIET_TEST=1
fi

run_test () {
    echo -n "$@:	"
    ./"$@"
}

run_test gethugepagesize
run_test test_root_hugetlbfs
run_test find_path
run_test tempfile
