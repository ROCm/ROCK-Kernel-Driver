#
# Makefile for the Linux /dev/pts virtual filesystem.
#

obj-$(CONFIG_DEVPTS_FS) += devpts.o

devpts-objs := inode.o

ifeq ($(CONFIG_DEVPTS_FS_XATTR),y)
devpts-objs += xattr.o 
endif

ifeq ($(CONFIG_DEVPTS_FS_SECURITY),y)
devpts-objs += xattr_security.o
endif
