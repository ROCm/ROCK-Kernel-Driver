#KERNELSRC=/usr/src/linux
EXTRA_CFLAGS = -g #-fno-inline

hfsplus-objs := super.o options.o inode.o extents.o catalog.o dir.o btree.o \
		bnode.o brec.o bfind.o tables.o unicode.o wrapper.o bitmap.o part_tbl.o

ifeq ($(PATCHLEVEL),4)
O_TARGET	:= hfsplus.o
obj-y		:= $(hfsplus-objs)
endif

obj-$(CONFIG_HFSPLUS_FS) += hfsplus.o

ifeq ("$(TOPDIR)","")
ifeq ("$(KERNELSRC)","")
all install:
	@echo 'use "make KERNELSRC=..."'
else
all:
	make modules -C $(KERNELSRC) SUBDIRS=$$PWD CONFIG_HFSPLUS_FS=m

install:
	@eval `sed -n '1,5s/^\([A-Z]*\) *= *\(.*\)$$/\1=\2/p' $(KERNELSRC)/Makefile`; \
	KERNELRELEASE=$$VERSION.$$PATCHLEVEL.$$SUBLEVEL$$EXTRAVERSION; \
	mkdir -pv $(INSTALL_MOD_PATH)/lib/modules/$$KERNELRELEASE/kernel/fs/hfsplus; \
	mod=hfsplus.o; test -f hfsplus.ko && mod=hfsplus.ko; \
	cp -v $$mod $(INSTALL_MOD_PATH)/lib/modules/$$KERNELRELEASE/kernel/fs/hfsplus


endif
endif

-include $(TOPDIR)/Rules.make

clean:
	rm -f hfsplus.o hfsplus.ko $(hfsplus-objs) .*.flags .*.cmd *.mod.?
