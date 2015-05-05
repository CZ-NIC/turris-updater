#!/usr/bin/perl
use common::sense;
use utf8;
use Getopt::Long;
use AnyEvent;
use AnyEvent::HTTP;
use AnyEvent::Util qw(run_cmd);
use File::Basename qw(dirname);
use File::Temp qw(tempdir);
use JSON qw();

my (@lists, @definitions, @branches, $verbose, $url, $output_file);

GetOptions
	verbose => \$verbose,
	'list=s' => \@lists,
	'definition=s' => \@definitions,
	'branch=s' => \@branches,
	'url=s' => \$url,
	'output=s' => \$output_file
or die "Bad params\n";

my $err = 0;

sub dbg(@) {
	print STDERR "DBG: ", @_ if $verbose;
}

my $unpack_cmd = dirname($0) . "/pkg-unpack";
dbg "Using $unpack_cmd as the unpacking command\n";
my $unpack_limit = 12;
my $workdir = tempdir(CLEANUP => 1);
dbg "Using $workdir as working directory\n";

my @condvars;

sub new_cv() {
	my $cv = AnyEvent->condvar;
	push @condvars, $cv;
	return $cv;
}

my (%packages, @queue);

sub process_output($) {
	my ($name) = @_;
	open my $output, '<:utf8', \$packages{$name}->{output} or die "Couldn't map output for $name to pseudo-file: $!\n";
	my (%configs, %hashes);
	while (<$output>) {
		chomp;
		if (/^-(.*)$/) {
			$configs{$1} = 1;
		} elsif (/^([a-f0-9]{32}) \*\.(.*)$/) {
			$hashes{$2} = $1;
		} else {
			warn "Unmatched line in output of $name: $_\n";
		}
	}
	delete @hashes{keys %configs};
	$packages{$name}->{files} = \%hashes;
}

sub check_queue() {
	if (@queue && $unpack_limit) {
		$unpack_limit --;
		my $t = pop @queue;
		my $output;
		dbg "Going to unpack $t->{name}\n";
		my $finished = run_cmd [$unpack_cmd, "$workdir/$t->{name}"],
		'>' => \$output,
		'<' => \$t->{data},
		close_all => 1;
		$finished->cb(sub {
			my $ecode = shift->recv;
			if ($ecode) {
				warn "Failed to unpack $t->{name}: $ecode\n";
				$err = 1;
			} else {
				dbg "Unpacked $t->{name}\n";
				$packages{$t->{name}}->{output} = $output;
				process_output $t->{name};
			}
			$t->{cv}->send;
			$unpack_limit ++;
			&check_queue();
		});
	}
}

sub handle_package($$) {
	my ($name, $data) = @_;
	my $cv = new_cv;
	push @queue, {
		name => $name,
		data => $data,
		cv => $cv
	};
	check_queue;
}

sub task($$&) {
	my ($name, $url, $cb) = @_;
	my $cv = new_cv;
	dbg "Want $name $url\n";
	http_get $url, tls_ctx => "high", sub {
		my ($body, $hdrs) = @_;
		if (defined $body and $hdrs->{Status} == 200) {
			$cb->($body);
		} else {
			warn "Failed to download $name $url: $hdrs->{Status} $hdrs->{Reason}\n";
			$err = 1;
		}
		$cv->send;
	};
}

sub get_package($$) {
	my ($name, $version) = @_;
	my $full = "$name-$version";
	return dbg "Package $full already queued\n" if exists $packages{$full};
	$packages{$full} = {};
	task package => "$url/packages/$full.ipk", sub {
		my ($body) = @_;
		handle_package $full, $body;
	};
}

sub handle_list($) {
	my ($data) = @_;
	for (split /\n/, $data) {
		my ($name, $version, $flags, $hash) = split;
		next if $flags =~ /R/;
		get_package $name, $version;
	}
}

sub get_list($) {
	my ($name) = @_;
	task list => "$url/lists/$name", \&handle_list;
}

sub handle_definition($$) {
	my ($branch, $data) = @_;
	for my $line (split /\n/, $data) {
		if ($line =~ /^\['(.*)'\] = {$/) {
			get_list "$1-$branch";
		}
	}
}

sub get_definition($$) {
	my ($branch, $name) = @_;
	task definition => "$url/lists/$name-$branch", sub {
		my ($body) = @_;
		handle_definition $branch, $body;
	};
}

for my $branch (@branches) {
	get_list $branch;
	for my $list (@lists) {
		get_list "$list-$branch";
	}
	for my $def (@definitions) {
		get_definition $branch, $def;
	}
}

while (my $cv = pop @condvars) {
	$cv->recv;
}

my %prepared;
@prepared{keys %packages} = map $_->{files}, values %packages;
delete @prepared{grep { !%{$prepared{$_}} } keys %prepared};

open my $out, '>:utf8', $output_file or die "Couldn't output to file $output_file $!\n";
print $out JSON->new->allow_nonref->pretty->encode(\%prepared);
close $out;

exit $err;
