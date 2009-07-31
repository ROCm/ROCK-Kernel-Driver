EXTRA_CFLAGS += -Idrivers/xen/sfc_netback -Idrivers/xen/sfc_netutil -Idrivers/xen/netback -Idrivers/net/sfc
EXTRA_CFLAGS += -D__ci_driver__ 
EXTRA_CFLAGS += -DEFX_USE_KCOMPAT
EXTRA_CFLAGS += -Werror

ifdef GCOV
EXTRA_CFLAGS += -fprofile-arcs -ftest-coverage -DEFX_GCOV
endif

obj-$(CONFIG_XEN_NETDEV_ACCEL_SFC_BACKEND) := sfc_netback.o

sfc_netback-objs   := accel.o accel_fwd.o accel_msg.o accel_solarflare.o accel_xenbus.o accel_debugfs.o
