obj-$(CONFIG_FB_ATY) += atyfb.o
obj-$(CONFIG_FB_ATY128) += aty128fb.o

atyfb-y				:= atyfb_base.o mach64_accel.o
atyfb-$(CONFIG_FB_ATY_GX)	+= mach64_gx.o
atyfb-$(CONFIG_FB_ATY_CT)	+= mach64_ct.o mach64_cursor.o
atyfb-objs			:= $(atyfb-y)
