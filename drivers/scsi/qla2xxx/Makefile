EXTRA_CFLAGS += -g -Idrivers/scsi -DUNIQUE_FW_NAME

qla2xxx-y := qla_os.o qla_init.o qla_mbx.o qla_iocb.o qla_isr.o qla_gs.o \
		qla_dbg.o qla_sup.o qla_rscn.o qla_xioct.o qla_inioct.o

qla2xxx-$(CONFIG_SCSI_QLA2XXX_FAILOVER_ENABLE) += qla_fo.o qla_foln.o \
		qla_cfg.o qla_cfgln.o

qla2100-y := ql2100.o ql2100_fw.o
qla2200-y := ql2200.o ql2200_fw.o
qla2300-y := ql2300.o ql2300_fw.o #ql2322_fw.o

host-progs := extras/qla_nvr
always := $(host-progs)

obj-$(CONFIG_SCSI_QLA21XX) += qla2xxx.o qla2100.o
obj-$(CONFIG_SCSI_QLA22XX) += qla2xxx.o qla2200.o
obj-$(CONFIG_SCSI_QLA23XX) += qla2xxx.o qla2300.o
