#
# Makefile for rcfs routines.
#

obj-$(CONFIG_RCFS_FS) += rcfs.o

rcfs-objs := super.o inode.o dir.o rootdir.o magic.o tc_magic.o socket_fs.o 

rcfs-objs-$(CONFIG_CKRM_TYPE_TASKCLASS) += tc_magic.o
rcfs-objs-$(CONFIG_CKRM_TYPE_SOCKETCLASS) += socket_fs.o
