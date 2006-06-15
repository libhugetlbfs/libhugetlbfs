#!/bin/bash

while getopts "d" opt; do
	case $opt in
		d) export HUGETLB_VERBOSE=99 ;;
	esac
done

killall -INT hugetlbd

exec hugetlbd
