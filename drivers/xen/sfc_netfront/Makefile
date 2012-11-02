EXTRA_CFLAGS += -Idrivers/xen/sfc_netfront -Idrivers/xen/sfc_netutil -Idrivers/xen/netfront
EXTRA_CFLAGS += -D__ci_driver__
EXTRA_CFLAGS += -Werror

ifdef GCOV
EXTRA_CFLAGS += -fprofile-arcs -ftest-coverage -DEFX_GCOV
endif

obj-$(CONFIG_XEN_NETDEV_ACCEL_SFC_FRONTEND)	:= sfc_netfront.o

sfc_netfront-objs := accel_msg.o accel_bufs.o accel_netfront.o accel_vi.o accel_xenbus.o accel_tso.o accel_ssr.o accel_debugfs.o falcon_event.o falcon_vi.o pt_tx.o vi_init.o
