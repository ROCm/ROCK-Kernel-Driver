#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2000 Silicon Graphics, Inc.
# Copyright (C) Jack Steiner (steiner@sgi.com)
#

TOPDIR=../../../..
HPATH           = $(TOPDIR)/include

LIB     = ../../lib/lib.a

OBJ=fpromasm.o main.o  fw-emu.o fpmem.o
obj-y=fprom

fprom: $(OBJ)
	$(LD) -static -Tfprom.lds -o fprom $(OBJ) $(LIB)

comma	:= ,

.S.o:
	$(CC)  -D__ASSEMBLY__ $(AFLAGS) $(AFLAGS_KERNEL) -c -o $*.o $<
.c.o:
	$(CC)  $(CFLAGS) -DKBUILD_BASENAME=$(subst $(comma),_,$(subst -,_,$(*F))) $(CFLAGS_KERNEL) -c -o $*.o $<

clean:
	rm -f *.o fprom


include $(TOPDIR)/Rules.make

