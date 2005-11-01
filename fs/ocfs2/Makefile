OCFS2_BUILD_VERSION="\"1.3.0 svn 2673\""
EXTRA_CFLAGS += -Ifs/ocfs2

EXTRA_CFLAGS += -DCATCH_BH_JBD_RACES
EXTRA_CFLAGS += -DOCFS2_CDSL

# This is a huge kludge, but it will go away when the kernel API catches up
# Without in-kernel integration, this is normally hidden away in the OCFS2
# configure/Makefile mess.
KAPI_INCLUDE_DIR = fs/ocfs2/kapi-compat/include
EXTRA_CFLAGS += -include $(KAPI_INCLUDE_DIR)/assert_spin_locked.h
EXTRA_CFLAGS += -include $(KAPI_INCLUDE_DIR)/journal_access.h
EXTRA_CFLAGS += -include $(KAPI_INCLUDE_DIR)/kref_init.h
EXTRA_CFLAGS += -include $(KAPI_INCLUDE_DIR)/kref_put.h
#EXTRA_CFLAGS += -include $(KAPI_INCLUDE_DIR)/sparse_endian_types.h

EXTRA_CFLAGS += -DOCFS2_BUILD_VERSION=$(OCFS2_BUILD_VERSION)

obj-$(CONFIG_OCFS2_FS) += ocfs2.o

ocfs2-objs := \
       aio.o                   \
       alloc.o                 \
       aops.o                  \
       buffer_head_io.o        \
       dcache.o                \
       dir.o                   \
       dlmglue.o               \
       extent_map.o            \
       export.o                \
       file.o                  \
       heartbeat.o             \
       inode.o                 \
       journal.o               \
       localalloc.o            \
       mmap.o                  \
       namei.o                 \
       slot_map.o              \
       suballoc.o              \
       super.o                 \
       symlink.o               \
       sysfile.o               \
       uptodate.o              \
       ver.o                   \
       vote.o

obj-$(CONFIG_OCFS2_FS) += cluster/
obj-$(CONFIG_OCFS2_FS) += dlm/
