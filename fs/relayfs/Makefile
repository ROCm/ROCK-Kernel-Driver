#
# relayfs Makefile
#

obj-$(CONFIG_RELAYFS_FS) += relayfs.o

relayfs-y := relay.o relay_lockless.o relay_locking.o inode.o resize.o
relayfs-$(CONFIG_KLOG_CHANNEL) += klog.o
