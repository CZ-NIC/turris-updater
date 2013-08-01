#!/usr/bin/perl
use common::sense;
use utf8;
use Scalar::Util qw(weaken);
use Data::Dumper;

# Where to get the packages and their list
my $url = $ARGV[0] or die "Expected the URL of the repository as my first argument\n";

# Download and decompress the list of opkg packages.

# FIXME: The use of shell here is technically insecure, but the input is ours,
# so it should be OK. Still, it would be nice to do it properly sometime.
my $list_url = "$url/Packages.gz";
open my $descriptions, '-|', "wget '$list_url' -O - | gzip -d" or die "Could not start download of $list_url $!\n";
my @packages;
{
	# The package descriptions are separated by one empty line, so pretend an empty line is EOF and read all „lines“
	local $/ = "\n\n";
	@packages = <$descriptions>;
}
close $descriptions or die "Failed to download $list_url\n";

# Parse the packages.

# Drop the newlines at the end.
s/\n\n$// foreach @packages;

# The fields are separated by new lines. However, if the next line starts with whitespace, it is continuation of the previous.
# So, split to the fields. Then, split each field to the name and value and then create a hash from the key-value pairs.
@packages = map +{
	map {
		my @fields = split /:\s*/, $_, 2;
		s/^\s+//gm foreach @fields; # Drop the start-of-line whitespaces
		@fields;
	} split /\n(?!\s)/ }, @packages;

# Index the packages by their name and create data structures for them.
my %packages = map { $_->{Package} => { desc => $_ } } @packages;
# Link the packages by dependencies and reverse dependencies

while (my ($name, $package) = each %packages) {
	my @deps = split /,\s*/, $package->{desc}->{Depends};
	for my $dep (@deps) {
		# FIXME: Some version handling instead of ignoring them
		$dep =~ s/\s*\(.*\)\s*//;
		my $dpackage = $packages{$dep} // ( warn "Dependency $dep of $name is missing\n", next );
		$dpackage->{revdep}->{$name} = $package;
		weaken $dpackage->{revdep}->{$name};
		$package->{dep}->{$dep} = $dpackage;
	}
}

# Get list of desired packages from stdin
my @desired = <STDIN>;
chomp @desired;
my %desired = map { my ($name, $flags) = split /\s+/, $_, 2; ($name, $flags); } @desired;

sub mark($) {
	my ($package) = @_;
	return if $package->{desired};
	$package->{desired} = 1;
	&mark($_) foreach values %{$package->{dep}};
}

# Mark each packet as desired, and its deps recursively too. Skip the ones to remove.
for my $desired (keys %desired) {
	next if $desired{$desired} =~ /R/;
	mark($packages{$desired} // die "Package $desired doesn't exist\n");
}

# Generate list of packages to be installed
my %final = map { $_->{desc}->{Package} => $_ } grep $_->{desired}, values %packages;

# Keep picking the things without dependencies, output them to the list and remove them as deps from others
mkdir 'packages';
while (my @nodeps = grep { not %{$_->{dep}} } values %final) {
	for my $package (@nodeps) {
		my $name = $package->{desc}->{Package};
		# Drop from the list of stuff to do
		delete $final{$name};
		for my $rdep (values %{$package->{revdep}}) {
			delete $rdep->{dep}->{$name}
		}
		# Handle the package
		print "$name\t$package->{desc}->{Version}\t$desired{$name}\n";
		die "Failed to download $name\n" if system 'wget', '-q', "$url/$package->{desc}->{Filename}", '-O', "packages/$name-$package->{desc}->{Version}.ipk";
		warn "Package $name should be encrypted, but that's not supported yet ‒ you need to encrypt manually\n" if $desired{$name} =~ /E/;
	}
}

# Make sure nothing is left
die "Circular dependencies in ", (join ", ", keys %final), "\n" if %final;

# Output the packages to remove
print map "$_\t-\t$desired{$_}\n", (grep $desired{$_} =~ /R/, keys %desired);
