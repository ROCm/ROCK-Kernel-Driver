# Kernel makefile for 8.x series QLogic Fibre Channel driver.
#
EXTRA_CFLAGS += -g -Idrivers/scsi -I$(obj) -DUNIQUE_FW_NAME

ISPTYPES := qla2100 qla2200 qla2300
ISPDIRS := $(foreach dir, $(ISPTYPES), $(addsuffix /, $(addprefix .,$(dir))))

COMMON_SRCS := qla_os.c qla_init.c qla_mbx.c qla_iocb.c qla_isr.c qla_gs.c \
	qla_xioct.c qla_inioct.c qla_fo.c qla_cfg.c qla_cfgln.c qla_vendor.c \
	qla_dbg.c qla_sup.c
ISP2100_SRCS := $(COMMON_SRCS) ql2100_fw.c
ISP2200_SRCS := $(COMMON_SRCS) ql2200_fw.c
ISP2300_SRCS := $(COMMON_SRCS) qla_rscn.c ql2300_fw.c #ql2322_fw.c
ISP2100_OBJS := $(foreach file, $(ISP2100_SRCS), $(patsubst %.c, %.o, $(file)))
ISP2200_OBJS := $(foreach file, $(ISP2200_SRCS), $(patsubst %.c, %.o, $(file)))
ISP2300_OBJS := $(foreach file, $(ISP2300_SRCS), $(patsubst %.c, %.o, $(file)))

ISP2100_DIR_SRCS := $(foreach file, $(ISP2100_SRCS), $(addprefix .qla2100/,$(file)))
ISP2200_DIR_SRCS := $(foreach file, $(ISP2200_SRCS), $(addprefix .qla2200/,$(file)))
ISP2300_DIR_SRCS := $(foreach file, $(ISP2300_SRCS), $(addprefix .qla2300/,$(file)))
ISP2100_DIR_OBJS := $(foreach file, $(ISP2100_OBJS), $(addprefix .qla2100/,$(file)))
ISP2200_DIR_OBJS := $(foreach file, $(ISP2200_OBJS), $(addprefix .qla2200/,$(file)))
ISP2300_DIR_OBJS := $(foreach file, $(ISP2300_OBJS), $(addprefix .qla2300/,$(file)))

PREPALL := $(shell for dir in $(ISPDIRS) ; \
	do \
		if [ ! -d $(obj)/$${dir} ] ; then \
			mkdir $(obj)/$${dir} ; \
		fi ; \
	done ; \
	for link in $(ISP2100_SRCS) ; \
	do \
		if [ ! -h $(obj)/.qla2100/$${link} ] ; then \
			ln -sf ../$${link} $(obj)/.qla2100/$${link} ; \
		fi ; \
	done ; \
	for link in $(ISP2200_SRCS) ; \
	do \
		if [ ! -h $(obj)/.qla2200/$${link} ] ; then \
			ln -sf ../$${link} $(obj)/.qla2200/$${link} ; \
		fi ; \
	done ; \
	for link in $(ISP2300_SRCS) ; \
	do \
		if [ ! -h $(obj)/.qla2300/$${link} ] ; then \
			ln -sf ../$${link} $(obj)/.qla2300/$${link} ; \
		fi ; \
	done)

obj-$(CONFIG_SCSI_QLA2XXX_QLA21XX) += qla2100.o
obj-$(CONFIG_SCSI_QLA2XXX_QLA22XX) += qla2200.o
obj-$(CONFIG_SCSI_QLA2XXX_QLA23XX) += qla2300.o

$(obj)/qla2100.o: $(PREPALL)
$(obj)/qla2200.o: $(PREPALL)
$(obj)/qla2300.o: $(PREPALL)

$(src)/.qla2100/%: EXTRA_CFLAGS += -Dqla21xx
$(src)/.qla2200/%: EXTRA_CFLAGS += -Dqla22xx
$(src)/.qla2300/%: EXTRA_CFLAGS += -Dqla23xx

qla2100-objs := $(ISP2100_DIR_OBJS)
qla2200-objs := $(ISP2200_DIR_OBJS)
qla2300-objs := $(ISP2300_DIR_OBJS)

clean-files := $(ISP2100_DIR_SRCS) $(ISP2200_DIR_SRCS) $(ISP2300_DIR_SRCS)
