#
# reiser4/Makefile
#

obj-$(CONFIG_REISER4_FS) += reiser4.o

reiser4-y := \
		   debug.o \
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
		   kcond.o \
		   seal.o \
		   dscale.o \
		   flush_queue.o \
		   ktxnmgrd.o \
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
		   entd.o\
		   readahead.o \
		   cluster.o \
		   crypt.o \
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
		   plugin/compress/compress_mode.o \
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
		   plugin/file/regular.o \
		   plugin/file/tail_conversion.o
