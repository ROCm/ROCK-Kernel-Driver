#
# Makefile for the linux reiser-filesystem routines.
#

obj-$(CONFIG_REISERFS_FS) += reiserfs.o

reiserfs-objs := bitmap.o do_balan.o namei.o inode.o file.o dir.o fix_node.o \
		 super.o prints.o objectid.o lbalance.o ibalance.o stree.o \
		 hashes.o tail_conversion.o journal.o resize.o \
		 item_ops.o ioctl.o procfs.o

ifeq ($(CONFIG_REISERFS_FS_XATTR),y)
reiserfs-objs += xattr.o xattr_user.o xattr_trusted.o
endif

ifeq ($(CONFIG_REISERFS_FS_SECURITY),y)
reiserfs-objs += xattr_security.o
endif

ifeq ($(CONFIG_REISERFS_FS_POSIX_ACL),y)
reiserfs-objs += xattr_acl.o
endif

ifeq ($(CONFIG_REISERFS_CC_REDUCE_OPTIMZE),y)
EXTRA_CFLAGS := -O1
endif

TAGS:
	etags *.c

