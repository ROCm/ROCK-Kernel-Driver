all: mk_ptregs

mk_ptregs : mk_ptregs.o
	$(CC) -o mk_ptregs mk_ptregs.o

mk_ptregs.o : mk_ptregs.c
	$(CC) -c $< 

clean : 
	$(RM) -f mk_ptregs *.o *~
