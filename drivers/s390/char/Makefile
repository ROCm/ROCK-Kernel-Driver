#
# S/390 character devices
#

export-objs := hwc_rw.o tape.o tape34xx.o

tub3270-objs := tuball.o tubfs.o tubtty.o \
                     tubttyaid.o tubttybld.o tubttyscl.o \
                     tubttyrcl.o tubttysiz.o

tape390-$(CONFIG_S390_TAPE_CHAR)  += tapechar.o
tape390-$(CONFIG_S390_TAPE_BLOCK) += tapeblock.o

tape390-objs   := tape.o tape34xx.o $(sort $(tape390-y))
tape_3480_mod-objs := tape3480.o
tape_3490_mod-objs := tape3490.o 


obj-y += ctrlchar.o
obj-$(CONFIG_TN3215) += con3215.o
obj-$(CONFIG_HWC) += hwc_con.o hwc_rw.o hwc_tty.o
obj-$(CONFIG_HWC_CPI) += hwc_cpi.o
obj-$(CONFIG_TN3270) += tub3270.o
obj-$(CONFIG_S390_TAPE)       += tape390.o
obj-$(CONFIG_S390_TAPE_3480)  += tape_3480_mod.o
obj-$(CONFIG_S390_TAPE_3490)  += tape_3490_mod.o

include $(TOPDIR)/Rules.make

