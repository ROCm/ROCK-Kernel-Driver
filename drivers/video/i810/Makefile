#
# Makefile for the Intel 810/815 framebuffer driver
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...


obj-$(CONFIG_FB_I810)		+= i810fb.o


i810fb-objs                     := i810_main.o i810_accel.o

ifdef CONFIG_FB_I810_GTF
i810fb-objs                     += i810_gtf.o
else
i810fb-objs                     += i810_dvt.o
endif
