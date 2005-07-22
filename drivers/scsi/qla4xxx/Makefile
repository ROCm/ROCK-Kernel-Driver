EXTRA_CFLAGS += -DCONFIG_SCSI_QLA4XXX -DCONFIG_SCSI_QLA4XXX_MODULE

EXTRA_CFLAGS += -DCONFIG_SCSI_QLA4XXX_FAILOVER

qla4xxx-y = ql4_os.o ql4_init.o ql4_mbx.o ql4_iocb.o ql4_isr.o ql4_isns.o \
		ql4_nvram.o ql4_dbg.o

qla4xxx-$(CONFIG_SCSI_QLA4XXX_FAILOVER) += ql4_cfg.o ql4_cfgln.o ql4_fo.o \
		ql4_foio.o ql4_foioctl.o ql4_foln.o ql4_xioct.o ql4_inioct.o \
		ql4_32ioctl.o
#		ql4_foio.o ql4_foln.o

obj-$(CONFIG_SCSI_QLA4XXX) += qla4xxx.o
