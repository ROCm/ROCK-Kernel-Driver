#
# linux/arch/arm/boot/compressed/Makefile
#
# create a compressed vmlinuz image from the original vmlinux
#
# Note! ZTEXTADDR, ZBSSADDR and ZRELADDR are now exported
# from arch/arm/boot/Makefile
#

HEAD	= head.o
OBJS	= misc.o
FONTC	= drivers/video/console/font_acorn_8x8.c

#
# Architecture dependencies
#
ifeq ($(CONFIG_ARCH_ACORN),y)
OBJS		+= ll_char_wr.o font.o
CFLAGS_misc.o	:= -DPARAMS_PHYS=$(PARAMS_PHYS)
endif

ifeq ($(CONFIG_ARCH_SHARK),y)
OBJS		+= head-shark.o ofw-shark.o
endif

ifeq ($(CONFIG_ARCH_CAMELOT),y)
OBJS		+= head-epxa10db.o
endif

ifeq ($(CONFIG_ARCH_FTVPCI),y)
OBJS		+= head-ftvpci.o
endif

ifeq ($(CONFIG_ARCH_L7200),y)
OBJS		+= head-l7200.o
endif

ifeq ($(CONFIG_ARCH_CLPS7500),y)
HEAD		= head-clps7500.o
endif

ifeq ($(CONFIG_ARCH_P720T),y)
# Borrow this code from SA1100
OBJS		+= head-sa1100.o
endif

ifeq ($(CONFIG_ARCH_SA1100),y)
OBJS		+= head-sa1100.o
ifeq ($(CONFIG_SA1100_NANOENGINE),y)
  OBJS += hw-bse.o
endif
endif

ifeq ($(CONFIG_CPU_XSCALE),y)
OBJS		+= head-xscale.o
endif

SEDFLAGS	= s/TEXT_START/$(ZTEXTADDR)/;s/LOAD_ADDR/$(ZRELADDR)/;s/BSS_START/$(ZBSSADDR)/

targets       := vmlinux vmlinux.lds piggy piggy.gz piggy.o \
                 font.o head.o $(OBJS)
EXTRA_CFLAGS  := -fpic
EXTRA_AFLAGS  := -traditional

LDFLAGS_vmlinux := -p -X \
	$(shell $(CC) $(CFLAGS) --print-libgcc-file-name) -T

$(obj)/vmlinux: $(obj)/vmlinux.lds $(obj)/$(HEAD) $(obj)/piggy.o \
	 	$(addprefix $(obj)/, $(OBJS)) FORCE
	$(call if_changed,ld)
	@:


$(obj)/piggy: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/piggy.gz: $(obj)/piggy FORCE
	$(call if_changed,gzip)

LDFLAGS_piggy.o := -r -b binary
$(obj)/piggy.o:  $(obj)/piggy.gz FORCE
	$(call if_changed,ld)

CFLAGS_font.o := -Dstatic=
$(obj)/font.o: $(FONTC)

$(obj)/vmlinux.lds: $(obj)/vmlinux.lds.in Makefile arch/arm/boot/Makefile .config
	@sed "$(SEDFLAGS)" < $< > $@

$(obj)/misc.o: $(obj)/misc.c include/asm/arch/uncompress.h lib/inflate.c

