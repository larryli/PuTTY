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
#  - COMPAT=/DAUTO_WINSOCK
#      Causes PuTTY to assume that <windows.h> includes its own WinSock
#      header file, so that it won't try to include <winsock.h>.
#
#  - COMPAT=/DWINSOCK_TWO
#      Causes the PuTTY utilities to include <winsock2.h> instead of
#      <winsock.h>, except Plink which _needs_ WinSock 2 so it already
#      does this.
#
#  - RCFL=/DASCIICTLS
#      Uses ASCII rather than Unicode to specify the tab control in
#      the resource file. Probably most useful when compiling with
#      Cygnus/mingw32, whose resource compiler may have less of a
#      problem with it.
#
#  - XFLAGS=/DMALLOC_LOG
#      Causes PuTTY to emit a file called putty_mem.log, logging every
#      memory allocation and free, so you can track memory leaks.
#
#  - XFLAGS=/DMINEFIELD
#      Causes PuTTY to use a custom memory allocator, similar in
#      concept to Electric Fence, in place of regular malloc(). Wastes
#      huge amounts of RAM, but should cause heap-corruption bugs to
#      show up as GPFs at the point of failure rather than appearing
#      later on as second-level damage.
#
##--

CFLAGS = /nologo /W3 /YX /O2 /Yd /D_WINDOWS /DDEBUG /ML /Fd
# LFLAGS = /debug

# Use MSVC DLL
# CFLAGS = /nologo /W3 /YX /O2 /Yd /D_WINDOWS /DDEBUG /MD /Fd

# Disable debug and incremental linking
LFLAGS = /incremental:no

.c.obj:
	cl $(COMPAT) $(FWHACK) $(XFLAGS) $(CFLAGS) /c $*.c

OBJ=obj
RES=res

##-- objects putty puttytel
GOBJS1 = window.$(OBJ) windlg.$(OBJ) winctrls.$(OBJ) terminal.$(OBJ)
GOBJS2 = xlat.$(OBJ) sizetip.$(OBJ)
##-- objects putty puttytel plink
LOBJS1 = telnet.$(OBJ) raw.$(OBJ) rlogin.$(OBJ) ldisc.$(OBJ) winnet.$(OBJ)
##-- objects putty plink
POBJS = be_all.$(OBJ)
##-- objects puttytel
TOBJS = be_nossh.$(OBJ)
##-- objects plink
PLOBJS = plink.$(OBJ)
##-- objects pscp
SOBJS = scp.$(OBJ) winnet.$(OBJ) be_none.$(OBJ)
##-- objects putty puttytel pscp plink
MOBJS = misc.$(OBJ) version.$(OBJ) winstore.$(OBJ) settings.$(OBJ)
MOBJ2 = tree234.$(OBJ)
##-- objects putty pscp plink
OBJS1 = sshcrc.$(OBJ) sshdes.$(OBJ) sshmd5.$(OBJ) sshrsa.$(OBJ) sshrand.$(OBJ)
OBJS2 = sshsha.$(OBJ) sshblowf.$(OBJ) noise.$(OBJ) sshdh.$(OBJ) sshdss.$(OBJ)
OBJS3 = sshbn.$(OBJ) sshpubk.$(OBJ) ssh.$(OBJ) pageantc.$(OBJ) sshzlib.$(OBJ)
OBJS4 = x11fwd.$(OBJ)
##-- objects pageant
PAGE1 = pageant.$(OBJ) sshrsa.$(OBJ) sshpubk.$(OBJ) sshdes.$(OBJ) sshbn.$(OBJ)
PAGE2 = sshmd5.$(OBJ) version.$(OBJ) tree234.$(OBJ) misc.$(OBJ)
##-- objects puttygen
GEN1 = puttygen.$(OBJ) sshrsag.$(OBJ) sshprime.$(OBJ) sshdes.$(OBJ)
GEN2 = sshbn.$(OBJ) sshmd5.$(OBJ) version.$(OBJ) sshrand.$(OBJ) noise.$(OBJ)
GEN3 = sshsha.$(OBJ) winstore.$(OBJ) misc.$(OBJ) winctrls.$(OBJ)
GEN4 = sshrsa.$(OBJ) sshpubk.$(OBJ)
##-- resources putty puttytel
PRESRC = win_res.$(RES)
##-- resources pageant
PAGERC = pageant.$(RES)
##-- resources puttygen
GENRC = puttygen.$(RES)
##-- resources pscp
SRESRC = scp.$(RES)
##-- resources plink
LRESRC = plink.$(RES)
##--

##-- gui-apps
# putty
# puttytel
# pageant
# puttygen
##-- console-apps
# pscp
# plink ws2_32
##--

LIBS1 = advapi32.lib user32.lib gdi32.lib
LIBS2 = comctl32.lib comdlg32.lib
LIBS3 = shell32.lib
SOCK1 = wsock32.lib
SOCK2 = ws2_32.lib

