EXTRA_CFLAGS += \
    -Idrivers/infiniband/include \
    -Idrivers/infiniband/ulp/ipoib \
    -Idrivers/infiniband/hw/mellanox-hca/include \
    -D__LINUX__ \
    -DTS_KERNEL_2_6

obj-m += ib_useraccess.o ib_useraccess_cm.o

ib_useraccess-objs := \
    useraccess_main.o \
    useraccess_ioctl.o \
    useraccess_mad.o

ib_useraccess_cm-objs := \
    useraccess_cm.o
