#
# linux/arch/arm/boot/compressed/Makefile
#
# create a compressed vmlinuz image from the original vmlinux
#
# Note! SYSTEM, ZTEXTADDR, ZBSSADDR and ZRELADDR are now exported
# from arch/arm/boot/Makefile
#

HEAD		 = head.o
OBJS		 = misc.o
CFLAGS		 = $(CPPFLAGS) -O2 -DSTDC_HEADERS $(CFLAGS_PROC)
FONTC		 = $(TOPDIR)/drivers/video/font_acorn_8x8.c
ZLDFLAGS	 = -p -X -T vmlinux.lds

#
# Architecture dependencies
#
ifeq ($(CONFIG_ARCH_ACORN),y)
OBJS		+= ll_char_wr.o font.o
ZLDFLAGS	+= -defsym params=$(PARAMS_PHYS)
endif

ifeq ($(CONFIG_ARCH_NETWINDER),y)
OBJS		+= head-netwinder.o
endif

ifeq ($(CONFIG_ARCH_INTEGRATOR),y)
OBJS		+= head-netwinder.o
endif

ifeq ($(CONFIG_ARCH_NEXUSPCI),y)
HEAD		 = head-nexuspci.o
endif

ifeq ($(CONFIG_ARCH_L7200),y)
OBJS		+= head-l7200.o
endif

ifeq ($(CONFIG_ARCH_P720T),y)
# Borrow this code from SA1100
OBJS		+= head-sa1100.o
endif

ifeq ($(CONFIG_ARCH_SA1100),y)
OBJS		+= head-sa1100.o setup-sa1100.o
ifeq ($(CONFIG_SA1100_NANOENGINE),y)
  OBJS += hw-bse.o
endif
endif

SEDFLAGS	= s/TEXT_START/$(ZTEXTADDR)/;s/LOAD_ADDR/$(ZRELADDR)/;

ifneq ($(ZBSSADDR),)
SEDFLAGS	+= s/BSS_START/$(ZBSSADDR)/
else
SEDFLAGS	+= s/BSS_START/ALIGN(4)/
endif

all:		vmlinux

vmlinux:	$(HEAD) $(OBJS) piggy.o vmlinux.lds
		$(LD) $(ZLDFLAGS) $(HEAD) $(OBJS) piggy.o $(LIBGCC) -o vmlinux

$(HEAD): 	$(HEAD:.o=.S)
		$(CC) $(AFLAGS) -traditional -c $(HEAD:.o=.S)

piggy.o:	$(SYSTEM)
		$(OBJCOPY) $(SYSTEM) piggy
		gzip $(GZFLAGS) < piggy > piggy.gz
		$(LD) -r -o $@ -b binary piggy.gz
		rm -f piggy piggy.gz

font.o:		$(FONTC)
		$(CC) $(CFLAGS) -Dstatic= -c -o $@ $(FONTC)

vmlinux.lds:	vmlinux.lds.in
		@sed "$(SEDFLAGS)" < vmlinux.lds.in > $@

clean:;		rm -f vmlinux core piggy* vmlinux.lds

.PHONY:	vmlinux.lds clean

misc.o: misc.c $(TOPDIR)/include/asm/arch/uncompress.h $(TOPDIR)/lib/inflate.c

%.o: %.S
	$(CC) $(AFLAGS) $(EXTRA_AFLAGS) $(AFLAGS_$@) -c -o $@ $<
