IB_SRC_DIR ?= drivers/infiniband

EXTRA_CFLAGS += \
    -I$(src) \
    -I$(IB_SRC_DIR)/drivers/infiniband/include \
    -I$(IB_SRC_DIR)/hw/mellanox-hca/include \
    -DOBUILD_FLAG \
    -DMT_KERNEL -D__LINUX__ -DMAX_ERROR=4 -DMTL_MODULE=MOSAL \
    "-DREMAP_PREFIX=current->mm->mmap,"

# XXX This is bogus, but it should work OK for now (since we don't
# XXX support all architectures yet)
ifdef CONFIG_PPC
EXTRA_CFLAGS += -DMT_BIG_ENDIAN
else
EXTRA_CFLAGS += -DMT_LITTLE_ENDIAN
endif

obj-m += mosal.o

mosal-objs := \
    mosal_gen_nos.o \
    mosal_wrap_kernel.o \
    mosal_bus.o \
    mosal_timer.o \
    mosal_mem.o \
    mosal_que.o \
    mosal_sync.o \
    mosal_gen.o \
    mosal_k2u_cbk.o \
    mosal_driver.o \
    mosal_mlock.o \
    mosal_iobuf.o \
    mosal_i2c.o \
    mosal_thread.o \
    mosal_syms.o \
    mosal_ioremap.o \
    mtl_common.o \
    mt_bufpool.o
