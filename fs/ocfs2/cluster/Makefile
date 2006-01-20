obj-$(CONFIG_OCFS2_FS) += ocfs2_nodemanager.o
obj-$(CONFIG_OCFS2_FS_O2CB) += ocfs2_disk_heartbeat.o
obj-$(CONFIG_OCFS2_FS_USERSPACE_CLUSTER) += ocfs2_user_heartbeat.o

ocfs2_nodemanager-objs := nodemanager.o heartbeat.o tcp.o net_proc.o \
	masklog.o ver.o sys.o disk_heartbeat.o quorum.o

ocfs2_user_heartbeat-objs := user_heartbeat.o

