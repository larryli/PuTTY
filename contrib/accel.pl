#! /usr/bin/perl -w

# $Id: accel.pl,v 1.2 2003/01/21 21:05:35 jacob Exp $
# Grotty script to check for clashes in the PuTTY config dialog keyboard
# accelerators in windlg.c, and to check the comments are still up to
# date. Based on:
#   windlg.c:1.201
#   win_res.rc:1.59 (for global accelerators)
#   winctrls.c:1.20 (for prefslist() hardcoded accelerators)
# usage: accel.pl [-q] [-v] [-f windlg-alt.c]

use strict;
use English;
use Getopt::Std;

# Accelerators that nothing in create_controls() must use
# (see win_res.rc, windlg.c:GenericMainDlgProc())
my $GLOBAL_ACCEL = "acgoh";

my $all_ok = 1;
my %opts = ();

# Sort a string of characters.
sub sortstr {
    my ($str) = @_;
    return join("",sort(split(//,$str)));
}

# Return duplicates in a sorted string of characters.
sub dups {
    my ($str) = @_;
    my %dups = ();
    my $chr = undef;
    for (my $i=0; $i < length($str); $i++) {
        if (defined($chr) &&
            $chr eq substr($str,$i,1)) {
            $dups{$chr} = 1;
        }
        $chr = substr($str,$i,1);
    }
    return keys(%dups);
}

sub mumble {
    print @_ unless exists($opts{q});
}

sub whinge {
    mumble(@_);
    $all_ok = 0;
    return 0;
}

# Having worked out stuff about a particular panel, check it for
# plausibility.
sub process_panel {
    my ($panel, $cmtkeys, $realkeys) = @_;
    my ($scmt, $sreal);
    my $ok = 1;
    $scmt  = sortstr ($cmtkeys);
    $sreal = sortstr ($GLOBAL_ACCEL . $realkeys);
    my @dups = dups($sreal);
    if (@dups) {
        $ok = whinge("$panel: accelerator clash(es): ",
                     join(", ", @dups), "\n") && $ok;
    }
    if ($scmt ne $sreal) {
        $ok = whinge("$panel: comment doesn't match reality ",
                     "([$GLOBAL_ACCEL] $realkeys)\n") && $ok;
    }
    if ($ok && exists($opts{v})) {
        mumble("$panel: ok\n");
    }
}

getopts("qvf:", \%opts);
my $windlg_c_name = "windlg.c";
$windlg_c_name = $opts{f} if exists($opts{f});

open WINDLG, "<$windlg_c_name";

# Grotty ad-hoc parser (tm) state
my $in_ctrl_fn = 0;
my $seen_ctrl_fn = 0;
my $panel;
my $cmt_accel;
my $real_accel;

while (<WINDLG>) {
    chomp;
    if (!$in_ctrl_fn) {

        # Look for the start of the function we're interested in.
        if (m/create_controls\s*\(.*\)\s*$/) {
            $in_ctrl_fn = 1;
            $seen_ctrl_fn = 1;
            $panel = undef;
            next;
        }

    } else {

        if (m/^}\s*$/) {
            # We've run out of function. (Probably.)
            # We should process any pending panel.
            if (defined($panel)) {
                process_panel($panel, $cmt_accel, $real_accel);
            }
            $in_ctrl_fn = 0;
            last;
        }
        if (m/^\s*if\s*\(panel\s*==\s*(\w+)panelstart\)/) {
            # New panel. Now seems like a good time to process the previous
            # one (if any).
            process_panel ($panel, $cmt_accel, $real_accel)
                if defined($panel);
            $panel = $1;
            $cmt_accel = $real_accel = "";
            next;
        }

        next unless defined($panel);

        # Some nasty hacks to get round the conditionalised stuff
        # in the Session panel. This is probably the bit most likely
        # to break.
        if ($panel eq "session") {
            my $munch;
            if (m/if\s*\(backends\[\w+\].backend\s*==\s*NULL\)/) {
                do { $munch = <WINDLG> } until ($munch =~ m/}\s*else\s*{/);
            } elsif (m/^#ifdef\s+FWHACK/) {
                do { $munch = <WINDLG> } until ($munch =~ m/^#else/);
            }
        }

	# Hack: winctrls.c:prefslist() has hard-coded "&Up" and "&Down"
	# buttons. Take this into account.
	if (m/\bprefslist *\(/) {
	    $real_accel .= "ud";
	}

        # Look for accelerator comment.
        if (m#/\* .* Accelerators used: (.*) \*/#) {
            die "aiee, multiple comments in panel" if ($cmt_accel);
            $cmt_accel = lc $1;
            $cmt_accel =~ tr/[] //d;    # strip ws etc
            next;
        }

        # Now try to find double-quoted strings.
        {
            my $line = $ARG;
            # Opening quote.
            while ($line =~ m/"/) {
                $line = $POSTMATCH;
                my $str = $line;
                # Be paranoid about \", since it does get used.
                while ($line =~ m/(?:(\\)?"|(&)(.))/) {
                    $line = $POSTMATCH;
                    if (defined($2)) {
                        if ($3 ne "&") {
                            # Found an accelerator. (Probably.)
                            $real_accel .= lc($3);
                        }
                        # Otherwise, found && -- ignore.
                    } else {
                        # It's an end quote.
                        last unless defined($1);
                        # Otherwise, it's a \" quote.
                        # Yum.
                    }
                }
            }
        }
    }

}

close WINDLG;

die "That didn't look anything like windlg.c to me" if (!$seen_ctrl_fn);

exit (!$all_ok);
