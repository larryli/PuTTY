CHAPTERS := $(SITE) blurb intro gs using config pscp psftp plink pubkey
CHAPTERS += pageant errors faq feedback licence

INPUTS = $(patsubst %,%.but,$(CHAPTERS))

# This is temporary. Hack it locally or something.
HALIBUT = $(HOME)/src/halibut/build/halibut

DROPPINGS := output.txt output.hlp output.cnt output.1
DROPPINGS += Contents.html IndexPage.html Chapter*.html Section*.html
DROPPINGS += Appendix*.html

all: man Contents.html

Contents.html: $(INPUTS)
	$(HALIBUT) $(INPUTS)
	rm -f index.html
	ln -s Contents.html index.html
	mv output.txt puttydoc.txt
	mv output.hlp putty.hlp
	mv output.cnt putty.cnt

MKMAN = $(HALIBUT) mancfg.but $< && mv output.1 $@ && rm -f $(DROPPINGS)
MANPAGES = putty.1 puttygen.1 plink.1 pscp.1 psftp.1 puttytel.1 pterm.1
man: $(MANPAGES)

putty.1: man-putt.but mancfg.but; $(MKMAN)
puttygen.1: man-pg.but mancfg.but; $(MKMAN)
plink.1: man-pl.but mancfg.but; $(MKMAN)
pscp.1: man-pscp.but mancfg.but; $(MKMAN)
psftp.1: man-psft.but mancfg.but; $(MKMAN)
puttytel.1: man-ptel.but mancfg.but; $(MKMAN)
pterm.1: man-pter.but mancfg.but; $(MKMAN)

clean:
	rm -f *.html *.txt *.hlp *.cnt *.1
