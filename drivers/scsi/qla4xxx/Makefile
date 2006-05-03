# --- Override settings until Kconfig embedded
CONFIG_SCSI_QLA4XXX=m
EXTRA_CFLAGS += -DCONFIG_SCSI_QLA4XXX -DCONFIG_SCSI_QLA4XXX_MODULE 

qla4xxx-y = ql4_os.o ql4_init.o ql4_mbx.o ql4_iocb.o ql4_isr.o ql4_isns.o \
		ql4_nvram.o ql4_dbg.o

obj-$(CONFIG_SCSI_QLA4XXX) += qla4xxx.o
