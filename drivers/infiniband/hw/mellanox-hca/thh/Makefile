IB_SRC_DIR ?= drivers/infiniband

EXTRA_CFLAGS += \
    -I$(src) \
    -I$(IB_SRC_DIR)/include \
    -I$(IB_SRC_DIR)/hw/mellanox-hca/include \
    -DTS_KERNEL_2_6 -DOBUILD_FLAG \
    -DMT_KERNEL -D__LINUX__ -DMAX_ERROR=4 -DMTL_MODULE=THH

# XXX This is bogus, but it should work OK for now (since we don't
# XXX support all architectures yet)
ifdef CONFIG_PPC
EXTRA_CFLAGS += -DMT_BIG_ENDIAN
else
EXTRA_CFLAGS += -DMT_LITTLE_ENDIAN
endif

obj-m += mod_thh.o

mod_thh-objs := \
    tlog2.o \
    epool.o \
    sm_mad.o \
    extbuddy.o \
    thh_init.o \
    thh_hob.o \
    cmdif.o \
    cmds_wrap.o \
    eventp.o \
    event_irqh.o \
    uar.o \
    tddrmm.o \
    thh_uldm.o \
    tmrwm.o \
    tcqm.o \
    tqpm.o \
    thh_srqm.o \
    udavm.o \
    mcgm.o \
    thh_mod_obj.o \
    thhklsyms.o
