# Visual C++ Makefile for PuTTY.
#
# Use `nmake' to build.
#

##-- help
#
# Extra options you can set:
#
#  - FWHACK=/DFWHACK
#      Enables a hack that tunnels through some firewall proxies.
#
#  - VER=/DSNAPSHOT=1999-01-25
#      Generates executables whose About box report them as being a
#      development snapshot.
#
#  - VER=/DRELEASE=0.43
#      Generates executables whose About box report them as being a
#      release version.
#
#  - COMPAT=/DWIN32S_COMPAT
#      Generates a binary that works (minimally) with Win32s.
#
#  - RCFL=/DASCIICTLS
#      Uses ASCII rather than Unicode to specify the tab control in
#      the resource file. Probably most useful when compiling with
#      Cygnus/mingw32, whose resource compiler may have less of a
#      problem with it.
#
##--

CFLAGS = /nologo /W3 /YX /O2 /Yd /D_WINDOWS /DDEBUG /ML /Fd
# LFLAGS = /debug

# Use MSVC DLL
# CFLAGS = /nologo /W3 /YX /O2 /Yd /D_WINDOWS /DDEBUG /MD /Fd

# Disable debug and incremental linking
LFLAGS = /incremental:no

.c.obj:
	cl $(COMPAT) $(FWHACK) $(CFLAGS) /c $*.c

OBJ=obj
RES=res

##-- objects putty puttytel
GOBJS1 = window.$(OBJ) windlg.$(OBJ) terminal.$(OBJ) telnet.$(OBJ) raw.$(OBJ)
GOBJS2 = xlat.$(OBJ) ldisc.$(OBJ) sizetip.$(OBJ)
##-- objects putty
POBJS = ssh.$(OBJ) be_all.$(OBJ)
##-- objects puttytel
TOBJS = be_nossh.$(OBJ)
##-- objects pscp
SOBJS = scp.$(OBJ) windlg.$(OBJ) ssh.$(OBJ) be_none.$(OBJ)
##-- objects putty puttytel pscp
MOBJS = misc.$(OBJ) version.$(OBJ)
##-- objects putty pscp
OBJS1 = sshcrc.$(OBJ) sshdes.$(OBJ) sshmd5.$(OBJ) sshrsa.$(OBJ) sshrand.$(OBJ)
OBJS2 = sshsha.$(OBJ) sshblowf.$(OBJ) noise.$(OBJ) sshdh.$(OBJ) sshdss.$(OBJ)
OBJS3 = sshbn.$(OBJ)
##-- resources putty
PRESRC = win_res.$(RES)
##-- resources puttytel
TRESRC = nosshres.$(RES)
##-- resources pscp
SRESRC = scp.$(RES)
##--

##-- gui-apps
# putty
# puttytel
##-- console-apps
# pscp
##--

LIBS1 = advapi32.lib user32.lib gdi32.lib
LIBS2 = wsock32.lib comctl32.lib comdlg32.lib

all: putty.exe puttytel.exe pscp.exe

putty.exe: $(GOBJS1) $(GOBJS2) $(POBJS) $(MOBJS) $(OBJS1) $(OBJS2) $(OBJS3) $(PRESRC) putty.rsp
	link $(LFLAGS) -out:putty.exe @putty.rsp

puttytel.exe: $(GOBJS1) $(GOBJS2) $(TOBJS) $(MOBJS) $(TRESRC) puttytel.rsp
	link $(LFLAGS) -out:puttytel.exe @puttytel.rsp

pscp.exe: $(SOBJS) $(MOBJS) $(OBJS1) $(OBJS2) $(OBJS3) $(SRESRC) pscp.rsp
	link $(LFLAGS) -out:pscp.exe @pscp.rsp