all: putty.exe puttytel.exe pscp.exe plink.exe pageant.exe puttygen.exe

putty.exe: $(GOBJS1) $(GOBJS2) $(LOBJS1) $(POBJS) $(MOBJS) $(MOBJ2) $(OBJS1) $(OBJS2) $(OBJS3) $(OBJS4) $(PRESRC) putty.rsp
	link $(LFLAGS) -out:putty.exe -map:putty.map @putty.rsp

puttytel.exe: $(GOBJS1) $(GOBJS2) $(LOBJS1) $(TOBJS) $(MOBJS) $(MOBJ2) $(PRESRC) puttytel.rsp
	link $(LFLAGS) -out:puttytel.exe -map:puttytel.map @puttytel.rsp

pageant.exe: $(PAGE1) $(PAGE2) $(PAGERC) pageant.rsp
	link $(LFLAGS) -out:pageant.exe -map:pageant.map @pageant.rsp

puttygen.exe: $(GEN1) $(GEN2) $(GEN3) $(GEN4) $(GENRC) puttygen.rsp
	link $(LFLAGS) -out:puttygen.exe -map:puttygen.map @puttygen.rsp

pscp.exe: $(SOBJS) $(MOBJS) $(MOBJ2) $(OBJS1) $(OBJS2) $(OBJS3) $(OBJS4) $(SRESRC) pscp.rsp
	link $(LFLAGS) -out:pscp.exe -map:pscp.map @pscp.rsp

plink.exe: $(LOBJS1) $(POBJS) $(PLOBJS) $(MOBJS) $(MOBJ2) $(OBJS1) $(OBJS2) $(OBJS3) $(OBJS4) $(LRESRC) plink.rsp
	link $(LFLAGS) -out:plink.exe -map:plink.map @plink.rsp

putty.rsp: makefile
	echo /nologo /subsystem:windows > putty.rsp
	echo $(GOBJS1) >> putty.rsp
	echo $(GOBJS2) >> putty.rsp
	echo $(LOBJS1) >> putty.rsp
	echo $(POBJS) >> putty.rsp
	echo $(MOBJS) >> putty.rsp
	echo $(MOBJ2) >> putty.rsp
	echo $(OBJS1) >> putty.rsp
	echo $(OBJS2) >> putty.rsp
	echo $(OBJS3) >> putty.rsp
	echo $(OBJS4) >> putty.rsp
	echo $(PRESRC) >> putty.rsp
	echo $(LIBS1) >> putty.rsp
	echo $(LIBS2) >> putty.rsp
	echo $(SOCK1) >> putty.rsp

puttytel.rsp: makefile
	echo /nologo /subsystem:windows > puttytel.rsp
	echo $(GOBJS1) >> puttytel.rsp
	echo $(GOBJS2) >> puttytel.rsp
	echo $(LOBJS1) >> puttytel.rsp
	echo $(TOBJS) >> puttytel.rsp
	echo $(MOBJS) >> puttytel.rsp
	echo $(MOBJ2) >> puttytel.rsp
	echo $(PRESRC) >> puttytel.rsp
	echo $(LIBS1) >> puttytel.rsp
	echo $(LIBS2) >> puttytel.rsp
	echo $(SOCK1) >> puttytel.rsp

pageant.rsp: makefile
	echo /nologo /subsystem:windows > pageant.rsp
	echo $(PAGE1) >> pageant.rsp
	echo $(PAGE2) >> pageant.rsp
	echo $(PAGERC) >> pageant.rsp
	echo $(LIBS1) >> pageant.rsp
	echo $(LIBS2) >> pageant.rsp
	echo $(LIBS3) >> pageant.rsp

puttygen.rsp: makefile
	echo /nologo /subsystem:windows > puttygen.rsp
	echo $(GEN1) >> puttygen.rsp
	echo $(GEN2) >> puttygen.rsp
	echo $(GEN3) >> puttygen.rsp
	echo $(GEN4) >> puttygen.rsp
	echo $(GENRC) >> puttygen.rsp
	echo $(LIBS1) >> puttygen.rsp
	echo $(LIBS2) >> puttygen.rsp
	echo $(LIBS3) >> puttygen.rsp

pscp.rsp: makefile
	echo /nologo /subsystem:console > pscp.rsp
	echo $(SOBJS) >> pscp.rsp
	echo $(MOBJS) >> pscp.rsp
	echo $(MOBJ2) >> pscp.rsp
	echo $(OBJS1) >> pscp.rsp
	echo $(OBJS2) >> pscp.rsp
	echo $(OBJS3) >> pscp.rsp
	echo $(OBJS4) >> pscp.rsp
	echo $(SRESRC) >> pscp.rsp
	echo $(LIBS1) >> pscp.rsp
	echo $(LIBS2) >> pscp.rsp
	echo $(SOCK1) >> pscp.rsp

