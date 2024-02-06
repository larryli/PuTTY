#!/usr/bin/perl

use Getopt::Long;
use strict;
use warnings;
use FileHandle;

my $dumpchannels = 0;
my $dumpdata = 0;
my $pass_through_events = 0;
my $verbose_all;
my %verbose_packet;
GetOptions("dump-channels|c" => \$dumpchannels,
           "dump-data|d" => \$dumpdata,
           "verbose|v" => \$verbose_all,
           "full|f=s" => sub { $verbose_packet{$_[1]} = 1; },
           "events|e" => \$pass_through_events,
           "help" => sub { &usage(\*STDOUT, 0); })
    or &usage(\*STDERR, 1);

sub usage {
    my ($fh, $exitstatus) = @_;
    print $fh <<'EOF';
usage:   logparse.pl [ options ] [ input-log-file ]
options: --dump-channels, -c  dump the final state of every channel
         --dump-data, -d      save data of every channel to ch0.i, ch0.o, ...
         --full=PKT, -f PKT   print extra detail for packets of type PKT
         --verbose, -v        print extra detail for all packets if available
         --events, -e         copy Event Log messages from input log file
EOF
    exit $exitstatus;
}

my @channels = (); # ultimate channel ids are indices in this array
my %chan_by_id = (); # indexed by 'c%d' or 's%d' for client and server ids
my %globalreq = (); # indexed by 'i' or 'o'

my %packets = (
#define SSH2_MSG_DISCONNECT                       1     /* 0x1 */
    'SSH2_MSG_DISCONNECT' => sub {
        my ($direction, $seq, $data) = @_;
        my ($reason, $description, $lang) = &parse("uss", $data);
        printf "%s\n", &str($description);
    },
#define SSH2_MSG_IGNORE                           2     /* 0x2 */
    'SSH2_MSG_IGNORE' => sub {
        my ($direction, $seq, $data) = @_;
        my ($str) = &parse("s", $data);
        printf "(%d bytes)\n", length $str;
    },
#define SSH2_MSG_UNIMPLEMENTED                    3     /* 0x3 */
    'SSH2_MSG_UNIMPLEMENTED' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rseq) = &parse("u", $data);
        printf "i%d\n", $rseq;
    },
#define SSH2_MSG_DEBUG                            4     /* 0x4 */
    'SSH2_MSG_DEBUG' => sub {
        my ($direction, $seq, $data) = @_;
        my ($disp, $message, $lang) = &parse("bss", $data);
        printf "%s\n", &str($message);
    },
#define SSH2_MSG_SERVICE_REQUEST                  5     /* 0x5 */
    'SSH2_MSG_SERVICE_REQUEST' => sub {
        my ($direction, $seq, $data) = @_;
        my ($service) = &parse("s", $data);
        printf "%s\n", &str($service);
    },
#define SSH2_MSG_SERVICE_ACCEPT                   6     /* 0x6 */
    'SSH2_MSG_SERVICE_ACCEPT' => sub {
        my ($direction, $seq, $data) = @_;
        my ($service) = &parse("s", $data);
        printf "%s\n", &str($service);
    },
#define SSH2_MSG_KEXINIT                          20    /* 0x14 */
    'SSH2_MSG_KEXINIT' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_NEWKEYS                          21    /* 0x15 */
    'SSH2_MSG_NEWKEYS' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXDH_INIT                       30    /* 0x1e */
    'SSH2_MSG_KEXDH_INIT' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXDH_REPLY                      31    /* 0x1f */
    'SSH2_MSG_KEXDH_REPLY' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEX_DH_GEX_REQUEST               30    /* 0x1e */
    'SSH2_MSG_KEX_DH_GEX_REQUEST' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEX_DH_GEX_GROUP                 31    /* 0x1f */
    'SSH2_MSG_KEX_DH_GEX_GROUP' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEX_DH_GEX_INIT                  32    /* 0x20 */
    'SSH2_MSG_KEX_DH_GEX_INIT' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEX_DH_GEX_REPLY                 33    /* 0x21 */
    'SSH2_MSG_KEX_DH_GEX_REPLY' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXGSS_INIT                      30  /* 0x1e */
    'SSH2_MSG_KEXGSS_INIT' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXGSS_CONTINUE                  31  /* 0x1f */
    'SSH2_MSG_KEXGSS_CONTINUE' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXGSS_COMPLETE                  32  /* 0x20 */
    'SSH2_MSG_KEXGSS_COMPLETE' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXGSS_HOSTKEY                   33  /* 0x21 */
    'SSH2_MSG_KEXGSS_HOSTKEY' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXGSS_ERROR                     34  /* 0x22 */
    'SSH2_MSG_KEXGSS_ERROR' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXGSS_GROUPREQ                  40  /* 0x28 */
    'SSH2_MSG_KEXGSS_GROUPREQ' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXGSS_GROUP                     41  /* 0x29 */
    'SSH2_MSG_KEXGSS_GROUP' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXRSA_PUBKEY                    30    /* 0x1e */
    'SSH2_MSG_KEXRSA_PUBKEY' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXRSA_SECRET                    31    /* 0x1f */
    'SSH2_MSG_KEXRSA_SECRET' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXRSA_DONE                      32    /* 0x20 */
    'SSH2_MSG_KEXRSA_DONE' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEX_ECDH_INIT                    30    /* 0x1e */
    'SSH2_MSG_KEX_ECDH_INIT' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEX_ECDH_REPLY                   31    /* 0x1f */
    'SSH2_MSG_KEX_ECDH_REPLY' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_REQUEST                 50    /* 0x32 */
    'SSH2_MSG_USERAUTH_REQUEST' => sub {
        my ($direction, $seq, $data) = @_;
        my ($user, $service, $method) = &parse("sss", $data);
        my $out = sprintf "%s %s %s",
            &str($user), &str($service), &str($method);
        if ($method eq "publickey") {
            my ($real) = &parse("b", $data);
            $out .= " real=$real";
        } elsif ($method eq "password") {
            my ($change) = &parse("b", $data);
            $out .= " change=$change";
        }
        print "$out\n";
    },
#define SSH2_MSG_USERAUTH_FAILURE                 51    /* 0x33 */
    'SSH2_MSG_USERAUTH_FAILURE' => sub {
        my ($direction, $seq, $data) = @_;
        my ($options) = &parse("s", $data);
        printf "%s\n", &str($options);
    },
