all: silo

silo.o: silo.c
	$(CROSS_COMPILE)gcc -c -o silo.o -O2 silo.c

cfg.o: cfg.c
	$(CROSS_COMPILE)gcc -c -o cfg.o -O2 cfg.c

silo: silo.o cfg.o
	$(CROSS_COMPILE)gcc -o $@ $^
	$(STRIP) $@

clean:
	rm -f *.o silo

