# Makefile for PuTTY. Use `FWHACK=/DFWHACK' to cause the firewall hack
# to be built in. (requires rebuild of ssh.obj only)

# Can also build with `VER=/DSNAPSHOT=1999-01-25' or
# `VER=/DRELEASE=0.43' to get version numbering; otherwise you'll
# get `Unidentified build'.

# COMPAT=/DWIN32S_COMPAT will produce a binary that works (minimally)
# with Win32s

CFLAGS = /nologo /W3 /YX /O2 /Yd /D_WINDOWS /DDEBUG /ML /Fd

.c.obj:
	cl $(COMPAT) $(FWHACK) $(CFLAGS) /c $*.c

POBJS1 = window.obj windlg.obj terminal.obj telnet.obj raw.obj
POBJS2 = xlat.obj ldisc.obj
OBJS1 = misc.obj noise.obj
OBJS2 = ssh.obj sshcrc.obj sshdes.obj sshmd5.obj sshrsa.obj sshrand.obj
OBJS3 = sshsha.obj sshblowf.obj version.obj sizetip.obj
RESRC = win_res.res
LIBS1 = advapi32.lib user32.lib gdi32.lib
LIBS2 = wsock32.lib comctl32.lib comdlg32.lib
SCPOBJS = scp.obj windlg.obj misc.obj noise.obj 
SCPOBJS2 = scpssh.obj sshcrc.obj sshdes.obj sshmd5.obj
SCPOBJS3 = sshrsa.obj sshrand.obj sshsha.obj sshblowf.obj version.obj

all: putty.exe pscp.exe

putty.exe: $(POBJS1) $(POBJS2) $(OBJS1) $(OBJS2) $(OBJS3) $(RESRC) link.rsp
	link /debug -out:putty.exe @link.rsp

puttyd.exe: $(POBJS1) $(POBJS2) $(OBJS1) $(OBJS2) $(OBJS3) $(RESRC) link.rsp
	link /debug -out:puttyd.exe @link.rsp

link.rsp: makefile
	echo /nologo /subsystem:windows > link.rsp
	echo $(POBJS1) >> link.rsp
	echo $(POBJS2) >> link.rsp
	echo $(OBJS1) >> link.rsp
	echo $(OBJS2) >> link.rsp
	echo $(OBJS3) >> link.rsp
	echo $(RESRC) >> link.rsp
	echo $(LIBS1) >> link.rsp
	echo $(LIBS2) >> link.rsp

window.obj: window.c putty.h win_res.h
windlg.obj: windlg.c putty.h ssh.h win_res.h
terminal.obj: terminal.c putty.h
sizetip.obj: sizetip.c putty.h
telnet.obj: telnet.c putty.h
raw.obj: raw.c putty.h
xlat.obj: xlat.c putty.h
ldisc.obj: ldisc.c putty.h
misc.obj: misc.c putty.h
noise.obj: noise.c putty.h ssh.h
ssh.obj: ssh.c ssh.h putty.h
sshcrc.obj: sshcrc.c ssh.h
sshdes.obj: sshdes.c ssh.h
sshmd5.obj: sshmd5.c ssh.h
sshrsa.obj: sshrsa.c ssh.h
sshsha.obj: sshsha.c ssh.h
sshrand.obj: sshrand.c ssh.h
sshblowf.obj: sshblowf.c ssh.h
version.obj: versionpseudotarget
	@echo (built version.obj)

# Hack to force version.obj to be rebuilt always
versionpseudotarget:
	cl $(FWHACK) $(VER) $(CFLAGS) /c version.c

win_res.res: win_res.rc win_res.h putty.ico
	rc $(FWHACK) -r -DWIN32 -D_WIN32 -DWINVER=0x0400 win_res.rc

pscp.exe: $(SCPOBJS) $(SCPOBJS2) $(SCPOBJS3) scp.res scp.rsp
	link /debug -out:pscp.exe @scp.rsp

scp.rsp: makefile
	echo /nologo /subsystem:console > scp.rsp
	echo $(SCPOBJS) >> scp.rsp
	echo $(SCPOBJS2) >> scp.rsp
	echo $(SCPOBJS3) >> scp.rsp
	echo scp.res >> scp.rsp
	echo $(LIBS1) >> scp.rsp
	echo $(LIBS2) >> scp.rsp

scp.obj: scp.c putty.h scp.h
scpssh.obj: scpssh.c putty.h ssh.h scp.h

scp.res: scp.rc scp.ico
	rc -r -DWIN32 -D_WIN32 -DWINVER=0x0400 scp.rc

clean:
	del *.obj
	del *.exe
	del *.res
	del *.pch
	del *.aps
	del *.ilk
	del *.pdb
	del *.rsp