#define SSH2_MSG_USERAUTH_SUCCESS                 52    /* 0x34 */
    'SSH2_MSG_USERAUTH_SUCCESS' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_BANNER                  53    /* 0x35 */
    'SSH2_MSG_USERAUTH_BANNER' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_PK_OK                   60    /* 0x3c */
    'SSH2_MSG_USERAUTH_PK_OK' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_PASSWD_CHANGEREQ        60    /* 0x3c */
    'SSH2_MSG_USERAUTH_PASSWD_CHANGEREQ' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_INFO_REQUEST            60    /* 0x3c */
    'SSH2_MSG_USERAUTH_INFO_REQUEST' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_INFO_RESPONSE           61    /* 0x3d */
    'SSH2_MSG_USERAUTH_INFO_RESPONSE' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_GLOBAL_REQUEST                   80    /* 0x50 */
    'SSH2_MSG_GLOBAL_REQUEST' => sub {
        my ($direction, $seq, $data) = @_;
        my ($type, $wantreply) = &parse("sb", $data);
        printf "%s (%s)", $type, $wantreply eq "yes" ? "reply" : "noreply";
        my $request = [$seq, $type];
        push @{$globalreq{$direction}}, $request if $wantreply eq "yes";
        if ($type eq "tcpip-forward" or $type eq "cancel-tcpip-forward") {
            my ($addr, $port) = &parse("su", $data);
            printf " %s:%s", $addr, $port;
            push @$request, $port;
        }
        print "\n";
    },
#define SSH2_MSG_REQUEST_SUCCESS                  81    /* 0x51 */
    'SSH2_MSG_REQUEST_SUCCESS' => sub {
        my ($direction, $seq, $data) = @_;
        my $otherdir = ($direction eq "i" ? "o" : "i");
        my $request = shift @{$globalreq{$otherdir}};
        if (defined $request) {
            printf "to %s", $request->[0];
            if ($request->[1] eq "tcpip-forward" and $request->[2] == 0) {
                my ($port) = &parse("u", $data);
                printf " port=%s", $port;
            }
        } else {
            print "(spurious?)";
        }
        print "\n";
    },
#define SSH2_MSG_REQUEST_FAILURE                  82    /* 0x52 */
    'SSH2_MSG_REQUEST_FAILURE' => sub {
        my ($direction, $seq, $data) = @_;
        my $otherdir = ($direction eq "i" ? "o" : "i");
        my $request = shift @{$globalreq{$otherdir}};
        if (defined $request) {
            printf "to %s", $request->[0];
        } else {
            print "(spurious?)";
        }
        print "\n";
    },
#define SSH2_MSG_CHANNEL_OPEN                     90    /* 0x5a */
    'SSH2_MSG_CHANNEL_OPEN' => sub {
        my ($direction, $seq, $data) = @_;
        my ($type, $sid, $winsize, $packet) = &parse("suuu", $data);
        # CHANNEL_OPEN tells the other side the _sender's_ id for the
        # channel, so this choice between "s" and "c" prefixes is
        # opposite to every other message in the protocol, which all
        # quote the _recipient's_ id of the channel.
        $sid = ($direction eq "i" ? "s" : "c") . $sid;
        my $chan = {'id'=>$sid, 'state'=>'halfopen',
                    'i'=>{'win'=>0, 'seq'=>0},
                    'o'=>{'win'=>0, 'seq'=>0}};
        $chan->{$direction}{'win'} = $winsize;
        push @channels, $chan;
        my $index = $#channels;
        $chan_by_id{$sid} = $index;
        printf "ch%d (%s) %s (--%d)", $index, $chan->{'id'}, $type,
            $chan->{$direction}{'win'};
        if ($type eq "x11") {
            my ($addr, $port) = &parse("su", $data);
            printf " from %s:%s", $addr, $port;
        } elsif ($type eq "forwarded-tcpip") {
            my ($saddr, $sport, $paddr, $pport) = &parse("susu", $data);
            printf " to %s:%s from %s:%s", $saddr, $sport, $paddr, $pport;
        } elsif ($type eq "direct-tcpip") {
            my ($daddr, $dport, $saddr, $sport) = &parse("susu", $data);
            printf " to %s:%s from %s:%s", $daddr, $dport, $saddr, $sport;
        }
        print "\n";
    },
#define SSH2_MSG_CHANNEL_OPEN_CONFIRMATION        91    /* 0x5b */
    'SSH2_MSG_CHANNEL_OPEN_CONFIRMATION' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $sid, $winsize, $packet) = &parse("uuuu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        if (!defined $index) {
            printf "UNKNOWN_CHANNEL (%s) (--%d)\n", $rid, $winsize;
            return;
        }
        $sid = ($direction eq "i" ? "s" : "c") . $sid;
        $chan_by_id{$sid} = $index;
        my $chan = $channels[$index];
        $chan->{'id'} = ($direction eq "i" ? "$rid/$sid" : "$sid/$rid");
        $chan->{'state'} = 'open';
        $chan->{$direction}{'win'} = $winsize;
        printf "ch%d (%s) (--%d)\n", $index, $chan->{'id'},
            $chan->{$direction}{'win'};
    },
#define SSH2_MSG_CHANNEL_OPEN_FAILURE             92    /* 0x5c */
    'SSH2_MSG_CHANNEL_OPEN_FAILURE' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $reason, $desc, $lang) = &parse("uuss", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        if (!defined $index) {
            printf "UNKNOWN_CHANNEL (%s) %s\n", $rid, &str($reason);
            return;
        }
        my $chan = $channels[$index];
        $chan->{'state'} = 'rejected';
        printf "ch%d (%s) %s\n", $index, $chan->{'id'}, &str($reason);
    },
#define SSH2_MSG_CHANNEL_WINDOW_ADJUST            93    /* 0x5d */
    'SSH2_MSG_CHANNEL_WINDOW_ADJUST' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $bytes) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        if (!defined $index) {
            printf "UNKNOWN_CHANNEL (%s) +%d\n", $rid, $bytes;
            return;
        }
        my $chan = $channels[$index];
        $chan->{$direction}{'win'} += $bytes;
        printf "ch%d (%s) +%d (--%d)\n", $index, $chan->{'id'}, $bytes,
            $chan->{$direction}{'win'};
    },
