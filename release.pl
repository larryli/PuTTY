#!/usr/bin/perl

# Script to automate some easy-to-mess-up parts of the PuTTY release
# procedure.

use strict;
use warnings;
use Getopt::Long;
use File::Temp qw/tempdir/;

my $version = undef; 
GetOptions("set-version=s" => \$version)
    or &usage();

if (defined $version) {
    0 == system "git", "diff-index", "--quiet", "--cached", "HEAD"
        or die "index is dirty";
    0 == system "git", "diff-files", "--quiet" or die "working tree is dirty";
    -f "Makefile" and die "run 'make distclean' first";
    my $builddir = tempdir(DIR => ".", CLEANUP => 1);
    0 == system "./mkfiles.pl" or die;
    0 == system "cd $builddir && ../configure" or die;
    0 == system "cd $builddir && make pscp plink RELEASE=${version}" or die;
    our $pscp_transcript = `cd $builddir && ./pscp --help`;
    $pscp_transcript =~ s/^Unidentified build/Release ${version}/m or die;
    $pscp_transcript =~ s/^/\\c /mg;
    our $plink_transcript = `cd $builddir && ./plink --help`;
    $plink_transcript =~ s/^Unidentified build/Release ${version}/m or die;
    $plink_transcript =~ s/^/\\c /mg;
    &transform("LATEST.VER", sub { s/^\d+\.\d+$/$version/ });
    &transform("windows/putty.iss", sub {
        s/^(AppVerName=PuTTY version |VersionInfoTextVersion=Release |AppVersion=|VersionInfoVersion=)\d+\.\d+/$1$version/ });
    our $transforming = 0;
    &transform("doc/pscp.but", sub {
        if (/^\\c.*>pscp$/) { $transforming = 1; $_ .= $pscp_transcript; }
        elsif (!/^\\c/) { $transforming = 0; }
        elsif ($transforming) { $_=""; }
    });
    $transforming = 0;
    &transform("doc/plink.but", sub {
        if (/^\\c.*>plink$/) { $transforming = 1; $_ .= $plink_transcript; }
        elsif (!/^\\c/) { $transforming = 0; }
        elsif ($transforming) { $_=""; }
    });
    &transform("Buildscr", sub {
        s!^(set Epoch )\d+!$1 . sprintf "%d", time/86400 - 1000!e });
    0 == system ("git", "commit", "-a", "-m",
                 "Update version number for ${version} release.") or die;
    exit 0;
}

&usage();

sub transform {
    my ($filename, $proc) = @_;
    my $file;
    open $file, "<", $filename or die "$file: open for read: $!\n";
    my $data = "";
    while (<$file>) {
        $proc->();
        $data .= $_;
    }
    close $file;
    open $file, ">", $filename or die "$file: open for write: $!\n";
    print $file $data;
    close $file or die "$file: close after write: $!\n";;
}

sub usage {
    die "usage: release.pl --set-version=X.YZ\n";
}
