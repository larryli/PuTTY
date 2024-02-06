#!/usr/bin/perl

# Script to automate some easy-to-mess-up parts of the PuTTY release
# procedure.

use strict;
use warnings;
use Getopt::Long;
use File::Find;
use File::Temp qw/tempdir/;
use LWP::UserAgent;

my $version = undef; 
my $setver = 0;
my $upload = 0;
my $precheck = 0;
my $postcheck = 0;
GetOptions("version=s" => \$version,
           "setver" => \$setver,
           "upload" => \$upload,
           "precheck" => \$precheck,
           "postcheck" => \$postcheck)
    or &usage();

# --set-version: construct a local commit which updates the version
# number, and the command-line help transcripts in the docs.
if ($setver) {
    defined $version or die "use --version";
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

# --upload: upload the release to all the places it should live, and
# check all signatures and md5sums once it arrives there.
if ($upload) {
    defined $version or die "use --version";

    # Run this inside the build.out directory.
    -d "maps-x86" or die "no maps-x86 directory in cwd";
    -d "putty" or die "no putty directory in cwd";

    0 == system("rsync", "-av", "maps-x86/",
                "atreus:src/putty-local/maps-$version")
        or die "could not upload link maps";

    for my $location (["atreus", "www/putty/$version"],
                      ["the",    "www/putty/$version"],
                      ["chiark", "ftp/putty-$version"]) {
        my ($host, $path) = @$location;
        0 == system("rsync", "-av", "putty/", "$host:$path")
            or die "could not upload release to $host";
        open my $pipe, "|-", "ssh", $host, "cd $path && sh";
        print $pipe "set -e\n";
        print $pipe "pwd\n";
        find({ wanted => sub
               {
                   if (m!^putty/(.*).gpg!) {
                       my $file = $1;
                       print $pipe "echo verifying $file\n";
                       if ($file =~ /sums$/) {
                           print $pipe "gpg --verify $file.gpg\n";
                       } else {
                           print $pipe "gpg --verify $file.gpg $file\n";
                       }
                   } elsif (m!^putty/(.*sum)s!) {
                       print $pipe "echo checking ${1}s\n";
                       print $pipe "$1 -c ${1}s\n";
                   }
               }, no_chdir => 1}, "putty");
        print $pipe "echo all verified ok\n";
        close $pipe;
        die "VERIFICATION FAILED on $host" if $? != 0;
    }

    print "Uploaded $version OK!\n";
    exit 0;
}

# --precheck and --postcheck: attempt to download the release from its
# various web and FTP locations.
if ($precheck || $postcheck) {
    defined $version or die "use --version";

    # Run this inside the build.out directory, so we can check the
    # downloaded files against the exact contents they should have.
    -d "putty" or die "no putty directory in cwd";

    my $httpprefix = "http://the.earth.li/~sgtatham/putty/";
    my $ftpprefix = "ftp://ftp.chiark.greenend.org.uk/users/sgtatham/putty-";

    # Go through all the files in build.out.
    find({ wanted => sub
           {
               if (-f $_) {
                   die unless (m!^putty/(.*)$!);
                   my $path = $1;

                   # Don't try to check .htaccess - web servers will
                   # treat it weirdly.
                   return if $path =~ m!^(.*/)?.htaccess$!;

                   print "Checking $path\n";

                   my $real_content = "";
                   open my $fh, "<", $_ or die "$_: open local file: $!";
                   $real_content .= $_ while <$fh>;
                   close $fh;

                   my $http_numbered = "${httpprefix}$version/$path";
                   my $http_latest   = "${httpprefix}latest/$path";
                   my $ftp_numbered  = "${ftpprefix}$version/$path";
                   my $ftp_latest    = "${ftpprefix}latest/$path";

                   my ($http_uri, $ftp_uri);

                   if ($precheck) {
                       # Before the 'latest' links/redirects update,
                       # we just download from explicitly version-
                       # numbered URLs.
                       $http_uri = $http_numbered;
                       $ftp_uri = $ftp_numbered;
                   }
                   if ($postcheck) {
                       # After 'latest' is updated, we're testing that
                       # the redirects work, so we download from the
                       # URLs with 'latest' in them.
                       $http_uri = $http_latest;
                       $ftp_uri = $ftp_latest;
                   }

                   # Now test-download the files themselves.
                   my $ftpdata = `curl -s $ftp_uri`;
                   printf "  got %d bytes via FTP", length $ftpdata;
                   die "FTP download for $ftp_uri did not match"
                       if $ftpdata ne $real_content;
                   print ", ok\n";

                   my $ua = LWP::UserAgent->new;
                   my $httpresponse = $ua->get($http_uri);
                   my $httpdata = $httpresponse->{_content};
                   printf "  got %d bytes via HTTP", length $httpdata;
                   die "HTTP download for $http_uri did not match"
                       if $httpdata ne $real_content;
                   print ", ok\n";

                   # Check content types on any files likely to go
                   # wrong.
                   my $ct = $httpresponse->{_headers}->{"content-type"};
                   if (defined $ct) {
                       printf "  got content-type %s", $ct;
                   } else {
                       printf "  got no content-type";
                   }
                   my $right_ct = undef;
                   if ($path =~ m/\.(hlp|cnt|chm)$/) {
                       $right_ct = "application/octet-stream";
                   } elsif ($path =~ /\.gpg$/) {
                       $right_ct = "application/pgp-signature";
                   }
                   if (defined $right_ct) {
                       if ($ct ne $right_ct) {
                           die "content-type $ct should be $right_ct";
                       } else {
                           print ", ok\n";
                       }
                   } else {
                       print "\n";
                   }

                   if ($postcheck) {
                       # Finally, if we're testing the 'latest' URL,
                       # also check that the HTTP redirect header was
                       # present and correct.
                       my $redirected = $httpresponse->{_request}->{_uri};
                       printf "  redirect -> %s\n", $redirected;
                       die "redirect header wrong for $http_uri"
                           if $redirected ne $http_numbered;
                   }
               }
           }, no_chdir => 1}, "putty");

    print "Check OK\n";
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
