#
# Makefile for the Linux /dev/pts virtual filesystem.
#

obj-$(CONFIG_DEVPTS_FS) += devpts.o

devpts-y := inode.o
devpts-$(CONFIG_DEVPTS_FS_XATTR)	+= xattr.o 
devpts-$(CONFIG_DEVPTS_FS_SECURITY)	+= xattr_security.o
