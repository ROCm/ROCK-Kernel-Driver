#
# Makefile for the Linux JFS filesystem routines.
#

ifeq ($(CONFIG_JFS_DMAPI),y)
	EXTRA_CFLAGS += -I$(TOPDIR)/fs/jfs -DDM_USE_SHASH
endif

obj-$(CONFIG_JFS_FS) += jfs.o

jfs-y    := super.o file.o inode.o namei.o jfs_mount.o jfs_umount.o \
	    jfs_xtree.o jfs_imap.o jfs_debug.o jfs_dmap.o \
	    jfs_unicode.o jfs_dtree.o jfs_inode.o \
	    jfs_extent.o symlink.o jfs_metapage.o \
	    jfs_logmgr.o jfs_txnmgr.o jfs_uniupr.o resize.o xattr.o

jfs-$(CONFIG_JFS_POSIX_ACL) += acl.o

jfs-$(CONFIG_JFS_DMAPI)	    += $(addprefix dmapi/, \
				dmapi_sysent.o \
				dmapi_attr.o \
				dmapi_config.o \
				dmapi_bulkattr.o \
				dmapi_dmattr.o \
				dmapi_event.o \
				dmapi_handle.o \
				dmapi_hole.o \
				dmapi_io.o \
				dmapi_mountinfo.o \
				dmapi_region.o \
				dmapi_register.o \
				dmapi_right.o \
				dmapi_session.o \
				dmapi_jfs.o \
				kmem.o)

EXTRA_CFLAGS += -D_JFS_4K
