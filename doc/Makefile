CHAPTERS := $(SITE) blurb intro gs using config pscp psftp plink pubkey
CHAPTERS += pageant errors faq feedback licence

INPUTS = $(patsubst %,%.but,$(CHAPTERS))

# This is temporary. Hack it locally or something.
HALIBUT = $(HOME)/src/halibut/build/halibut

Contents.html: $(INPUTS)
	$(HOME)/src/halibut/build/halibut $(INPUTS)
	rm -f index.html
	ln -s Contents.html index.html
	mv output.txt puttydoc.txt
	mv output.hlp putty.hlp
	mv output.cnt putty.cnt

clean:
	rm -f *.html *.txt *.hlp *.cnt