#define SSH2_MSG_CHANNEL_DATA                     94    /* 0x5e */
    'SSH2_MSG_CHANNEL_DATA' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $bytes) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        if (!defined $index) {
            printf "UNKNOWN_CHANNEL (%s), %s bytes\n", $rid, $bytes;
            return;
        }
        my $chan = $channels[$index];
        $chan->{$direction}{'seq'} += $bytes;
        printf "ch%d (%s), %s bytes (%d--%d)\n", $index, $chan->{'id'}, $bytes,
            $chan->{$direction}{'seq'}-$bytes, $chan->{$direction}{'seq'};
        my @realdata = splice @$data, 0, $bytes;
        if ($dumpdata) {
            my $filekey = $direction . "file";
            if (!defined $chan->{$filekey}) {
                my $filename = sprintf "ch%d.%s", $index, $direction;
                $chan->{$filekey} = FileHandle->new(">$filename");
                if (!defined $chan->{$filekey}) {
                    die "$filename: $!\n";
                }
            }
            die "channel data not present in $seq\n" if @realdata < $bytes;
            my $rawdata = pack "C*", @realdata;
            my $fh = $chan->{$filekey};
            print $fh $rawdata;
        }
        if (@realdata == $bytes and defined $chan->{$direction."data"}) {
            my $rawdata = pack "C*", @realdata;
            $chan->{$direction."data"}->($chan, $index, $direction, $rawdata);
        }
    },
#define SSH2_MSG_CHANNEL_EXTENDED_DATA            95    /* 0x5f */
    'SSH2_MSG_CHANNEL_EXTENDED_DATA' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $type, $bytes) = &parse("uuu", $data);
        if ($type == 1) {
            $type = "SSH_EXTENDED_DATA_STDERR";
        }
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        if (!defined $index) {
            printf "UNKNOWN_CHANNEL (%s), type %s, %s bytes\n", $rid,
                $type, $bytes;
            return;
        }
        my $chan = $channels[$index];
        $chan->{$direction}{'seq'} += $bytes;
        printf "ch%d (%s), type %s, %s bytes (%d--%d)\n", $index,$chan->{'id'},
            $type, $bytes, $chan->{$direction}{'seq'}-$bytes,
            $chan->{$direction}{'seq'};
        my @realdata = splice @$data, 0, $bytes;
        if ($dumpdata) {
            # We treat EXTENDED_DATA as equivalent to DATA, for the
            # moment. It's not clear what else would be a better thing
            # to do with it, and this at least is the Right Answer if
            # the data is going to a terminal and the aim is to debug
            # the terminal emulator.
            my $filekey = $direction . "file";
            if (!defined $chan->{$filekey}) {
                my $filename = sprintf "ch%d.%s", $index, $direction;
                $chan->{$filekey} = FileHandle->new(">$filename");
                if (!defined $chan->{$filekey}) {
                    die "$filename: $!\n";
                }
            }
            die "channel data not present in $seq\n" if @realdata < $bytes;
            my $rawdata = pack "C*", @realdata;
            my $fh = $chan->{$filekey};
            print $fh $rawdata;
        }
        if (@realdata == $bytes and defined $chan->{$direction."data"}) {
            my $rawdata = pack "C*", @realdata;
            $chan->{$direction."data"}->($chan, $index, $direction, $rawdata);
        }
    },
#define SSH2_MSG_CHANNEL_EOF                      96    /* 0x60 */
    'SSH2_MSG_CHANNEL_EOF' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        if (!defined $index) {
            printf "UNKNOWN_CHANNEL (%s)\n", $rid;
            return;
        }
        my $chan = $channels[$index];
        printf "ch%d (%s)\n", $index, $chan->{'id'};
    },
#define SSH2_MSG_CHANNEL_CLOSE                    97    /* 0x61 */
    'SSH2_MSG_CHANNEL_CLOSE' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        if (!defined $index) {
            printf "UNKNOWN_CHANNEL (%s)\n", $rid;
            return;
        }
        my $chan = $channels[$index];
        $chan->{'state'} = ($chan->{'state'} eq "open" ? "halfclosed" :
                            $chan->{'state'} eq "halfclosed" ? "closed" :
                            "confused");
        if ($chan->{'state'} eq "closed") {
            $chan->{'ifile'}->close if defined $chan->{'ifile'};
            $chan->{'ofile'}->close if defined $chan->{'ofile'};
        }
        printf "ch%d (%s)\n", $index, $chan->{'id'};
    },
#define SSH2_MSG_CHANNEL_REQUEST                  98    /* 0x62 */
    'SSH2_MSG_CHANNEL_REQUEST' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $type, $wantreply) = &parse("usb", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        my $chan;
        if (!defined $index) {
            printf "UNKNOWN_CHANNEL (%s) %s (%s)", $rid,
                $type, $wantreply eq "yes" ? "reply" : "noreply";
        } else {
            $chan = $channels[$index];
            printf "ch%d (%s) %s (%s)", $index, $chan->{'id'},
                $type, $wantreply eq "yes" ? "reply" : "noreply";
            push @{$chan->{'requests_'.$direction}}, [$seq, $type]
                if $wantreply eq "yes";
        }
        if ($type eq "pty-req") {
            my ($term, $w, $h, $pw, $ph, $modes) = &parse("suuuus", $data);
            printf " %s %sx%s", &str($term), $w, $h;
        } elsif ($type eq "x11-req") {
            my ($single, $xprot, $xcookie, $xscreen) = &parse("bssu", $data);
            print " one-off" if $single eq "yes";
            printf " %s :%s", $xprot, $xscreen;
        } elsif ($type eq "exec") {
            my ($command) = &parse("s", $data);
            printf " %s", &str($command);
        } elsif ($type eq "subsystem") {
            my ($subsys) = &parse("s", $data);
            printf " %s", &str($subsys);
            if ($subsys eq "sftp") {
                &sftp_setup($index);
            }
        } elsif ($type eq "window-change") {
            my ($w, $h, $pw, $ph) = &parse("uuuu", $data);
            printf " %sx%s", $w, $h;
        } elsif ($type eq "xon-xoff") {
            my ($can) = &parse("b", $data);
            printf " %s", $can;
        } elsif ($type eq "signal") {
            my ($sig) = &parse("s", $data);
            printf " %s", &str($sig);
        } elsif ($type eq "exit-status") {
            my ($status) = &parse("u", $data);
            printf " %s", $status;
        } elsif ($type eq "exit-signal") {
            my ($sig, $core, $error, $lang) = &parse("sbss", $data);
            printf " %s", &str($sig);
            print " (core dumped)" if $core eq "yes";
        }
        print "\n";
    },