plink.rsp: makefile
	echo /nologo /subsystem:console > plink.rsp
	echo $(LOBJS1) >> plink.rsp
	echo $(POBJS) >> plink.rsp
	echo $(PLOBJS) >> plink.rsp
	echo $(MOBJS) >> plink.rsp
	echo $(MOBJ2) >> plink.rsp
	echo $(OBJS1) >> plink.rsp
	echo $(OBJS2) >> plink.rsp
	echo $(OBJS3) >> plink.rsp
	echo $(OBJS4) >> plink.rsp
	echo $(LRESRC) >> plink.rsp
	echo $(LIBS1) >> plink.rsp
 	echo $(LIBS2) >> plink.rsp
 	echo $(SOCK2) >> plink.rsp

##-- dependencies
window.$(OBJ): window.c putty.h puttymem.h network.h win_res.h storage.h winstuff.h
windlg.$(OBJ): windlg.c putty.h puttymem.h network.h ssh.h win_res.h winstuff.h
winctrls.$(OBJ): winctrls.c winstuff.h winstuff.h
settings.$(OBJ): settings.c putty.h puttymem.h network.h storage.h
winstore.$(OBJ): winstore.c putty.h puttymem.h network.h storage.h
terminal.$(OBJ): terminal.c putty.h puttymem.h network.h
sizetip.$(OBJ): sizetip.c putty.h puttymem.h network.h winstuff.h
telnet.$(OBJ): telnet.c putty.h puttymem.h network.h
raw.$(OBJ): raw.c putty.h puttymem.h network.h
rlogin.$(OBJ): rlogin.c putty.h puttymem.h network.h
xlat.$(OBJ): xlat.c putty.h puttymem.h network.h
ldisc.$(OBJ): ldisc.c putty.h puttymem.h network.h
misc.$(OBJ): misc.c putty.h puttymem.h network.h
noise.$(OBJ): noise.c putty.h puttymem.h network.h ssh.h storage.h
ssh.$(OBJ): ssh.c ssh.h putty.h puttymem.h network.h tree234.h
sshcrc.$(OBJ): sshcrc.c ssh.h puttymem.h
sshdes.$(OBJ): sshdes.c ssh.h puttymem.h
sshmd5.$(OBJ): sshmd5.c ssh.h puttymem.h
sshrsa.$(OBJ): sshrsa.c ssh.h puttymem.h
sshsha.$(OBJ): sshsha.c ssh.h puttymem.h
sshrand.$(OBJ): sshrand.c ssh.h puttymem.h
sshblowf.$(OBJ): sshblowf.c ssh.h puttymem.h
sshdh.$(OBJ): sshdh.c ssh.h puttymem.h
sshdss.$(OBJ): sshdss.c ssh.h puttymem.h
sshbn.$(OBJ): sshbn.c ssh.h puttymem.h
sshpubk.$(OBJ): sshpubk.c ssh.h puttymem.h
sshzlib.$(OBJ): sshzlib.c ssh.h puttymem.h
scp.$(OBJ): scp.c putty.h puttymem.h network.h winstuff.h
version.$(OBJ): version.c
be_all.$(OBJ): be_all.c
be_nossh.$(OBJ): be_nossh.c
be_none.$(OBJ): be_none.c
plink.$(OBJ): plink.c putty.h puttymem.h network.h winstuff.h
pageant.$(OBJ): pageant.c ssh.h puttymem.h tree234.h
pageantc.$(OBJ): pageantc.c puttymem.h
tree234.$(OBJ): tree234.c tree234.h puttymem.h
puttygen.$(OBJ): puttygen.c putty.h ssh.h winstuff.h
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
scp.$(RES): scp.rc scp.ico
##--
scp.$(RES):
	rc $(FWHACK) $(RCFL) -r -DWIN32 -D_WIN32 -DWINVER=0x0400 scp.rc

##-- dependencies
pageant.$(RES): pageant.rc pageant.ico pageants.ico
##--
pageant.$(RES):
	rc $(FWHACK) $(RCFL) -r -DWIN32 -D_WIN32 -DWINVER=0x0400 pageant.rc

##-- dependencies
puttygen.$(RES): puttygen.rc puttygen.ico
##--
puttygen.$(RES):
	rc $(FWHACK) $(RCFL) -r -DWIN32 -D_WIN32 -DWINVER=0x0400 puttygen.rc

clean: tidy
	del *.exe

tidy:
	del *.obj
	del *.res
	del *.pch
	del *.aps
	del *.ilk
	del *.pdb
	del *.rsp
	del *.dsp
	del *.dsw
	del *.ncb
	del *.opt
	del *.plg
	del *.map
