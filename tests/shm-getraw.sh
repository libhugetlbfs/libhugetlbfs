#!/bin/bash

. wrapper-utils.sh

#shm-getraw will fail if the hugepage size is different from the system default

def_hpage_size=`grep 'Hugepagesize:' /proc/meminfo | awk '{print $2}'`
let "def_hpage_size *= 1024"

if [ -z "$HUGETLB_DEFAULT_PAGE_SIZE" ]; then
	EXP_RC=$RC_PASS
elif [ "$def_hpage_size" -eq "$HUGETLB_DEFAULT_PAGE_SIZE" ]; then
	EXP_RC=$RC_PASS
else
	EXP_RC=$RC_FAIL
fi

exec_and_check $EXP_RC shm-getraw "$@"