#define SSH2_MSG_CHANNEL_SUCCESS                  99    /* 0x63 */
    'SSH2_MSG_CHANNEL_SUCCESS' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        if (!defined $index) {
            printf "UNKNOWN_CHANNEL (%s)\n", $rid;
            return;
        }
        my $chan = $channels[$index];
        printf "ch%d (%s)", $index, $chan->{'id'};
        my $otherdir = ($direction eq "i" ? "o" : "i");
        my $request = shift @{$chan->{'requests_' . $otherdir}};
        if (defined $request) {
            printf " to %s", $request->[0];
        } else {
            print " (spurious?)";
        }
        print "\n";
    },
#define SSH2_MSG_CHANNEL_FAILURE                  100   /* 0x64 */
    'SSH2_MSG_CHANNEL_FAILURE' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        if (!defined $index) {
            printf "UNKNOWN_CHANNEL (%s)\n", $rid;
            return;
        }
        my $chan = $channels[$index];
        printf "ch%d (%s)", $index, $chan->{'id'};
        my $otherdir = ($direction eq "i" ? "o" : "i");
        my $request = shift @{$chan->{'requests_' . $otherdir}};
        if (defined $request) {
            printf " to %s", $request->[0];
        } else {
            print " (spurious?)";
        }
        print "\n";
    },
#define SSH2_MSG_USERAUTH_GSSAPI_RESPONSE               60
    'SSH2_MSG_USERAUTH_GSSAPI_RESPONSE' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_GSSAPI_TOKEN                  61
    'SSH2_MSG_USERAUTH_GSSAPI_TOKEN' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_GSSAPI_EXCHANGE_COMPLETE      63
    'SSH2_MSG_USERAUTH_GSSAPI_EXCHANGE_COMPLETE' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_GSSAPI_ERROR                  64
    'SSH2_MSG_USERAUTH_GSSAPI_ERROR' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_GSSAPI_ERRTOK                 65
    'SSH2_MSG_USERAUTH_GSSAPI_ERRTOK' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_GSSAPI_MIC                    66
    'SSH2_MSG_USERAUTH_GSSAPI_MIC' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
);

our %disc_reasons = (
    1 => "SSH_DISCONNECT_HOST_NOT_ALLOWED_TO_CONNECT",
    2 => "SSH_DISCONNECT_PROTOCOL_ERROR",
    3 => "SSH_DISCONNECT_KEY_EXCHANGE_FAILED",
    4 => "SSH_DISCONNECT_RESERVED",
    5 => "SSH_DISCONNECT_MAC_ERROR",
    6 => "SSH_DISCONNECT_COMPRESSION_ERROR",
    7 => "SSH_DISCONNECT_SERVICE_NOT_AVAILABLE",
    8 => "SSH_DISCONNECT_PROTOCOL_VERSION_NOT_SUPPORTED",
    9 => "SSH_DISCONNECT_HOST_KEY_NOT_VERIFIABLE",
    10 => "SSH_DISCONNECT_CONNECTION_LOST",
    11 => "SSH_DISCONNECT_BY_APPLICATION",
    12 => "SSH_DISCONNECT_TOO_MANY_CONNECTIONS",
    13 => "SSH_DISCONNECT_AUTH_CANCELLED_BY_USER",
    14 => "SSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE",
    15 => "SSH_DISCONNECT_ILLEGAL_USER_NAME",
);

