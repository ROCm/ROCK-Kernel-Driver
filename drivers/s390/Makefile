#
# Makefile for the linux i386-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

all: io.o

CFLAGS += 
O_TARGET := io.o

obj-y	:= s390io.o s390mach.o s390dyn.o idals.o ccwcache.o

subdir-y := block char misc net
subdir-m := $(subdir-y)

obj-y += block/s390-block.o \
  char/s390-char.o \
  misc/s390-misc.o \
  net/s390-net.o 

io.o: $(obj-y)

block/s390-block.o: dummy
	$(MAKE)  -C block

char/s390-char.o: dummy
	$(MAKE)  -C char

misc/s390-misc.o: dummy
	$(MAKE) -C misc

net/s390-net.o: dummy
	$(MAKE) -C net

include $(TOPDIR)/Rules.make
