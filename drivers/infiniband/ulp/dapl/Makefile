EXTRA_CFLAGS += \
    -Idrivers/infiniband/include \
    -Idrivers/infiniband/ulp/ipoib \
    -Idrivers/infiniband/hw/mellanox-hca/include \
    -D__LINUX__ \
    -DTS_HOST_DRIVER -DTS_KERNEL_2_6 -D_NO_DATA_PATH_TRACE

obj-m += ib_udapl.o

ib_udapl-objs := \
    udapl_mod.o \
    khash.o
