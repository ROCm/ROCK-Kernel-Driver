EXTRA_CFLAGS += \
    -Idrivers/infiniband/include \
    -Idrivers/infiniband/ulp/ipoib \
    -DTS_HOST_DRIVER -DTS_USE_CM_API_V3 -DTS_KERNEL_2_6 -D_NO_DATA_PATH_TRACE

obj-m += ib_sdp.o

ib_sdp-objs := \
    sdp_buff.o \
    sdp_proc.o \
    sdp_inet.o \
    sdp_advt.o \
    sdp_iocb.o \
    sdp_wall.o \
    sdp_conn.o \
    sdp_post.o \
    sdp_rcvd.o \
    sdp_sent.o \
    sdp_kvec.o \
    sdp_send.o \
    sdp_recv.o \
    sdp_read.o \
    sdp_queue.o \
    sdp_write.o \
    sdp_event.o
