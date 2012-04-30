#!/usr/bin/perl

use strict;
use warnings;
use FileHandle;

my $dumpchannels = 0;
my $dumpdata = 0;
while ($ARGV[0] =~ /^-/) {
    my $opt = shift @ARGV;
    if ($opt eq "--") {
        last; # stop processing options
    } elsif ($opt eq "-c") {
        $dumpchannels = 1;
    } elsif ($opt eq "-d") {
        $dumpdata = 1;
    } else {
        die "unrecognised option '$opt'\n";
    }
}

my @channels = (); # ultimate channel ids are indices in this array
my %chan_by_id = (); # indexed by 'c%d' or 's%d' for client and server ids
my %globalreq = (); # indexed by 'i' or 'o'

my %packets = (
#define SSH2_MSG_DISCONNECT                       1	/* 0x1 */
    'SSH2_MSG_DISCONNECT' => sub {
        my ($direction, $seq, $data) = @_;
        my ($reason, $description, $lang) = &parse("uss", $data);
        printf "%s\n", &str($description);
    },
#define SSH2_MSG_IGNORE                           2	/* 0x2 */
    'SSH2_MSG_IGNORE' => sub {
        my ($direction, $seq, $data) = @_;
        my ($str) = &parse("s", $data);
        printf "(%d bytes)\n", length $str;
    },
#define SSH2_MSG_UNIMPLEMENTED                    3	/* 0x3 */
    'SSH2_MSG_UNIMPLEMENTED' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rseq) = &parse("u", $data);
        printf "i%d\n", $rseq;
    },
#define SSH2_MSG_DEBUG                            4	/* 0x4 */
    'SSH2_MSG_DEBUG' => sub {
        my ($direction, $seq, $data) = @_;
        my ($disp, $message, $lang) = &parse("bss", $data);
        printf "%s\n", &str($message);
    },
#define SSH2_MSG_SERVICE_REQUEST                  5	/* 0x5 */
    'SSH2_MSG_SERVICE_REQUEST' => sub {
        my ($direction, $seq, $data) = @_;
        my ($service) = &parse("s", $data);
        printf "%s\n", &str($service);
    },
#define SSH2_MSG_SERVICE_ACCEPT                   6	/* 0x6 */
    'SSH2_MSG_SERVICE_ACCEPT' => sub {
        my ($direction, $seq, $data) = @_;
        my ($service) = &parse("s", $data);
        printf "%s\n", &str($service);
    },