my %verbose_packet_dump_functions = (
    'SSH2_MSG_KEXINIT' => sub {
        my ($data) = @_;
        my ($cookie0, $cookie1, $cookie2, $cookie3,
            $kex, $hostkey, $cscipher, $sccipher, $csmac, $scmac,
            $cscompress, $sccompress, $cslang, $sclang, $guess, $reserved) =
                &parse("uuuussssssssssbu", $data);
        printf("  cookie: %08x%08x%08x%08x\n",
               $cookie0, $cookie1, $cookie2, $cookie3);
        my $print_namelist = sub {
            my @names = split /,/, $_[1];
            printf "  %s: name-list with %d items%s\n", $_[0], (scalar @names),
            join "", map { "\n    $_" } @names;
        };
        $print_namelist->("kex", $kex);
        $print_namelist->("host key", $hostkey);
        $print_namelist->("client->server cipher", $cscipher);
        $print_namelist->("server->client cipher", $sccipher);
        $print_namelist->("client->server MAC", $csmac);
        $print_namelist->("server->client MAC", $scmac);
        $print_namelist->("client->server compression", $cscompress);
        $print_namelist->("server->client compression", $sccompress);
        $print_namelist->("client->server language", $cslang);
        $print_namelist->("server->client language", $sclang);
        printf "  first kex packet follows: %s\n", $guess;
        printf "  reserved field: %#x\n", $reserved;
    },
    'SSH2_MSG_KEXDH_INIT' => sub {
        my ($data) = @_;
        my ($e) = &parse("m", $data);
        printf "  e: %s\n", $e;
    },
    'SSH2_MSG_KEX_DH_GEX_REQUEST' => sub {
        my ($data) = @_;
        my ($min, $pref, $max) = &parse("uuu", $data);
        printf "  min bits: %d\n", $min;
        printf "  preferred bits: %d\n", $pref;
        printf "  max bits: %d\n", $max;
    },
    'SSH2_MSG_KEX_DH_GEX_GROUP' => sub {
        my ($data) = @_;
        my ($p, $g) = &parse("mm", $data);
        printf "  p: %s\n", $p;
        printf "  g: %s\n", $g;
    },
    'SSH2_MSG_KEX_DH_GEX_INIT' => sub {
        my ($data) = @_;
        my ($e) = &parse("m", $data);
        printf "  e: %s\n", $e;
    },
    'SSH2_MSG_KEX_ECDH_INIT' => sub {
        my ($data) = @_;
        my ($cpv) = &parse("s", $data);
        # Public values in ECDH depend for their interpretation on the
        # selected curve, and this script doesn't cross-analyse the
        # two KEXINIT packets to independently figure out what that
        # curve is. So the best we can do is just dump the raw data.
        printf "  client public value: %s\n", (unpack "H*", $cpv);
    },
    'SSH2_MSG_KEXDH_REPLY' => sub {
        my ($data) = @_;
        my ($hostkeyblob, $f, $sigblob) = &parse("sms", $data);
        my ($hktype, @hostkey) = &parse_public_key($hostkeyblob);
        printf "  host key: %s\n", $hktype;
        while (@hostkey) {
            my ($key, $value) = splice @hostkey, 0, 2;
            printf "    $key: $value\n";
        }
        printf "  f: %s\n", $f;
        printf "  signature:\n";
        my @signature = &parse_signature($sigblob, $hktype);
        while (@signature) {
            my ($key, $value) = splice @signature, 0, 2;
            printf "    $key: $value\n";
        }
    },
    'SSH2_MSG_KEX_DH_GEX_REPLY' => sub {
        my ($data) = @_;
        my ($hostkeyblob, $f, $sigblob) = &parse("sms", $data);
        my ($hktype, @hostkey) = &parse_public_key($hostkeyblob);
        printf "  host key: %s\n", $hktype;
        while (@hostkey) {
            my ($key, $value) = splice @hostkey, 0, 2;
            printf "    $key: $value\n";
        }
        printf "  f: %s\n", $f;
        printf "  signature:\n";
        my @signature = &parse_signature($sigblob, $hktype);
        while (@signature) {
            my ($key, $value) = splice @signature, 0, 2;
            printf "    $key: $value\n";
        }
    },
    'SSH2_MSG_KEX_ECDH_REPLY' => sub {
        my ($data) = @_;
        my ($hostkeyblob, $spv, $sigblob) = &parse("sss", $data);
        my ($hktype, @hostkey) = &parse_public_key($hostkeyblob);
        printf "  host key: %s\n", $hktype;
        while (@hostkey) {
            my ($key, $value) = splice @hostkey, 0, 2;
            printf "    $key: $value\n";
        }
        printf "  server public value: %s\n", (unpack "H*", $spv);
        printf "  signature:\n";
        my @signature = &parse_signature($sigblob, $hktype);
        while (@signature) {
            my ($key, $value) = splice @signature, 0, 2;
            printf "    $key: $value\n";
        }
    },
    'SSH2_MSG_NEWKEYS' => sub {},
    'SSH2_MSG_SERVICE_REQUEST' => sub {
        my ($data) = @_;
        my ($servname) = &parse("s", $data);
        printf "  service name: %s\n", $servname;
    },
    'SSH2_MSG_SERVICE_ACCEPT' => sub {
        my ($data) = @_;
        my ($servname) = &parse("s", $data);
        printf "  service name: %s\n", $servname;
    },
    'SSH2_MSG_DISCONNECT' => sub {
        my ($data) = @_;
        my ($reason, $desc, $lang) = &parse("uss", $data);
        printf("  reason code: %d%s\n", $reason,
               defined $disc_reasons{$reason} ?
               " ($disc_reasons{$reason})" : "" );
        printf "  description: '%s'\n", $desc;
        printf "  language tag: '%s'\n", $lang;
    },
    'SSH2_MSG_DEBUG' => sub {
        my ($data) = @_;
        my ($display, $desc, $lang) = &parse("bss", $data);
        printf "  always display: %s\n", $display;
        printf "  description: '%s'\n", $desc;
        printf "  language tag: '%s'\n", $lang;
    },
    'SSH2_MSG_IGNORE' => sub {
        my ($data) = @_;
        my ($payload) = &parse("s", $data);
        printf "  data: %s\n", unpack "H*", $payload;
    },
    'SSH2_MSG_UNIMPLEMENTED' => sub {
        my ($data) = @_;
        my ($seq) = &parse("u", $data);
        printf "  sequence number: %d\n", $seq;
    },
    'SSH2_MSG_KEXGSS_INIT' => sub {
        my ($data) = @_;
        my ($token, $e) = &parse("sm", $data);
        printf "  output token: %s\n", unpack "H*", $token;
        printf "  e: %s\n", $e;
    },
    'SSH2_MSG_KEXGSS_CONTINUE' => sub {
        my ($data) = @_;
        my ($token) = &parse("s", $data);
        printf "  output token: %s\n", unpack "H*", $token;
    },
    'SSH2_MSG_KEXGSS_COMPLETE' => sub {
        my ($data) = @_;
        my ($f, $permsgtoken, $got_output) = &parse("msb", $data);
        printf "  f: %s\n", $f;
        printf "  per-message token: %s\n", unpack "H*", $permsgtoken;
        printf "  output token present: %s\n", $got_output;
        if ($got_output eq "yes") {
            my ($token) = &parse("s", $data);
            printf "  output token: %s\n", unpack "H*", $token;
        }
    },
    'SSH2_MSG_KEXGSS_HOSTKEY' => sub {
        my ($data) = @_;
        my ($hostkey) = &parse("s", $data);
        printf "  host key: %s\n", unpack "H*", $hostkey;
    },
    'SSH2_MSG_KEXGSS_ERROR' => sub {
        my ($data) = @_;
        my ($maj, $min, $msg, $lang) = &parse("uuss", $data);
        printf "  major status: %d\n", $maj;
        printf "  minor status: %d\n", $min;
        printf "  message: '%s'\n", $msg;
        printf "  language tag: '%s'\n", $lang;
    },
    'SSH2_MSG_KEXGSS_GROUPREQ' => sub {
        my ($data) = @_;
        my ($min, $pref, $max) = &parse("uuu", $data);
        printf "  min bits: %d\n", $min;
        printf "  preferred bits: %d\n", $pref;
        printf "  max bits: %d\n", $max;
    },
    'SSH2_MSG_KEXGSS_GROUP' => sub {
        my ($data) = @_;
        my ($p, $g) = &parse("mm", $data);
        printf "  p: %s\n", $p;
        printf "  g: %s\n", $g;
    },
);

my %sftp_packets = (
#define SSH_FXP_INIT                              1     /* 0x1 */
    0x1 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($ver) = &parse("u", $data);
        printf "SSH_FXP_INIT %d\n", $ver;
    },
#define SSH_FXP_VERSION                           2     /* 0x2 */
    0x2 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($ver) = &parse("u", $data);
        printf "SSH_FXP_VERSION %d\n", $ver;
    },
