#
# Makefile for the Linux nfs filesystem routines.
#

obj-$(CONFIG_NFS_FS) += nfs.o

nfs-y 			:= dir.o file.o flushd.o inode.o nfs2xdr.o pagelist.o \
			   proc.o read.o symlink.o unlink.o write.o
nfs-$(CONFIG_ROOT_NFS)	+= nfsroot.o mount_clnt.o      
nfs-$(CONFIG_NFS_V3)	+= nfs3proc.o nfs3xdr.o
nfs-objs		:= $(nfs-y)

include $(TOPDIR)/Rules.make
