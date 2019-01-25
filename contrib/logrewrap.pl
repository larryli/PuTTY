#!/usr/bin/perl

# Process a PuTTY SSH packet log that has gone through inappropriate
# line wrapping, and try to make it legible again.
#
# Motivation: people often include PuTTY packet logs in email
# messages, and if they're not careful, the sending MUA 'helpfully'
# wraps the lines at 72 characters, corrupting all the hex dumps into
# total unreadability.
#
# But as long as it's only the ASCII part of the dump at the end of
# the line that gets wrapped, and the hex part is untouched, this is a
# mechanically recoverable kind of corruption, because the ASCII is
# redundant and can be reconstructed from the hex. Better still, you
# can spot lines in which this has happened (because the ASCII at the
# end of the line is a truncated version of what we think it should
# say), and use that as a cue to remove the following line.

use strict;
use warnings;

while (<<>>) {
    if (/^  ([0-9a-f]{8})  ((?:[0-9a-f]{2} ){0,15}(?:[0-9a-f]{2}))/) {
        my $addr = $1;
        my $hex = $2;
        my $ascii = "";
        for (my $i = 0; $i < length($2); $i += 3) {
            my $byte = hex(substr($hex, $i, 2));
            my $char = ($byte >= 32 && $byte < 127 ? chr($byte) : ".");
            $ascii .= $char;
        }
        $hex = substr($hex . (" " x 48), 0, 47);
        my $old_line = $_;
        chomp($old_line);
        my $new_line = "  $addr  $hex  $ascii";
        if ($old_line ne $new_line and
            $old_line eq substr($new_line, 0, length($old_line))) {
            print "$new_line\n";
            <<>>; # eat the subsequent wrapped line
            next;
        }
    }
    print $_;
}
