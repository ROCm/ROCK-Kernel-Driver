#
# Makefile for the DECstation family specific parts of the kernel
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: dec.o
O_TARGET := dec.o
O_OBJS	 := int-handler.o setup.o irq.o time.o reset.o rtc-dec.o

ifdef CONFIG_PROM_CONSOLE
O_OBJS   += promcon.o
endif

ifdef CONFIG_SERIAL
O_OBJS   += serial.o
endif

ifeq ($(CONFIG_MODULES),y)
 OX_OBJS  = wbflush.o
else
 O_OBJS   += wbflush.o
endif

int-handler.o:	int-handler.S

clean:

include $(TOPDIR)/Rules.make
