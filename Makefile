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
##--

CFLAGS = /nologo /W3 /YX /O2 /Yd /D_WINDOWS /DDEBUG /ML /Fd

.c.obj:
	cl $(COMPAT) $(FWHACK) $(CFLAGS) /c $*.c

OBJ=obj
RES=res

##-- objects putty
POBJS1 = window.$(OBJ) windlg.$(OBJ) terminal.$(OBJ) telnet.$(OBJ) raw.$(OBJ)
POBJS2 = xlat.$(OBJ) ldisc.$(OBJ) sizetip.$(OBJ) ssh.$(OBJ)
##-- objects pscp
SOBJS = scp.$(OBJ) windlg.$(OBJ) scpssh.$(OBJ)
##-- objects putty pscp
OBJS1 = misc.$(OBJ) noise.$(OBJ)
OBJS2 = sshcrc.$(OBJ) sshdes.$(OBJ) sshmd5.$(OBJ) sshrsa.$(OBJ) sshrand.$(OBJ)
OBJS3 = sshsha.$(OBJ) sshblowf.$(OBJ) version.$(OBJ)
##-- resources putty
PRESRC = win_res.$(RES)
##-- resources pscp
SRESRC = scp.$(RES)
##--

##-- gui-apps
# putty
##-- console-apps
# pscp
##--

LIBS1 = advapi32.lib user32.lib gdi32.lib
LIBS2 = wsock32.lib comctl32.lib comdlg32.lib

all: putty.exe pscp.exe

putty.exe: $(POBJS1) $(POBJS2) $(OBJS1) $(OBJS2) $(OBJS3) $(PRESRC) link.rsp
	link /debug -out:putty.exe @link.rsp

pscp.exe: $(SOBJS) $(OBJS1) $(OBJS2) $(OBJS3) $(SRESRC) scp.rsp
	link /debug -out:pscp.exe @scp.rsp

link.rsp: makefile
	echo /nologo /subsystem:windows > link.rsp
	echo $(POBJS1) >> link.rsp
	echo $(POBJS2) >> link.rsp
	echo $(OBJS1) >> link.rsp
	echo $(OBJS2) >> link.rsp
	echo $(OBJS3) >> link.rsp
	echo $(PRESRC) >> link.rsp
	echo $(LIBS1) >> link.rsp
	echo $(LIBS2) >> link.rsp

scp.rsp: makefile
	echo /nologo /subsystem:console > scp.rsp
	echo $(SOBJS) >> scp.rsp
	echo $(OBJS1) >> scp.rsp
	echo $(OBJS2) >> scp.rsp
	echo $(OBJS3) >> scp.rsp
	echo $(SRESRC) >> scp.rsp
	echo $(LIBS1) >> scp.rsp
	echo $(LIBS2) >> scp.rsp

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
scp.$(OBJ): scp.c putty.h scp.h
scpssh.$(OBJ): scpssh.c putty.h ssh.h scp.h
version.$(OBJ): version.c
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
	rc $(FWHACK) -r -DWIN32 -D_WIN32 -DWINVER=0x0400 win_res.rc

##-- dependencies
scp.$(RES): scp.rc scp.ico
##--
scp.$(RES):
	rc $(FWHACK) -r -DWIN32 -D_WIN32 -DWINVER=0x0400 scp.rc

clean:
	del *.obj
	del *.exe
	del *.res
	del *.pch
	del *.aps
	del *.ilk
	del *.pdb
	del *.rsp
