#
# arch/ppc/boot/common/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Tom Rini	January 2001
#

.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<
.S.s:
	$(CPP) $(AFLAGS) -traditional -o $*.o $<
.S.o:
	$(CC) $(AFLAGS) -traditional -c -o $*.o $<

OBJCOPY_ARGS = -O elf32-powerpc

coffcrt0.o:
	$(CC) $(AFLAGS) -DXCOFF -traditional -c -o coffcrt0.o crt0.S

include $(TOPDIR)/Rules.make
