IB_SRC_DIR ?= drivers/infiniband

EXTRA_CFLAGS += \
    -I$(src) \
    -I$(IB_SRC_DIR)/include \
    -I$(IB_SRC_DIR)/hw/mellanox-hca/include \
    -I$(IB_SRC_DIR)/hw/mellanox-hca/thh \
    -I$(IB_SRC_DIR)/hw/mellanox-hca/vipkl \
    -DMT_KERNEL -D__LINUX__ -DMAX_ERROR=4 -DMTL_MODULE=VAPI

# XXX This is bogus, but it should work OK for now (since we don't
# XXX support all architectures yet)
ifdef CONFIG_PPC
EXTRA_CFLAGS += -DMT_BIG_ENDIAN
else
EXTRA_CFLAGS += -DMT_LITTLE_ENDIAN
endif

obj-m += mod_vapi.o

mod_vapi-objs := \
    vipul.o \
    hobul.o \
    vapisyms.o \
    hhul.o \
    hh_common.o \
    tlog2.o \
    epool.o \
    thhul_hob.o \
    thhul_pdm.o \
    thhul_cqm.o \
    thhul_srqm.o \
    thhul_qpm.o \
    thhul_mwm.o \
    uar.o \
    udavm.o
