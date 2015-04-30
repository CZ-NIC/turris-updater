#!/usr/bin/perl
use common::sense;
use utf8;
use Getopt::Long;
use AnyEvent;
use AnyEvent::HTTP;

my (@lists, @definitions, @branches, $verbose, $url);

GetOptions
	verbose => \$verbose,
	'list=s' => \@lists,
	'definition=s' => \@definitions,
	'branch=s' => \@branches,
	'url=s' => \$url
or die "Bad params\n";

my $err = 0;

sub dbg(@) {
	print STDERR "DBG: ", @_ if $verbose;
}

my @condvars;

sub new_cv() {
	my $cv = AnyEvent->condvar;
	push @condvars, $cv;
	return $cv;
}

my %packages;

sub handle_package($$) {
	my ($name, $data) = @_;
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

exit $err;
