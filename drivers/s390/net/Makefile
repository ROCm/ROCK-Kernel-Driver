#
# S/390 network devices
#

O_TARGET := s390-net.o

list-multi := ctc.o
export-objs := iucv.o

ctc-objs := ctcmain.o ctctty.o fsm.o

obj-y += iucv.o
obj-$(CONFIG_CTC) += ctc.o
obj-$(CONFIG_IUCV) += netiucv.o

include $(TOPDIR)/Rules.make

ctc.o: $(ctc-objs)
	$(LD) -r -o $@ $(ctc-objs)