#define SSH2_MSG_KEXINIT                          20	/* 0x14 */
    'SSH2_MSG_KEXINIT' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_NEWKEYS                          21	/* 0x15 */
    'SSH2_MSG_NEWKEYS' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXDH_INIT                       30	/* 0x1e */
    'SSH2_MSG_KEXDH_INIT' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEXDH_REPLY                      31	/* 0x1f */
    'SSH2_MSG_KEXDH_REPLY' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEX_DH_GEX_REQUEST               30	/* 0x1e */
    'SSH2_MSG_KEX_DH_GEX_REQUEST' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEX_DH_GEX_GROUP                 31	/* 0x1f */
    'SSH2_MSG_KEX_DH_GEX_GROUP' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEX_DH_GEX_INIT                  32	/* 0x20 */
    'SSH2_MSG_KEX_DH_GEX_INIT' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_KEX_DH_GEX_REPLY                 33	/* 0x21 */
    'SSH2_MSG_KEX_DH_GEX_REPLY' => sub {
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
#define SSH2_MSG_USERAUTH_REQUEST                 50	/* 0x32 */
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
#define SSH2_MSG_USERAUTH_FAILURE                 51	/* 0x33 */
    'SSH2_MSG_USERAUTH_FAILURE' => sub {
        my ($direction, $seq, $data) = @_;
        my ($options) = &parse("s", $data);
        printf "%s\n", &str($options);
    },
#define SSH2_MSG_USERAUTH_SUCCESS                 52	/* 0x34 */
    'SSH2_MSG_USERAUTH_SUCCESS' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_BANNER                  53	/* 0x35 */
    'SSH2_MSG_USERAUTH_BANNER' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_PK_OK                   60	/* 0x3c */
    'SSH2_MSG_USERAUTH_PK_OK' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_PASSWD_CHANGEREQ        60	/* 0x3c */
    'SSH2_MSG_USERAUTH_PASSWD_CHANGEREQ' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_INFO_REQUEST            60	/* 0x3c */
    'SSH2_MSG_USERAUTH_INFO_REQUEST' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_USERAUTH_INFO_RESPONSE           61	/* 0x3d */
    'SSH2_MSG_USERAUTH_INFO_RESPONSE' => sub {
        my ($direction, $seq, $data) = @_;
        print "\n";
    },
#define SSH2_MSG_GLOBAL_REQUEST                   80	/* 0x50 */
    'SSH2_MSG_GLOBAL_REQUEST' => sub {
        my ($direction, $seq, $data) = @_;
        my ($type, $wantreply) = &parse("sb", $data);
        printf "%s (%s)", $type, $wantreply eq "yes" ? "reply" : "noreply";
        my $request = [$seq, $type];
        push @{$globalreq{$direction}}, $request if $wantreply;
        if ($type eq "tcpip-forward" or $type eq "cancel-tcpip-forward") {
            my ($addr, $port) = &parse("su", $data);
            printf " %s:%s", $addr, $port;
            push @$request, $port;
        }
        print "\n";
    },
#define SSH2_MSG_REQUEST_SUCCESS                  81	/* 0x51 */
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
#define SSH2_MSG_REQUEST_FAILURE                  82	/* 0x52 */
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
#define SSH2_MSG_CHANNEL_OPEN                     90	/* 0x5a */
    'SSH2_MSG_CHANNEL_OPEN' => sub {
        my ($direction, $seq, $data) = @_;
        my ($type, $sid, $winsize, $packet) = &parse("suuu", $data);
        # CHANNEL_OPEN tells the other side the _sender's_ id for the
        # channel, so this choice between "s" and "c" prefixes is
        # opposite to every other message in the protocol, which all
        # quote the _recipient's_ id of the channel.
        $sid = ($direction eq "i" ? "s" : "c") . $sid;
        my $chan = {'id'=>$sid, 'state'=>'halfopen'};
        push @channels, $chan;
        my $index = $#channels;
        $chan_by_id{$sid} = $index;
        printf "ch%d (%s) %s", $index, $chan->{'id'}, $type;
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
#define SSH2_MSG_CHANNEL_OPEN_CONFIRMATION        91	/* 0x5b */
    'SSH2_MSG_CHANNEL_OPEN_CONFIRMATION' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $sid, $winsize, $packet) = &parse("uuuu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        $sid = ($direction eq "i" ? "s" : "c") . $sid;
        $chan_by_id{$sid} = $index;
        my $chan = $channels[$index];
        $chan->{'id'} = ($direction eq "i" ? "$rid/$sid" : "$sid/$rid");
        $chan->{'state'} = 'open';
        printf "ch%d (%s)\n", $index, $chan->{'id'};
    },
#define SSH2_MSG_CHANNEL_OPEN_FAILURE             92	/* 0x5c */
    'SSH2_MSG_CHANNEL_OPEN_FAILURE' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $reason, $desc, $lang) = &parse("uuss", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        my $chan = $channels[$index];
        $chan->{'state'} = 'rejected';
        printf "ch%d (%s) %s\n", $index, $chan->{'id'}, &str($reason);
    },
#define SSH2_MSG_CHANNEL_WINDOW_ADJUST            93	/* 0x5d */
    'SSH2_MSG_CHANNEL_WINDOW_ADJUST' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $bytes) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        my $chan = $channels[$index];
        printf "ch%d (%s) +%s\n", $index, $chan->{'id'}, $bytes;
    },
#define SSH2_MSG_CHANNEL_DATA                     94	/* 0x5e */
    'SSH2_MSG_CHANNEL_DATA' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $bytes) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        my $chan = $channels[$index];
        printf "ch%d (%s), %s bytes\n", $index, $chan->{'id'}, $bytes;
        if ($dumpdata) {
            my $filekey = $direction . "file";
            if (!defined $chan->{$filekey}) {
                my $filename = sprintf "ch%d.%s", $index, $direction;
                $chan->{$filekey} = FileHandle->new(">$filename");
                if (!defined $chan->{$filekey}) {
                    die "$filename: $!\n";
                }
            }
            my @realdata = splice @$data, 0, $bytes;
            die "channel data not present in $seq\n" if @realdata < $bytes;
            my $rawdata = pack "C*", @realdata;
            my $fh = $chan->{$filekey};
            print $fh $rawdata;
        }
    },
