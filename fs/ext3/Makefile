#
# Makefile for the linux ext3-filesystem routines.
#

obj-$(CONFIG_EXT3_FS) += ext3.o

ext3-objs    := balloc.o bitmap.o dir.o file.o fsync.o ialloc.o inode.o \
		ioctl.o namei.o super.o symlink.o hash.o

ifeq ($(CONFIG_EXT3_FS_XATTR),y)
ext3-objs += xattr.o xattr_user.o xattr_trusted.o
endif

ifeq ($(CONFIG_EXT3_FS_POSIX_ACL),y)
ext3-objs += acl.o
endif

ifeq ($(CONFIG_EXT3_FS_SECURITY),y)
ext3-objs += xattr_security.o
endif
