CHAPTERS = blurb intro gs config pscp plink pubkey pageant trouble

INPUTS = $(patsubst %,%.but,$(CHAPTERS))

# This is temporary. Hack it locally or something.
BUTTRESS = $(HOME)/src/buttress/build/buttress

Contents.html: $(INPUTS)
	$(HOME)/src/buttress/build/buttress $(INPUTS)
	rm -f index.html
	ln -s Contents.html index.html
