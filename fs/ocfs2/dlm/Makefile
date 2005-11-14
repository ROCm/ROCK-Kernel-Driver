EXTRA_CFLAGS += -Ifs/ocfs2

DLM_BUILD_VERSION="\"1.3.0 svn 2673\""
EXTRA_CFLAGS += -DDLM_BUILD_VERSION=$(DLM_BUILD_VERSION)

KAPI_INCLUDE_DIR = $(srctree)/fs/ocfs2/kapi-compat/include
EXTRA_CFLAGS += -include $(KAPI_INCLUDE_DIR)/kref_init.h

obj-$(CONFIG_OCFS2_FS) += ocfs2_dlm.o ocfs2_dlmfs.o

ocfs2_dlm-objs := dlmdomain.o dlmdebug.o dlmthread.o dlmrecovery.o \
       dlmmaster.o dlmast.o dlmconvert.o dlmlock.o dlmunlock.o dlmver.o

ocfs2_dlmfs-objs := userdlm.o dlmfs.o dlmfsver.o