#define SSH2_MSG_CHANNEL_EXTENDED_DATA            95	/* 0x5f */
    'SSH2_MSG_CHANNEL_EXTENDED_DATA' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $bytes) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        my $chan = $channels[$index];
        printf "ch%d (%s), %s bytes\n", $index, $chan->{'id'}, $bytes;
        if ($dumpdata) {
            # We treat EXTENDED_DATA as equivalent to DATA, for the
            # moment. It's not clear what else would be a better thing
            # to do with it, and this at least is the Right Answer if
            # the data is going to a terminal and the aim is to debug
            # the terminal emulator.
            my $filekey = $direction . "file";
            if (!defined $chan->{$filekey}) {
                my $filename = sprintf "ch%d.%s", $index, $direction;
                $chan->{$filekey} = FileHandle->new;
                if (!$chan->{$filekey}->open(">", $filename)) {
                    die "$filename: $!\n";
                }
            }
            my @realdata = splice @$data, 0, $bytes;
            die "channel data not present in $seq\n" if @realdata < $bytes;
            my $rawdata = pack "C*", @realdata;
            my $fh = $chan->{$filekey};
            print $fh $rawdata;
        }
    },
#define SSH2_MSG_CHANNEL_EOF                      96	/* 0x60 */
    'SSH2_MSG_CHANNEL_EOF' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        my $chan = $channels[$index];
        printf "ch%d (%s)\n", $index, $chan->{'id'};
    },
#define SSH2_MSG_CHANNEL_CLOSE                    97	/* 0x61 */
    'SSH2_MSG_CHANNEL_CLOSE' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
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
#define SSH2_MSG_CHANNEL_REQUEST                  98	/* 0x62 */
    'SSH2_MSG_CHANNEL_REQUEST' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid, $type, $wantreply) = &parse("usb", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
        my $chan = $channels[$index];
        printf "ch%d (%s) %s (%s)",
            $index, $chan->{'id'}, $type, $wantreply eq "yes" ? "reply" : "noreply";
        push @{$chan->{'requests_'.$direction}}, [$seq, $type] if $wantreply;
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
#define SSH2_MSG_CHANNEL_SUCCESS                  99	/* 0x63 */
    'SSH2_MSG_CHANNEL_SUCCESS' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
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
#define SSH2_MSG_CHANNEL_FAILURE                  100	/* 0x64 */
    'SSH2_MSG_CHANNEL_FAILURE' => sub {
        my ($direction, $seq, $data) = @_;
        my ($rid) = &parse("uu", $data);
        $rid = ($direction eq "i" ? "c" : "s") . $rid;
        my $index = $chan_by_id{$rid};
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

my ($direction, $seq, $ourseq, $type, $data, $recording);
my %ourseqs = ('i'=>0, 'o'=>0);

$recording = 0;
while (<>) {
    if ($recording) {
        if (/^  [0-9a-fA-F]{8}  ((?:[0-9a-fA-F]{2} )*[0-9a-fA-F]{2})/) {
            push @$data, map { $_ eq "XX" ? -1 : hex $_ } split / /, $1;
        } else {
            $recording = 0;
            my $fullseq = "$direction$ourseq";
            print "$fullseq: $type ";
            if (defined $packets{$type}) {
                $packets{$type}->($direction, $fullseq, $data);
            } else {
                printf "raw %s\n", join "", map { sprintf "%02x", $_ } @$data;
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

sub parseone {
    my ($type, $data) = @_;
    if ($type eq "u") { # uint32
        my @bytes = splice @$data, 0, 4;
        return "<missing>" if @bytes < 4 or grep { $_<0 } @bytes;
        return unpack "N", pack "C*", @bytes;
    } elsif ($type eq "U") { # uint64
        my @bytes = splice @$data, 0, 4;
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
        if ($type eq "mpint") {
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
            $str .= "0x" . join "", map { sprintf "%02x", $_ } @bytes;
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
