#
# Makefile for the linux kernel.
#
# Reuse any files we can from the H8S
#

#VPATH := $(VPATH):$(BOARD)

# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -I. -c $< -o $*.o

obj-y := entry.o ints.o

$(BOARD)/crt0_$(MODEL).o: $(BOARD)/crt0_$(MODEL).S

entry.o: entry.S

ints.o: ints.c
