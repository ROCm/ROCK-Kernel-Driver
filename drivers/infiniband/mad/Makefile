EXTRA_CFLAGS += \
    -Idrivers/infiniband/include \
    -Idrivers/infiniband/hw/mellanox-hca/include \
    -I$(obj)/../core \
    -DTS_HOST_DRIVER -DTS_KERNEL_2_6 -D_NO_DATA_PATH_TRACE

obj-m += ib_mad.o

ib_mad-objs := \
    mad_main.o \
    mad_ib.o \
    mad_filter.o \
    mad_thread.o \
    mad_static.o \
    mad_proc.o \
    mad_export.o

$(ib_mod-objs:%=$(obj)/%): $(obj)/ib_packet_lib.o
