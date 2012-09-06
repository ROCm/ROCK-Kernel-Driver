EXTRA_CFLAGS += -Idrivers/xen/sfc_netutil
EXTRA_CFLAGS += -Werror

ifdef GGOV
EXTRA_CFLAGS += -fprofile-arcs -ftest-coverage -DEFX_GCOV
endif

obj-$(CONFIG_XEN_NETDEV_ACCEL_SFC_UTIL) := sfc_netutil.o

sfc_netutil-objs := accel_cuckoo_hash.o accel_msg_iface.o accel_util.o 

