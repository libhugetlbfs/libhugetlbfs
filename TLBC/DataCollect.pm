#
# DataCollect.pm
#
# This module is the base class for DTLB data collection.  This class is a
# interface class only, to add a new collection method inherit from this
# class and use it in calc_missrate.pl
# Licensed under LGPL 2.1 as packaged with libhugetlbfs
# (c) Eric Munson 2009

package TLBC::DataCollect;

use warnings;
use strict;
use Carp;
use base;

sub new()
{
}

##
# The setup method should take care of setting up the data collector for
# collecting DTLB miss data.  This method takes no args and returns $self

sub setup()
{
}

##
# This method should the return the total event count as of its
# invocation.  This method takes no args and it returns the total number
# of DTLB misses.

sub get_current_eventcount()
{
}

##
# The shutdown method should stop the data collection and do any clean up
# necessary.  This method takes no args and returns $self

sub shutdown()
{
}

1;
