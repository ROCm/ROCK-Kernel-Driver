all: silo

silo.o: silo.c
	$(CC) -c -o silo.o -O2 silo.c

cfg.o: cfg.c
	$(CC) -c -o cfg.o -O2 cfg.c

silo: silo.o cfg.o
	$(CC) -o $@ $^
	$(STRIP) $@

clean:
	rm -f *.o silo

