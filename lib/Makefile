#
# Makefile for some libs needed in the kernel.
#


lib-y := errno.o ctype.o string.o vsprintf.o cmdline.o \
	 bust_spinlocks.o rbtree.o radix-tree.o dump_stack.o \
	 kobject.o idr.o div64.o parser.o int_sqrt.o \
	 bitmap.o extable.o

# hack for now till some static code uses krefs, then it can move up above...
obj-y += kref.o

lib-$(CONFIG_RWSEM_GENERIC_SPINLOCK) += rwsem-spinlock.o
lib-$(CONFIG_RWSEM_XCHGADD_ALGORITHM) += rwsem.o

ifneq ($(CONFIG_HAVE_DEC_LOCK),y) 
  lib-y += dec_and_lock.o
endif

obj-$(CONFIG_CRC32)	+= crc32.o
obj-$(CONFIG_LIBCRC32C)	+= libcrc32c.o

obj-$(CONFIG_ZLIB_INFLATE) += zlib_inflate/
obj-$(CONFIG_ZLIB_DEFLATE) += zlib_deflate/

host-progs	:= gen_crc32table
clean-files	:= crc32table.h

$(obj)/crc32.o: $(obj)/crc32table.h

quiet_cmd_crc32 = GEN     $@
      cmd_crc32 = $< > $@

$(obj)/crc32table.h: $(obj)/gen_crc32table
	$(call cmd,crc32)
