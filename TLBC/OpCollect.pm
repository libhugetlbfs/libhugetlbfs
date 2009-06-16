#
# OpCollect.pm
#
# This module contains all the setup, data collection, and cleanup methods
# for collecting CPU performance counter information from oprofile.
# Licensed under LGPL 2.1 as packaged with libhugetlbfs
# (c) Eric Munson 2009

package TLBC::OpCollect;

use warnings;
use strict;
use Carp;

use FindBin qw($Bin);
use lib "$Bin/lib";
use TLBC::DataCollect;

our @ISA = qw(TLBC::DataCollect);

my $count = 0;
my $reference;

#use interface 'DataCollect';

sub _clear_oprofile()
{
	my $self = shift;
	system("opcontrol --reset > /dev/null 2>&1");
	system("opcontrol --stop > /dev/null 2>&1");
	system("opcontrol --reset > /dev/null 2>&1");
	system("opcontrol --deinit > /dev/null 2>&1");
	return $self;
}

sub _setup_oprofile()
{
	my $self = shift;
	my $vmlinux = shift;
	my $event = shift;
	my $cmd = "$Bin/oprofile_start.sh --vmlinux=$vmlinux " .
		   "--event=$event  > /dev/null 2>&1";
	system($cmd) == 0 or die "Failed to start oprofile\n";
	return $self;
}

sub _get_event()
{
	my $self = shift;
	my $event = shift;
	my @vals;
	my $lowlevel_event;
	$lowlevel_event = `$Bin/oprofile_map_events.pl --event $event 2>/dev/null`;
	chomp($lowlevel_event);
	if ($lowlevel_event eq "" || $lowlevel_event !~ /^[A-Z0-9_]+:[0-9]+/) {
		die "Unable to find $event event for this CPU\n";
	}

	@vals = split(/:/, $lowlevel_event);
	$count = $vals[1];
	return $self;
}

sub new()
{
	my $class = shift;
	if ($reference) {
		return $reference;
	}

	$reference = {@_};
	bless($reference, $class);
	return $reference;
}

sub setup()
{
	my $self = shift;
	my $vmlinux = shift;
	my $event = shift;
	$self->_get_event($event);
	$self->_clear_oprofile();
	$self->_setup_oprofile($vmlinux, $event);
	return $self;
}

sub samples()
{
	return $count;
}

sub get_current_eventcount()
{
	my @results;
	my $line;
	my $report;
	my $hits = 0;
	my $self = shift;
	my $binName = shift;

	system("opcontrol --dump > /dev/null 2>&1");
	$report = `opreport`;
	@results = split(/\n/, $report);
	foreach $line (@results) {
		if ($line =~ /$binName/) {
			chomp($line);
			$line =~ s/^\s+//;
			$line =~ s/\s+$//;
			my @vals = split(/ /, $line);
			$hits += $vals[0];
		}
	}
	return $hits;
}

sub shutdown()
{
	my $self = shift;
	_clear_oprofile();
	return $self;
}

1;
