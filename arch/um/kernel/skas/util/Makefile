all: mk_ptregs

mk_ptregs : mk_ptregs.o
	$(HOSTCC) -o mk_ptregs mk_ptregs.o

mk_ptregs.o : mk_ptregs.c
	$(HOSTCC) -c $< 

clean : 
	$(RM) -f mk_ptregs *.o *~
