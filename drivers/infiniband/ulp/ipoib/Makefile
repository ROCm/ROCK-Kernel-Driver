EXTRA_CFLAGS += \
    -Idrivers/infiniband/include \
    -DTS_HOST_DRIVER -DTS_KERNEL_2_6 -D_NO_DATA_PATH_TRACE

obj-m += ib_ipoib.o ib_ip2pr.o

ib_ipoib-objs := \
    ipoib_main.o \
    ipoib_ib.o \
    ipoib_multicast.o \
    ipoib_arp.o \
    ipoib_proc.o \
    ipoib_export.o \
    ipoib_verbs.o \
    ipoib_vlan.o

ib_ip2pr-objs := \
    ip2pr_mod.o \
    ip2pr_link.o \
    ip2pr_proc.o
