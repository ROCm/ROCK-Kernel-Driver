all: dasdfmt

dasdfmt: dasdfmt.c
	$(CROSS_COMPILE)gcc -o $@ $^
	$(STRIP) $@

clean:
	rm -f dasdfmt

