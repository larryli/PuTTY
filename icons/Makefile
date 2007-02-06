# Makefile for the PuTTY icon suite.

ICONS = putty puttycfg puttygen pscp pageant pterm ptermcfg puttyins
SIZES = 16 32 48

MODE = # override to -it on command line for opaque testing

PNGS = $(foreach I,$(ICONS),$(foreach S,$(SIZES),$(I)-$(S).png))
MONOPNGS = $(foreach I,$(ICONS),$(foreach S,$(SIZES),$(I)-$(S)-mono.png))
TRUEPNGS = $(foreach I,$(ICONS),$(foreach S,$(SIZES),$(I)-$(S)-true.png))

ICOS = putty.ico puttygen.ico pscp.ico pageant.ico pageants.ico puttycfg.ico \
       puttyins.ico
CICONS = xpmputty.c xpmpucfg.c xpmpterm.c xpmptcfg.c

base: icos cicons

all: pngs monopngs base # truepngs currently disabled by default

pngs: $(PNGS)
monopngs: $(MONOPNGS)
truepngs: $(TRUEPNGS)

icos: $(ICOS)
cicons: $(CICONS)

install: icos cicons
	cp $(ICOS) ../windows
	cp $(CICONS) ../unix

$(PNGS): %.png: mkicon.py
	./mkicon.py $(MODE) $(join $(subst -, ,$(basename $@)),_icon) $@

$(MONOPNGS): %.png: mkicon.py
	./mkicon.py -2 $(MODE) $(join $(subst -, ,$(subst -mono,,$(basename $@))),_icon) $@

$(TRUEPNGS): %.png: mkicon.py
	./mkicon.py -T $(MODE) $(join $(subst -, ,$(subst -true,,$(basename $@))),_icon) $@

putty.ico: putty-16.png putty-32.png putty-48.png \
           putty-16-mono.png putty-32-mono.png putty-48-mono.png
	./icon.pl -4 $(filter-out %-mono.png, $^) -1 $(filter %-mono.png, $^) > $@

puttycfg.ico: puttycfg-16.png puttycfg-32.png puttycfg-48.png \
              puttycfg-16-mono.png puttycfg-32-mono.png puttycfg-48-mono.png
	./icon.pl -4 $(filter-out %-mono.png, $^) -1 $(filter %-mono.png, $^) > $@

puttygen.ico: puttygen-16.png puttygen-32.png puttygen-48.png \
              puttygen-16-mono.png puttygen-32-mono.png puttygen-48-mono.png
	./icon.pl -4 $(filter-out %-mono.png, $^) -1 $(filter %-mono.png, $^) > $@

pageant.ico: pageant-16.png pageant-32.png pageant-48.png \
             pageant-16-mono.png pageant-32-mono.png pageant-48-mono.png
	./icon.pl -4 $(filter-out %-mono.png, $^) -1 $(filter %-mono.png, $^) > $@

pageants.ico: pageant-16.png pageant-16-mono.png
	./icon.pl -4 $(filter-out %-mono.png, $^) -1 $(filter %-mono.png, $^) > $@

pscp.ico: pscp-16.png pscp-32.png pscp-48.png \
          pscp-16-mono.png pscp-32-mono.png pscp-48-mono.png
	./icon.pl -4 $(filter-out %-mono.png, $^) -1 $(filter %-mono.png, $^) > $@

# Because the installer icon makes heavy use of brown when drawing
# the cardboard box, it's worth having 8-bit versions of it in
# addition to the 4- and 1-bit ones.
puttyins.ico: puttyins-16.png puttyins-32.png puttyins-48.png \
              puttyins-16-mono.png puttyins-32-mono.png \
              puttyins-48-mono.png \
              puttyins-16-true.png puttyins-32-true.png \
              puttyins-48-true.png
	./icon.pl -8 $(filter %-true.png, $^) \
                  -4 $(filter-out %-true.png, $(filter-out %-mono.png, $^)) \
                  -1 $(filter %-mono.png, $^) > $@

# Icon for the website. (This isn't linked into "make all".)
website.ico: putty-16.png
	./icon.pl -4 $^ >$@

xpmputty.c: putty-16.png putty-32.png putty-48.png
	./cicon.pl main_icon $^ > $@

xpmpucfg.c: puttycfg-16.png puttycfg-32.png puttycfg-48.png
	./cicon.pl cfg_icon $^ > $@

xpmpterm.c: pterm-16.png pterm-32.png pterm-48.png
	./cicon.pl main_icon $^ > $@

xpmptcfg.c: ptermcfg-16.png ptermcfg-32.png ptermcfg-48.png
	./cicon.pl cfg_icon $^ > $@

clean:
	rm -f *.png *.ico *.c
