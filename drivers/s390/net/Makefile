#
# S/390 network devices
#

export-objs := iucv.o fsm.o cu3088.o

ctc-objs := ctcmain.o ctctty.o

obj-$(CONFIG_IUCV) += iucv.o fsm.o
obj-$(CONFIG_CTC) += ctc.o fsm.o cu3088.o
obj-$(CONFIG_IUCV) += netiucv.o
obj-$(CONFIG_LCS) += lcs.o cu3088.o

include $(TOPDIR)/Rules.make

