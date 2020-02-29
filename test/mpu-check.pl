#!/usr/bin/perl

# Trivial command-line client for the function
# Math::Prime::Util::verify_prime, which checks a certificate of
# primality in MPU format.

use strict;
use warnings;
use Math::Prime::Util;

Math::Prime::Util::prime_set_config(verbose => 1);

my $cert = "";
$cert .= $_ while <<>>;

my $success = Math::Prime::Util::verify_prime($cert);

die "verification failed\n" unless $success;
warn "verification succeeded\n";
