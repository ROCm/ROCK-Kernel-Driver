#
# This is a simple Makefile to test some of the RAID-6 code
# from userspace.
#

CC	= gcc
CFLAGS	= -I.. -O2 -g -march=i686
LD	= ld
PERL	= perl

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

%.c: ../%.c
	cp -f $< $@

%.uc: ../%.uc
	cp -f $< $@

%.pl: ../%.pl
	cp -f $< $@

all:	raid6.o raid6test

raid6.o: raid6int1.o raid6int2.o raid6int4.o raid6int8.o raid6int16.o \
	 raid6mmx.o raid6sse1.o raid6sse2.o \
	 raid6recov.o raid6algos.o \
	 raid6tables.o
	$(LD) -r -o $@ $^

raid6test: raid6.o test.c
	$(CC) $(CFLAGS) -o raid6test $^

raid6int1.c: raid6int.uc unroller.pl
	$(PERL) ./unroller.pl 1 < raid6int.uc > $@

raid6int2.c: raid6int.uc unroller.pl
	$(PERL) ./unroller.pl 2 < raid6int.uc > $@

raid6int4.c: raid6int.uc unroller.pl
	$(PERL) ./unroller.pl 4 < raid6int.uc > $@

raid6int8.c: raid6int.uc unroller.pl
	$(PERL) ./unroller.pl 8 < raid6int.uc > $@

raid6int16.c: raid6int.uc unroller.pl
	$(PERL) ./unroller.pl 16 < raid6int.uc > $@

raid6tables.c: mktables
	./mktables > raid6tables.c

clean:
	rm -f *.o mktables mktables.c raid6int.uc raid6*.c raid6test

spotless: clean
	rm -f *~
