#!/usr/bin/env perl
#
# Makefile generator.
# Produces the other PuTTY makefiles from the master one.

open IN,"Makefile";
@current = ();
while (<IN>) {
  chomp;
  if (/^##--/) {
    @words = split /\s+/,$_;
    shift @words; # remove ##--
    $i = shift @words; # first word
    if (!defined $i) { # no words
      @current = ();
    } elsif (!defined $words[0]) { # only one word
      @current = ($i);
    } else { # at least two words
      @current = map { $i . "." . $_ } @words;
      foreach $i (@words) { $projects{$i} = $i; }
      push @current, "objdefs";
    }
  } else {
    foreach $i (@current) { $store{$i} .= $_ . "\n"; }
  }
}
close IN;
@projects = keys %projects;

foreach $i (split '\n',$store{'gui-apps'}) {
  $i =~ s/^# //;
  $gui{$i} = 1;
}

foreach $i (split '\n',$store{'console-apps'}) {
  $i =~ s/^# //;
  $gui{$i} = -0;
}

sub project {
  my ($p) = @_;
  my ($q) = $store{"$p"} . $store{"objects.$p"} . $store{"resources.$p"};
  $q =~ s/(\S+)\s[^\n]*\n/\$($1) /gs;
  $q =~ s/ $//;
  $q;
}

sub projlist {
  my ($p) = @_;
  my ($q) = $store{"$p"} . $store{"objects.$p"} . $store{"resources.$p"};
  $q =~ s/(\S+)\s[^\n]*\n/$1 /gs;
  my (@q) = split ' ',$q;
  @q;
}

##-- CygWin makefile
open OUT, ">Makefile.cyg"; select OUT;
print
"# Makefile for PuTTY under cygwin.\n";
# gcc command line option is -D not /D
($_ = $store{"help"}) =~ s/=\/D/=-D/gs;
print $_;
print
"\n".
"# You can define this path to point at your tools if you need to\n".
"# TOOLPATH = c:\\cygwin\\bin\\ # or similar, if you're running Windows\n".
"# TOOLPATH = /pkg/mingw32msvc/i386-mingw32msvc/bin/\n".
"CC = \$(TOOLPATH)gcc\n".
"RC = \$(TOOLPATH)windres\n".
"# You may also need to tell windres where to find include files:\n".
"# RCINC = --include-dir c:\\cygwin\\include\\\n".
"\n".
"CFLAGS = -g -O2 -D_WINDOWS -DDEBUG\n".
"RCFLAGS = \$(RCINC) --define WIN32=1 --define _WIN32=1 --define WINVER=0x0400\n".
"LIBS = -ladvapi32 -luser32 -lgdi32 -lwsock32 -lcomctl32 -lcomdlg32\n".
"OBJ=o\n".
"RES=o\n".
"\n";
print $store{"objdefs"};
print
"\n".
".SUFFIXES:\n".
"\n".
"%.o: %.c\n".
"\t\$(CC) \$(FWHACK) \$(CFLAGS) -c \$<\n".
"\n".
"%.o: %.rc\n".
"\t\$(RC) \$(FWHACK) \$(RCFLAGS) \$<\n".
"\n";
foreach $p (@projects) {
  print $p, ".exe: ", &project($p), "\n";
  print "\t\$(CC) \$(LDFLAGS) -o \$@ " . &project($p), " \$(LIBS)\n\n";
}
print $store{"dependencies"};
print
"\n".
"version.o: FORCE\n".
"# Hack to force version.o to be rebuilt always\n".
"FORCE:\n".
"\t\$(CC) \$(FWHACK) \$(CFLAGS) \$(VER) -c version.c\n\n".
"clean:\n".
"\trm -f *.o *.exe *.res\n".
"\n";
select STDOUT; close OUT;

##-- Borland makefile
open OUT, ">Makefile.bor"; select OUT;
print
"# Makefile for PuTTY under Borland C.\n";
# bcc32 command line option is -D not /D
($_ = $store{"help"}) =~ s/=\/D/=-D/gs;
print $_;
print
"\n".
"# If you rename this file to `Makefile', you should change this line,\n".
"# so that the .rsp files still depend on the correct makefile.\n".
"MAKEFILE = Makefile.bor\n".
"\n".
"# Stop windows.h including winsock2.h which would conflict with winsock 1\n".
"CFLAGS = -DWIN32_LEAN_AND_MEAN\n".
"\n".
"# Get include directory for resource compiler\n".
"!if !\$d(BCB)\n".
"BCB = \$(MAKEDIR)\\..\n".
"!endif\n".
"\n".
".c.obj:\n".
"\tbcc32 \$(COMPAT) \$(FWHACK) \$(CFLAGS) /c \$*.c\n".
".rc.res:\n".
"\tbrc32 \$(FWHACK) -i \$(BCB)\\include \\\n".
"\t\t-r -DWIN32 -D_WIN32 -DWINVER=0x0400 \$*.rc\n".
"\n".
"OBJ=obj\n".
"RES=res\n".
"\n";
print $store{"objdefs"};
print "\n";
print "all:";
print map { " $_.exe" } @projects;
print "\n\n";
foreach $p (@projects) {
  print $p, ".exe: ", &project($p), " $p.rsp\n";
  $tw = $gui{$p} ? " -tW" : "";
  print "\tbcc32$tw -e$p.exe \@$p.rsp\n";
  print "\tbrc32 -fe$p.exe " . (join " ", &project("resources.$p")) . "\n\n";
}
foreach $p (@projects) {
  print $p, ".rsp: \$(MAKEFILE)\n";
  $arrow = ">";
  foreach $i (&projlist("objects.$p")) {
    print "\techo \$($i) $arrow $p.rsp\n";
    $arrow = ">>";
  }
  print "\n";
}
print $store{"dependencies"};
print
"\n".
"version.o: FORCE\n".
"# Hack to force version.o to be rebuilt always\n".
"FORCE:\n".
"\tbcc32 \$(FWHACK) \$(VER) \$(CFLAGS) /c version.c\n\n".
"clean:\n".
"\tdel *.obj\n".
"\tdel *.exe\n".
"\tdel *.res\n".
"\tdel *.pch\n".
"\tdel *.aps\n".
"\tdel *.ilk\n".
"\tdel *.pdb\n".
"\tdel *.rsp\n";
select STDOUT; close OUT;
