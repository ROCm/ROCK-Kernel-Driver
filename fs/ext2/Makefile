#
# Makefile for the linux ext2-filesystem routines.
#

obj-$(CONFIG_EXT2_FS) += ext2.o

ext2-objs := balloc.o bitmap.o dir.o file.o fsync.o ialloc.o inode.o \
	     ioctl.o namei.o super.o symlink.o

ifeq ($(CONFIG_EXT2_FS_XATTR),y)
ext2-objs += xattr.o xattr_user.o xattr_trusted.o
endif

ifeq ($(CONFIG_EXT2_FS_POSIX_ACL),y)
ext2-objs += acl.o
endif

ifeq ($(CONFIG_EXT2_FS_SECURITY),y)
ext2-objs += xattr_security.o
endif
