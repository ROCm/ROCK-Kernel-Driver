all: mk_ptregs

mk_ptregs : mk_ptregs.o
	$(HOSTCC) -o mk_ptregs mk_ptregs.o

mk_ptregs.o : mk_ptregs.c
	$(HOSTCC) -c $<

clean-files := mk_ptregs *.o *~
