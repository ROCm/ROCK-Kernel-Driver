EXTRA_CFLAGS += \
    -Idrivers/infiniband/include \
    -DTS_HOST_DRIVER -DTS_KERNEL_2_6 -D_NO_DATA_PATH_TRACE

obj-$(CONFIG_INFINIBAND) += ib_services.o ib_poll.o

ib_services-objs := \
    services_main.o \
    services_trace.o \
    services_hash.o \
    services_thread.o \
    services_export.o

ib_poll-objs := \
    poll_main.o \
    poll_timer.o \
    poll_export.o