#define SSH_FXP_OPEN                              3     /* 0x3 */
    0x3 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $path, $pflags) = &parse("usu", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_OPEN");
        printf " \"%s\" ", $path;
        if ($pflags eq 0) {
            print "0";
        } else {
            my $sep = "";
            if ($pflags & 1) { $pflags ^= 1; print "${sep}READ"; $sep = "|"; }
            if ($pflags & 2) { $pflags ^= 2; print "${sep}WRITE"; $sep = "|"; }
            if ($pflags & 4) { $pflags ^= 4; print "${sep}APPEND"; $sep = "|"; }
            if ($pflags & 8) { $pflags ^= 8; print "${sep}CREAT"; $sep = "|"; }
            if ($pflags & 16) { $pflags ^= 16; print "${sep}TRUNC"; $sep = "|"; }
            if ($pflags & 32) { $pflags ^= 32; print "${sep}EXCL"; $sep = "|"; }
            if ($pflags) { print "${sep}${pflags}"; }
        }
        print "\n";
    },
#define SSH_FXP_CLOSE                             4     /* 0x4 */
    0x4 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $handle) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_CLOSE");
        printf " \"%s\"", &stringescape($handle);
        print "\n";
    },
#define SSH_FXP_READ                              5     /* 0x5 */
    0x5 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $handle, $offset, $len) = &parse("usUu", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_READ");
        printf " \"%s\" %d %d", &stringescape($handle), $offset, $len;
        print "\n";
    },
#define SSH_FXP_WRITE                             6     /* 0x6 */
    0x6 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $handle, $offset, $wdata) = &parse("usUs", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_WRITE");
        printf " \"%s\" %d [%d bytes]", &stringescape($handle), $offset, length $wdata;
        print "\n";
    },
#define SSH_FXP_LSTAT                             7     /* 0x7 */
    0x7 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $path) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_LSTAT");
        printf " \"%s\"", $path;
        print "\n";
    },
#define SSH_FXP_FSTAT                             8     /* 0x8 */
    0x8 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $handle) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_FSTAT");
        printf " \"%s\"", &stringescape($handle);
        print "\n";
    },
#define SSH_FXP_SETSTAT                           9     /* 0x9 */
    0x9 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $path) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_SETSTAT");
        my $attrs = &sftp_parse_attrs($data);
        printf " \"%s\" %s", $path, $attrs;
        print "\n";
    },
#define SSH_FXP_FSETSTAT                          10    /* 0xa */
    0xa => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $handle) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_FSETSTAT");
        my $attrs = &sftp_parse_attrs($data);
        printf " \"%s\" %s", &stringescape($handle), $attrs;
        print "\n";
    },
#define SSH_FXP_OPENDIR                           11    /* 0xb */
    0xb => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $path) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_OPENDIR");
        printf " \"%s\"", $path;
        print "\n";
    },
#define SSH_FXP_READDIR                           12    /* 0xc */
    0xc => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $handle) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_READDIR");
        printf " \"%s\"", &stringescape($handle);
        print "\n";
    },
#define SSH_FXP_REMOVE                            13    /* 0xd */
    0xd => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $path) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_REMOVE");
        printf " \"%s\"", $path;
        print "\n";
    },
#define SSH_FXP_MKDIR                             14    /* 0xe */
    0xe => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $path) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_MKDIR");
        printf " \"%s\"", $path;
        print "\n";
    },
#define SSH_FXP_RMDIR                             15    /* 0xf */
    0xf => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $path) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_RMDIR");
        printf " \"%s\"", $path;
        print "\n";
    },
#define SSH_FXP_REALPATH                          16    /* 0x10 */
    0x10 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $path) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_REALPATH");
        printf " \"%s\"", $path;
        print "\n";
    },
#define SSH_FXP_STAT                              17    /* 0x11 */
    0x11 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $path) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_STAT");
        printf " \"%s\"", $path;
        print "\n";
    },
#define SSH_FXP_RENAME                            18    /* 0x12 */
    0x12 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $srcpath, $dstpath) = &parse("uss", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_RENAME");
        printf " \"%s\" \"%s\"", $srcpath, $dstpath;
        print "\n";
    },
#define SSH_FXP_STATUS                            101   /* 0x65 */
    0x65 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $status) = &parse("uu", $data);
        &sftp_logreply($chan, $direction, $reqid, $id, "SSH_FXP_STATUS");
        print " ";
        if ($status eq "0") { print "SSH_FX_OK"; }
        elsif ($status eq "1") { print "SSH_FX_EOF"; }
        elsif ($status eq "2") { print "SSH_FX_NO_SUCH_FILE"; }
        elsif ($status eq "3") { print "SSH_FX_PERMISSION_DENIED"; }
        elsif ($status eq "4") { print "SSH_FX_FAILURE"; }
        elsif ($status eq "5") { print "SSH_FX_BAD_MESSAGE"; }
        elsif ($status eq "6") { print "SSH_FX_NO_CONNECTION"; }
        elsif ($status eq "7") { print "SSH_FX_CONNECTION_LOST"; }
        elsif ($status eq "8") { print "SSH_FX_OP_UNSUPPORTED"; }
        else { printf "[unknown status %d]", $status; }
        print "\n";
    },
#define SSH_FXP_HANDLE                            102   /* 0x66 */
    0x66 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $handle) = &parse("us", $data);
        &sftp_logreply($chan, $direction, $reqid, $id, "SSH_FXP_HANDLE");
        printf " \"%s\"", &stringescape($handle);
        print "\n";
    },
#define SSH_FXP_DATA                              103   /* 0x67 */
    0x67 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $retdata) = &parse("us", $data);
        &sftp_logreply($chan, $direction, $reqid, $id, "SSH_FXP_DATA");
        printf " [%d bytes]", length $retdata;
        print "\n";
    },
#define SSH_FXP_NAME                              104   /* 0x68 */
    0x68 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $count) = &parse("uu", $data);
        &sftp_logreply($chan, $direction, $reqid, $id, "SSH_FXP_NAME");
        for my $i (1..$count) {
            my ($name, $longname) = &parse("ss", $data);
            my $attrs = &sftp_parse_attrs($data);
            print " [name=\"$name\", longname=\"$longname\", attrs=$attrs]";
        }
        print "\n";
    },
#define SSH_FXP_ATTRS                             105   /* 0x69 */
    0x69 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid) = &parse("u", $data);
        &sftp_logreply($chan, $direction, $reqid, $id, "SSH_FXP_ATTRS");
        my $attrs = &sftp_parse_attrs($data);
        printf " %s", $attrs;
        print "\n";
    },
#define SSH_FXP_EXTENDED                          200   /* 0xc8 */
    0xc8 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid, $type) = &parse("us", $data);
        &sftp_logreq($chan, $direction, $reqid, $id, "SSH_FXP_EXTENDED");
        printf " \"%s\"", $type;
        print "\n";
    },
