#
# Makefile for x86 timers
#

obj-y := timer.o

obj-y += timer_tsc.o
obj-y += timer_pit.o
obj-$(CONFIG_X86_CYCLONE)   += timer_cyclone.o

include $(TOPDIR)/Rules.make