putty.rsp: makefile
	echo /nologo /subsystem:windows > putty.rsp
	echo $(GOBJS1) >> putty.rsp
	echo $(GOBJS2) >> putty.rsp
	echo $(POBJS) >> putty.rsp
	echo $(MOBJS) >> putty.rsp
	echo $(OBJS1) >> putty.rsp
	echo $(OBJS2) >> putty.rsp
	echo $(OBJS3) >> putty.rsp
	echo $(PRESRC) >> putty.rsp
	echo $(LIBS1) >> putty.rsp
	echo $(LIBS2) >> putty.rsp

puttytel.rsp: makefile
	echo /nologo /subsystem:windows > puttytel.rsp
	echo $(GOBJS1) >> puttytel.rsp
	echo $(GOBJS2) >> puttytel.rsp
	echo $(TOBJS) >> puttytel.rsp
	echo $(MOBJS) >> puttytel.rsp
	echo $(TRESRC) >> puttytel.rsp
	echo $(LIBS1) >> puttytel.rsp
	echo $(LIBS2) >> puttytel.rsp

pscp.rsp: makefile
	echo /nologo /subsystem:console > pscp.rsp
	echo $(SOBJS) >> pscp.rsp
	echo $(MOBJS) >> pscp.rsp
	echo $(OBJS1) >> pscp.rsp
	echo $(OBJS2) >> pscp.rsp
	echo $(OBJS3) >> pscp.rsp
	echo $(SRESRC) >> pscp.rsp
	echo $(LIBS1) >> pscp.rsp
	echo $(LIBS2) >> pscp.rsp

##-- dependencies
window.$(OBJ): window.c putty.h win_res.h
windlg.$(OBJ): windlg.c putty.h ssh.h win_res.h
terminal.$(OBJ): terminal.c putty.h
sizetip.$(OBJ): sizetip.c putty.h
telnet.$(OBJ): telnet.c putty.h
raw.$(OBJ): raw.c putty.h
xlat.$(OBJ): xlat.c putty.h
ldisc.$(OBJ): ldisc.c putty.h
misc.$(OBJ): misc.c putty.h
noise.$(OBJ): noise.c putty.h ssh.h
ssh.$(OBJ): ssh.c ssh.h putty.h
sshcrc.$(OBJ): sshcrc.c ssh.h
sshdes.$(OBJ): sshdes.c ssh.h
sshmd5.$(OBJ): sshmd5.c ssh.h
sshrsa.$(OBJ): sshrsa.c ssh.h
sshsha.$(OBJ): sshsha.c ssh.h
sshrand.$(OBJ): sshrand.c ssh.h
sshblowf.$(OBJ): sshblowf.c ssh.h
sshdh.$(OBJ): sshdh.c ssh.h
sshdss.$(OBJ): sshdss.c ssh.h
sshbn.$(OBJ): sshbn.c ssh.h
scp.$(OBJ): scp.c putty.h scp.h
version.$(OBJ): version.c
be_all.$(OBJ): be_all.c
be_nossh.$(OBJ): be_nossh.c
be_none.$(OBJ): be_none.c
##--

# Hack to force version.obj to be rebuilt always
version.obj: versionpseudotarget
	@echo (built version.obj)
versionpseudotarget:
	cl $(FWHACK) $(VER) $(CFLAGS) /c version.c

##-- dependencies
win_res.$(RES): win_res.rc win_res.h putty.ico
##--
win_res.$(RES):
	rc $(FWHACK) $(RCFL) -r -DWIN32 -D_WIN32 -DWINVER=0x0400 win_res.rc

##-- dependencies
nosshres.$(RES): nosshres.rc win_res.rc win_res.h putty.ico
##--
nosshres.$(RES):
	rc $(FWHACK) $(RCFL) -r -DWIN32 -D_WIN32 -DWINVER=0x0400 nosshres.rc

##-- dependencies
scp.$(RES): scp.rc scp.ico
##--
scp.$(RES):
	rc $(FWHACK) $(RCFL) -r -DWIN32 -D_WIN32 -DWINVER=0x0400 scp.rc

clean:
	del *.obj
	del *.exe
	del *.res
	del *.pch
	del *.aps
	del *.ilk
	del *.pdb
	del *.rsp