#define SSH_FXP_EXTENDED_REPLY                    201   /* 0xc9 */
    0xc9 => sub {
        my ($chan, $index, $direction, $id, $data) = @_;
        my ($reqid) = &parse("u", $data);
        print "\n";
        &sftp_logreply($chan, $direction, $reqid,$id,"SSH_FXP_EXTENDED_REPLY");
    },
);

for my $type (keys %verbose_packet) {
    if (!defined $verbose_packet_dump_functions{$type}) {
        die "no verbose dump available for packet type $type\n";
    }
}

my ($direction, $seq, $ourseq, $type, $data, $recording);
my %ourseqs = ('i'=>0, 'o'=>0);

$recording = 0;
while (<<>>) {
    if ($recording) {
        if (/^  [0-9a-fA-F]{8}  ((?:[0-9a-fA-F]{2} )*[0-9a-fA-F]{2})/) {
            push @$data, map { $_ eq "XX" ? -1 : hex $_ } split / /, $1;
        } else {
            $recording = 0;
            my $fullseq = "$direction$ourseq";
            print "$fullseq: $type ";

            my ($verbose_dump, $verbose_data) = undef;
            if (defined $verbose_packet_dump_functions{$type} &&
                ($verbose_all || defined $verbose_packet{$type})) {
                $verbose_dump = $verbose_packet_dump_functions{$type};
                $verbose_data = [ @$data ];
            }

            if (defined $packets{$type}) {
                $packets{$type}->($direction, $fullseq, $data);
            } else {
                printf "raw %s\n", join "", map { sprintf "%02x", $_ } @$data;
            }
            if (defined $verbose_dump) {
                $verbose_dump->($verbose_data);
                if (@$verbose_data) {
                    printf("  trailing bytes: %s\n",
                           unpack "H*", pack "C*", @$verbose_data);
                }
            }
        }
    }
    if (/^(Incoming|Outgoing) packet #0x([0-9a-fA-F]+), type \d+ \/ 0x[0-9a-fA-F]+ \((.*)\)/) {
        $direction = ($1 eq "Incoming" ? 'i' : 'o');
        # $seq is the sequence number quoted in the log file. $ourseq
        # is our own count of the sequence number, which differs in
        # that it shouldn't wrap at 2^32, should anyone manage to run
        # this script over such a huge log file.
        $seq = hex $2;
        $ourseq = $ourseqs{$direction}++;
        $type = $3;
        $data = [];
        $recording = 1;
    }
    if ($pass_through_events && m/^Event Log: ([^\n]*)$/) {
        printf "event: $1\n";
    }
}

if ($dumpchannels) {
    my %stateorder = ('closed'=>0, 'rejected'=>1,
                      'halfclosed'=>2, 'open'=>3, 'halfopen'=>4);
    for my $index (0..$#channels) {
        my $chan = $channels[$index];
        my $so = $stateorder{$chan->{'state'}};
        $so = 1000 unless defined $so; # any state I've missed above comes last
        $chan->{'index'} = sprintf "ch%d", $index;
        $chan->{'order'} = sprintf "%08d %08d", $so, $index;
    }
    my @sortedchannels = sort { $a->{'order'} cmp $b->{'order'} } @channels;
    for my $chan (@sortedchannels) {
        printf "%s (%s): %s\n", $chan->{'index'}, $chan->{'id'}, $chan->{'state'};
    }
}

sub format_unsigned_hex_integer {
    my $abs = join "", map { sprintf "%02x", $_ } @_;
    $abs =~ s!^0*!!g;
    $abs = "0" if $abs eq "";
    return "0x" . $abs;
}

sub parseone {
    my ($type, $data) = @_;
    if ($type eq "u") { # uint32
        my @bytes = splice @$data, 0, 4;
        return "<missing>" if @bytes < 4 or grep { $_<0 } @bytes;
        return unpack "N", pack "C*", @bytes;
    } elsif ($type eq "U") { # uint64
        my @bytes = splice @$data, 0, 8;
        return "<missing>" if @bytes < 8 or grep { $_<0 } @bytes;
        my @words = unpack "NN", pack "C*", @bytes;
        return ($words[0] << 32) + $words[1];
    } elsif ($type eq "b") { # boolean
        my $byte = shift @$data;
        return "<missing>" if !defined $byte or $byte < 0;
        return $byte ? "yes" : "no";
    } elsif ($type eq "B") { # byte
        my $byte = shift @$data;
        return "<missing>" if !defined $byte or $byte < 0;
        return $byte;
    } elsif ($type eq "s" or $type eq "m") { # string, mpint
        my @bytes = splice @$data, 0, 4;
        return "<missing>" if @bytes < 4 or grep { $_<0 } @bytes;
        my $len = unpack "N", pack "C*", @bytes;
        @bytes = splice @$data, 0, $len;
        return "<missing>" if @bytes < $len or grep { $_<0 } @bytes;
        if ($type eq "m") {
            my $str = "";
            if ($bytes[0] >= 128) {
                # Take two's complement.
                @bytes = map { 0xFF ^ $_ } @bytes;
                for my $i (reverse 0..$#bytes) {
                    if ($bytes[$i] < 0xFF) {
                        $bytes[$i]++;
                        last;
                    } else {
                        $bytes[$i] = 0;
                    }
                }
                $str = "-";
            }
            $str .= &format_unsigned_hex_integer(@bytes);
            return $str;
        } else {
            return pack "C*", @bytes;
        }
    }
}

sub parse {
    my ($template, $data) = @_;
    return map { &parseone($_, $data) } split //, $template;
}

sub str {
    # Quote as a string. If I get enthusiastic I might arrange for
    # strange characters inside the string to be quoted.
    my $str = shift @_;
    return "'$str'";
}

sub sftp_setup {
    my $index = shift @_;
    my $chan = $channels[$index];
    $chan->{'obuf'} = $chan->{'ibuf'} = '';
    $chan->{'ocnt'} = $chan->{'icnt'} = 0;
    $chan->{'odata'} = $chan->{'idata'} = \&sftp_data;
    $chan->{'sftpreqs'} = {};
}

