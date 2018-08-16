#!/usr/bin/env perl
# Copyright 2016, CZ.NIC z.s.p.o. (http://www.nic.cz/)
#
# This file is part of the turris updater.
#
# Updater is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Updater is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Updater.  If not, see <http://www.gnu.org/licenses/>.

# Appends coverage info from lua to given output
# Usage: ./lua_coverage2info.sh COVERAGE_DIR OUT_INFO SOURCE
# Where COVERAGE_DIR is directory with coverage files from Lua, OUT_INFO is
# output file and SOURCE is original source directory
# TODO support functions, currently we only handle executed lines.
use common::sense;
use Cwd 'abs_path';

my @cfs = <$ARGV[0]/*>;
open my $outf, '>>', $ARGV[1] or die "Couldn't append to file $ARGV[1]: $!\n";
my $source = $ARGV[2];

# We intensionally ignore some modules here to not show them in output.
# TODO probably implement some search here instead
my %module2path = (
	coverage => "coverage.lua",
	utils => "autoload/a_02_utils.lua",
	testing => "autoload/a_03_testing.lua",
	logging => "autoload/a_04_logging.lua",
	cleanup => "autoload/a_05_cleanup.lua",
	syscnf => "autoload/a_06_syscnf.lua",
	backend => "autoload/a_08_backend.lua",
	transaction => "autoload/a_09_transaction.lua",
	uri => "autoload/a_10_uri.lua",
	requests => "autoload/a_11_requests.lua",
	sandbox => "autoload/a_12_sandbox.lua",
	postprocess => "autoload/a_13_postprocess.lua",
	planner => "autoload/a_14_planner.lua",
	updater => "autoload/a_15_updater.lua",
);
foreach my $module (keys %module2path) {
	$module2path{$module} = abs_path($source . '/src/lib/' . $module2path{$module});
}

# Collects hits from file and writes them to output
# first argument is source file, second one is file with coverage data
# This implementation is very naive, specially when we are reading source and adding unexecuted lines.
sub add_file($$) {
	my ($source, $lines) = @_;
	print $outf "TN:\n";
	print $outf "SF:" . abs_path($source) . "\n"; # absolute path to source file
	open my $inf, '<', $lines or die "Couldn't read $lines: $!\n";
	my %dt;
	while (<$inf>) {
		my ($line, $count) = /^([\d]+):([\d]+)$/;
		$dt{$line} += $count;
	}
	close $inf;
	# Read source and found lines with code without execution history
	open my $inf, $source or die "Couldn't read $lines: $!\n";
	my $i = 1;
	my $multiline = 0; # ignore multi-line comments
	while (<$inf>) {
		$multiline = 1 if /--\[\[/;
		$dt{$i} //= 0 unless 
				$multiline or # we are in multi-line comment
				/^[\s]*$/ or # ignore empty lines
				/^[\s]*--/ or # Ignore single line comments
				/^(end|else|\)|\}|[\s])*$/ # Ignore lines just with end, else, ) or }
			;
		$multiline = 0 if /\]\]/;
		$i++;
	}
	close $inf;
	foreach my $l (sort { $a <=> $b } keys %dt) {
		print $outf "DA:$l,$dt{$l}\n";
	}
	my $lines = scalar keys %dt;
	print $outf "LH:$lines\n";
	# We print that we executed all lines we know about, but it seems make no difference, we receive correct percent coverage anyway.
	print $outf "LF:$lines\n";
	print $outf "end_of_record\n";
}

foreach (@cfs) {
	chomp;
	my $cfsource = s/^$ARGV[0]\///r =~ s/-/\//gr =~ s/\.lua_lines//r;
	if ($cfsource =~ s/^@//) { # We should have path
		$cfsource = $source . '/' . $cfsource; # relative to source
	} else { # We have module name
		$cfsource = $module2path{$cfsource} if defined $module2path{$cfsource};
	}
	if (-f $cfsource) {
		add_file $cfsource, $_;
	} else {
		warn "$cfsource ignored. Can't locate file.\n";
	}
}
close $outf;
