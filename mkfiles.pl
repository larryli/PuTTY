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
  $speciallibs = [split ' ', $i];
  $i = shift @$speciallibs; # take off project name
  $gui{$i} = 1;
  $libs{$i} = $speciallibs;
}

foreach $i (split '\n',$store{'console-apps'}) {
  $i =~ s/^# //;
  $speciallibs = [split ' ', $i];
  $i = shift @$speciallibs; # take off project name
  $gui{$i} = 0;
  $libs{$i} = $speciallibs;
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
"CFLAGS = -mno-cygwin -Wall -O2 -D_WINDOWS -DDEBUG -DWIN32S_COMPAT".
  " -DNO_SECURITY -D_NO_OLDNAMES -I.\n".
"LDFLAGS = -mno-cygwin -s\n".
"RCFLAGS = \$(RCINC) --define WIN32=1 --define _WIN32=1 --define WINVER=0x0400 --define MINGW32_FIX=1\n".
"LIBS = -ladvapi32 -luser32 -lgdi32 -lwsock32 -lcomctl32 -lcomdlg32 -lwinmm -limm32 -lwinspool\n".
"OBJ=o\n".
"RES=res.o\n".
"\n";
print $store{"objdefs"};
print
"\n".
".SUFFIXES:\n".
"\n".
"%.o: %.c\n".
"\t\$(CC) \$(COMPAT) \$(FWHACK) \$(XFLAGS) \$(CFLAGS) -c \$<\n".
"\n".
"%.res.o: %.rc\n".
"\t\$(RC) \$(FWHACK) \$(RCFL) \$(RCFLAGS) \$< \$\@\n".
"\n";
print "all:";
print map { " $_.exe" } @projects;
print "\n\n";
foreach $p (@projects) {
  print $p, ".exe: ", &project($p), "\n";
  my $mw = $gui{$p} ? " -mwindows" : "";
  $libstr = "";
  foreach $lib (@{$libs{$p}}) { $libstr .= " -l$lib"; }
  print "\t\$(CC)" . $mw . " \$(LDFLAGS) -o \$@ " . &project($p), " \$(LIBS)$libstr\n\n";
}
print $store{"dependencies"};
print
"\n".
"version.o: FORCE;\n".
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
"# Makefile for PuTTY under Borland C++.\n";
# bcc32 command line option is -D not /D
($_ = $store{"help"}) =~ s/=\/D/=-D/gs;
print $_;
print
"\n".
"# If you rename this file to `Makefile', you should change this line,\n".
"# so that the .rsp files still depend on the correct makefile.\n".
"MAKEFILE = Makefile.bor\n".
"\n".
"# C compilation flags\n".
"CFLAGS = -DWINVER=0x0401\n".
"\n".
"# Get include directory for resource compiler\n".
"!if !\$d(BCB)\n".
"BCB = \$(MAKEDIR)\\..\n".
"!endif\n".
"\n".
".c.obj:\n".
"\tbcc32 -w-aus -w-ccc -w-par \$(COMPAT) \$(FWHACK) \$(XFLAGS) \$(CFLAGS) /c \$*.c\n".
".rc.res:\n".
"\tbrcc32 \$(FWHACK) \$(RCFL) -i \$(BCB)\\include \\\n".
"\t\t-r -DNO_WINRESRC_H -DWIN32 -D_WIN32 -DWINVER=0x0401 \$*.rc\n".
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
  $ap = $gui{$p} ? " -aa" : " -ap";
  print "\tilink32$ap -Gn -L\$(BCB)\\lib \@$p.rsp\n\n";
}
foreach $p (@projects) {
  print $p, ".rsp: \$(MAKEFILE)\n";
  $c0w = $gui{$p} ? "c0w32" : "c0x32";
  print "\techo $c0w + > $p.rsp\n";
  @objlines = &projlist("objects.$p");
  for ($i=0; $i<=$#objlines; $i++) {
    $plus = ($i < $#objlines ? " +" : "");
    print "\techo \$($objlines[$i])$plus >> $p.rsp\n";
  }
  print "\techo $p.exe >> $p.rsp\n";
  @libs = @{$libs{$p}};
  unshift @libs, "cw32", "import32";
  $libstr = join ' ', @libs;
  print "\techo nul,$libstr, >> $p.rsp\n";
  print "\techo " . (join " ", &project("resources.$p")) . " >> $p.rsp\n";
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
"\t-del *.obj\n".
"\t-del *.exe\n".
"\t-del *.res\n".
"\t-del *.pch\n".
"\t-del *.aps\n".
"\t-del *.il*\n".
"\t-del *.pdb\n".
"\t-del *.rsp\n".
"\t-del *.tds\n".
"\t-del *.\$\$\$\$\$\$\n";
select STDOUT; close OUT;
