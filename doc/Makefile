CHAPTERS = blurb intro gs using config pscp psftp plink pubkey pageant faq

INPUTS = $(patsubst %,%.but,$(CHAPTERS))

# This is temporary. Hack it locally or something.
BUTTRESS = $(HOME)/src/buttress/build/buttress

Contents.html: $(INPUTS)
	$(HOME)/src/buttress/build/buttress $(INPUTS)
	rm -f index.html
	ln -s Contents.html index.html
	mv output.txt puttydoc.txt
	mv output.hlp putty.hlp
	mv output.cnt putty.cnt

clean:
	rm -f *.html *.txt *.hlp *.cnt
