#
# reiser4/Makefile
#

obj-$(CONFIG_REISER4_FS) += reiser4.o

EXTRA_CFLAGS += \
           -Wformat \
	       -Wundef \
           -Wunused \
	       -Wcomment \
           \
	       -Wno-nested-externs \
	       -Wno-write-strings \
	       -Wno-sign-compare

#	       -Wpointer-arith \
#	       -Wlarger-than-16384 \
#	       -Winline \

ifeq ($(CONFIG_REISER4_NOOPT),y)
	EXTRA_CFLAGS += -O0 -fno-inline
else
# this warning is only supported when optimization is on.
	EXTRA_CFLAGS += \
           -Wuninitialized
endif

ifeq ($(CONFIG_REISER4_ALL_IN_ONE),y)

reiser4-objs := all-reiser4.o

else

reiser4-objs := \
		   debug.o \
		   stats.o \
		   jnode.o \
		   znode.o \
		   key.o \
		   pool.o \
		   tree_mod.o \
		   estimate.o \
		   carry.o \
		   carry_ops.o \
		   lock.o \
		   tree.o \
		   context.o \
		   tap.o \
		   coord.o \
		   block_alloc.o \
		   txnmgr.o \
		   kassign.o \
		   flush.o \
		   wander.o \
		   eottl.o \
		   search.o \
		   page_cache.o \
		   lnode.o \
		   kcond.o \
		   seal.o \
		   dscale.o \
		   log.o \
		   flush_queue.o \
		   ktxnmgrd.o \
		   kattr.o \
		   blocknrset.o \
		   super.o \
		   oid.o \
		   tree_walk.o \
		   inode.o \
		   vfs_ops.o \
		   inode_ops.o \
		   file_ops.o \
		   as_ops.o \
		   emergency_flush.o \
		   spinprof.o\
		   entd.o\
		   readahead.o \
		   crypt.o \
		   diskmap.o \
		   prof.o \
		   repacker.o \
		   status_flags.o \
		   init_super.o \
		   safe_link.o \
           \
		   plugin/plugin.o \
		   plugin/plugin_set.o \
		   plugin/node/node.o \
		   plugin/object.o \
		   plugin/symlink.o \
		   plugin/cryptcompress.o \
		   plugin/digest.o \
		   plugin/node/node40.o \
           \
		   plugin/compress/minilzo.o \
		   plugin/compress/compress.o \
           \
		   plugin/item/static_stat.o \
		   plugin/item/sde.o \
		   plugin/item/cde.o \
		   plugin/item/blackbox.o \
		   plugin/item/internal.o \
		   plugin/item/tail.o \
		   plugin/item/ctail.o \
		   plugin/item/extent.o \
		   plugin/item/extent_item_ops.o \
		   plugin/item/extent_file_ops.o \
		   plugin/item/extent_flush_ops.o \
		   plugin/item/extent_repack_ops.o \
           \
		   plugin/hash.o \
		   plugin/fibration.o \
		   plugin/tail_policy.o \
		   plugin/item/item.o \
           \
		   plugin/dir/hashed_dir.o \
		   plugin/dir/pseudo_dir.o \
		   plugin/dir/dir.o \
           \
		   plugin/security/perm.o \
           \
		   plugin/pseudo/pseudo.o \
           \
		   plugin/space/bitmap.o \
           \
		   plugin/disk_format/disk_format40.o \
		   plugin/disk_format/disk_format.o \
           \
		   plugin/file/pseudo.o \
		   plugin/file/file.o \
		   plugin/file/tail_conversion.o

reiser4-objs += sys_reiser4.o

ifeq ($(CONFIG_REISER4_FS_SYSCALL),y)

  ifeq ($(CONFIG_REISER4_FS_SYSCALL_YACC),y)

      YFLAGS= -d -v -r -b $(obj)/parser/parser

   $(obj)/parser/parser.code.c: $(obj)/parser/parser.y

	$(YACC) $(YFLAGS) $(obj)/parser/parser.y

  endif

  sys_reiser4.o: $/sys_reiser4.c       \
                 $/parser/parser.code.c \
                 $/parser/parser.tab.c \
                 $/parser/parser.tab.h \
                 $/parser/lib.c        \
                 $/parser/pars.cls.h   \
                 $/parser/pars.yacc.h  \
                 $/parser/parser.h


#	$(MAKE)  $(obj)/parser/parser
#clean-files := parser/parser.code.c
##clean-rule =@$(MAKE) -C $/parser clean
#clean-rule =@$(MAKE) $(obj)/parser/parser.code.c
endif

endif

