#
# S/390 network devices
#

export-objs := iucv.o fsm.o

ctc-objs := ctcmain.o ctctty.o

obj-$(CONFIG_IUCV) += iucv.o fsm.o
obj-$(CONFIG_CTC) += ctc.o fsm.o
obj-$(CONFIG_IUCV) += netiucv.o
obj-$(CONFIG_LCS) += lcs.o

include $(TOPDIR)/Rules.make

