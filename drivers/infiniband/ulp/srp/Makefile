EXTRA_CFLAGS += \
    -Idrivers/infiniband/include \
    -Idrivers/scsi \
    -DRHAS_SCSI_API -DSUPPORT_SCSI_REV2 \
    -DTS_HOST_DRIVER -DTS_KERNEL_2_6 -D_NO_DATA_PATH_TRACE

obj-m += ib_srp.o

ib_srp-objs := \
    srptp.o \
    srp_host.o \
    hostoptions.o \
    srp_dm.o
