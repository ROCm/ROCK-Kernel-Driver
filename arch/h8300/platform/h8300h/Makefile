#
# Makefile for the linux kernel.
#
# Reuse any files we can from the H8/300H
#

#VPATH := $(VPATH):$(BOARD)

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -I. -c $< -o $*.o

obj-y := entry.o ints.o

$(BOARD)/crt0_$(MODEL).o: $(BOARD)/crt0_$(MODEL).S

entry.o: entry.S

ints.o: ints.c
