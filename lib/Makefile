#
# Makefile for some libs needed in the kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

L_TARGET := lib.a

obj-y := errno.o ctype.o string.o vsprintf.o cmdline.o \
	 bust_spinlocks.o rbtree.o radix-tree.o dump_stack.o \
	 kobject.o idr.o

obj-$(CONFIG_RWSEM_GENERIC_SPINLOCK) += rwsem-spinlock.o
obj-$(CONFIG_RWSEM_XCHGADD_ALGORITHM) += rwsem.o
obj-$(CONFIG_SMP) += percpu_counter.o

ifneq ($(CONFIG_HAVE_DEC_LOCK),y) 
  obj-y += dec_and_lock.o
endif

obj-$(CONFIG_CRC32)	+= crc32.o

obj-$(CONFIG_ZLIB_INFLATE) += zlib_inflate/
obj-$(CONFIG_ZLIB_DEFLATE) += zlib_deflate/

include $(TOPDIR)/drivers/net/Makefile.lib
include $(TOPDIR)/drivers/usb/Makefile.lib
include $(TOPDIR)/fs/Makefile.lib
include $(TOPDIR)/net/bluetooth/bnep/Makefile.lib

host-progs := gen_crc32table
clean-files := crc32table.h

$(obj)/crc32.o: $(obj)/crc32table.h

$(obj)/crc32table.h: $(obj)/gen_crc32table
	./$< > $@
