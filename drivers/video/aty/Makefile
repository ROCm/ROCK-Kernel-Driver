
O_TARGET := built-in.o

export-objs    :=  atyfb_base.o mach64_accel.o

obj-$(CONFIG_FB_ATY) += atyfb.o

atyfb-y				:= atyfb_base.o mach64_accel.o
atyfb-$(CONFIG_FB_ATY_GX)	+= mach64_gx.o
atyfb-$(CONFIG_FB_ATY_CT)	+= mach64_ct.o mach64_cursor.o
atyfb-objs			:= $(atyfb-y)

include $(TOPDIR)/Rules.make

