#!/bin/bash

while getopts "d" opt; do
	case $opt in
		d) export HUGETLB_VERBOSE=99 ;;
	esac
done

export PATH=${PATH}:./obj32/:./obj64/

killall -INT hugetlbd

exec hugetlbd
