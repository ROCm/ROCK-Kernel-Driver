IB_SRC_DIR ?= drivers/infiniband

EXTRA_CFLAGS += \
    -I$(IB_SRC_DIR)/include \
    -I$(IB_SRC_DIR)/hw/mellanox-hca/include \
    -D__LINUX__ \
    -DTS_HOST_DRIVER -DTS_KERNEL_2_6 -D_NO_DATA_PATH_TRACE

obj-m += ib_tavor.o

ib_tavor-objs := \
    tavor_main.o \
    tavor_device.o \
    tavor_pd.o \
    tavor_ah.o \
    tavor_qp.o \
    tavor_cq.o \
    tavor_mr.o \
    tavor_fmr.o \
    tavor_mcast.o \
    tavor_mad.o \
    tavor_async.o