sub sftp_data {
    my ($chan, $index, $direction, $data) = @_;
    my $buf = \$chan->{$direction."buf"};
    my $cnt = \$chan->{$direction."cnt"};
    $$buf .= $data;
    while (length $$buf >= 4) {
        my $msglen = unpack "N", $$buf;
        last if length $$buf < 4 + $msglen;
        my $msg = substr $$buf, 4, $msglen;
        $$buf = substr $$buf, 4 + $msglen;
        $msg = [unpack "C*", $msg];
        my $type = shift @$msg;
        my $id = sprintf "ch%d_sftp_%s%d", $index, $direction, ${$cnt}++;
        print "$id: ";
        if (defined $sftp_packets{$type}) {
            $sftp_packets{$type}->($chan, $index, $direction, $id, $msg);
        } else {
            printf "unknown SFTP packet type %d\n", $type;
        }
    }
}

sub sftp_logreq {
    my ($chan, $direction, $reqid, $id, $name) = @_;
    print "$name";
    if ($direction eq "o") { # requests coming _in_ are too weird to track
        $chan->{'sftpreqs'}->{$reqid} = $id;
    }
}

sub sftp_logreply {
    my ($chan, $direction, $reqid, $id, $name) = @_;
    print "$name";
    if ($direction eq "i") { # replies going _out_ are too weird to track
        if (defined $chan->{'sftpreqs'}->{$reqid}) {
            print " to ", $chan->{'sftpreqs'}->{$reqid};
            $chan->{'sftpreqs'}->{$reqid} = undef;
        }
    }
}

sub sftp_parse_attrs {
    my ($data) = @_;
    my ($flags) = &parse("u", $data);
    return $flags if $flags eq "<missing>";
    my $out = "{";
    my $sep = "";
    if ($flags & 0x00000001) { # SSH_FILEXFER_ATTR_SIZE
        $out .= $sep . sprintf "size=%d", &parse("U", $data);
        $sep = ", ";
    }
    if ($flags & 0x00000002) { # SSH_FILEXFER_ATTR_UIDGID
        $out .= $sep . sprintf "uid=%d", &parse("u", $data);
        $out .= $sep . sprintf "gid=%d", &parse("u", $data);
        $sep = ", ";
    }
    if ($flags & 0x00000004) { # SSH_FILEXFER_ATTR_PERMISSIONS
        $out .= $sep . sprintf "perms=%#o", &parse("u", $data);
        $sep = ", ";
    }
    if ($flags & 0x00000008) { # SSH_FILEXFER_ATTR_ACMODTIME
        $out .= $sep . sprintf "atime=%d", &parse("u", $data);
        $out .= $sep . sprintf "mtime=%d", &parse("u", $data);
        $sep = ", ";
    }
    if ($flags & 0x80000000) { # SSH_FILEXFER_ATTR_EXTENDED
        my $extcount = &parse("u", $data);
        while ($extcount-- > 0) {
            $out .= $sep . sprintf "\"%s\"=\"%s\"", &parse("ss", $data);
            $sep = ", ";
        }
    }
    $out .= "}";
    return $out;
}

sub parse_public_key {
    my ($blob) = @_;
    my $data = [ unpack "C*", $blob ];
    my @toret;
    my ($type) = &parse("s", $data);
    push @toret, $type;
    if ($type eq "ssh-rsa") {
        my ($e, $n) = &parse("mm", $data);
        push @toret, "e", $e, "n", $n;
    } elsif ($type eq "ssh-dss") {
        my ($p, $q, $g, $y) = &parse("mmmm", $data);
        push @toret, "p", $p, "q", $q, "g", $g, "y", $y;
    } elsif ($type eq "ssh-ed25519") {
        my ($xyblob) = &parse("s", $data);
        my @y = unpack "C*", $xyblob;
        push @toret, "hibit(x)", $y[$#y] & 1;
        $y[$#y] &= ~1;
        push @toret, "y & ~1", &format_unsigned_hex_integer(@y);
    } elsif ($type =~ m!^ecdsa-sha2-nistp(256|384|521)$!) {
        my ($curvename, $blob) = &parse("ss", $data);
        push @toret, "curve name", $curvename;
        my @blobdata = unpack "C*", $blob;
        my ($fmt) = &parse("B", \@blobdata);
        push @toret, "format byte", $fmt;
        if ($fmt == 4) {
            push @toret, "x", &format_unsigned_hex_integer(
                @blobdata[0..($#blobdata+1)/2-1]);
            push @toret, "y", &format_unsigned_hex_integer(
                @blobdata[($#blobdata+1)/2..$#blobdata]);
        }
    } else {
        push @toret, "undecoded data", unpack "H*", pack "C*", @$data;
    }
    return @toret;
};

sub parse_signature {
    my ($blob, $keytype) = @_;
    my $data = [ unpack "C*", $blob ];
    my @toret;
    if ($keytype eq "ssh-rsa") {
        my ($type, $s) = &parse("ss", $data);
        push @toret, "sig type", $type;
        push @toret, "s", &format_unsigned_hex_integer(unpack "C*", $s);
    } elsif ($keytype eq "ssh-dss") {
        my ($type, $subblob) = &parse("ss", $data);
        push @toret, "sig type", $type;
        push @toret, "r", &format_unsigned_hex_integer(
            unpack "C*", substr($subblob, 0, 20));
        push @toret, "s", &format_unsigned_hex_integer(
            unpack "C*", substr($subblob, 20, 40));
    } elsif ($keytype eq "ssh-ed25519") {
        my ($type, $rsblob) = &parse("ss", $data);
        push @toret, "sig type", $type;
        my @ry = unpack "C*", $rsblob;
        my @sy = splice @ry, 32, 32;
        push @toret, "hibit(r.x)", $ry[$#ry] & 1;
        $ry[$#ry] &= ~1;
        push @toret, "r.y & ~1", &format_unsigned_hex_integer(@ry);
        push @toret, "hibit(s.x)", $sy[$#sy] & 1;
        $sy[$#sy] &= ~1;
        push @toret, "s.y & ~1", &format_unsigned_hex_integer(@sy);
    } elsif ($keytype =~ m!^ecdsa-sha2-nistp(256|384|521)$!) {
        my ($sigtype, $subblob) = &parse("ss", $data);
        push @toret, "sig type", $sigtype;
        my @sbdata = unpack "C*", $subblob;
        my ($r, $s) = &parse("mm", \@sbdata);
        push @toret, "r", $r, "s", $s;
    } else {
        push @toret, "undecoded data", unpack "H*", pack "C*", @$data;
    }
    return @toret;
};

sub stringescape {
    my ($str) = @_;
    $str =~ s!\\!\\\\!g;
    $str =~ s![^ -~]!sprintf "\\x%02X", ord $&!eg;
    return $str;
}
