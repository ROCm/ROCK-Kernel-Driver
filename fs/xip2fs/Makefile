#
# Makefile for the linux xip2-filesystem routines.
#

obj-$(CONFIG_XIP2_FS) += xip2.o

xip2-y := balloc.o bitmap.o dir.o file.o  ialloc.o inode.o \
	  ioctl.o namei.o super.o symlink.o

xip2-$(CONFIG_XIP2_FS_XATTR)	 += xattr.o xattr_user.o xattr_trusted.o
xip2-$(CONFIG_XIP2_FS_POSIX_ACL) += acl.o
xip2-$(CONFIG_XIP2_FS_SECURITY)	 += xattr_security.o
