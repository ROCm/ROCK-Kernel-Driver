CLUSTER_BUILD_VERSION="\"1.3.0 svn 2673\""
EXTRA_CFLAGS += -DCLUSTER_BUILD_VERSION=$(CLUSTER_BUILD_VERSION)

KAPI_INCLUDE_DIR = $(srctree)/fs/ocfs2/kapi-compat/include
EXTRA_CFLAGS += -include $(KAPI_INCLUDE_DIR)/kref_init.h

obj-$(CONFIG_OCFS2_FS) += ocfs2_nodemanager.o

ocfs2_nodemanager-objs := heartbeat.o masklog.o nodemanager.o \
       quorum.o tcp.o ver.o sys.o
