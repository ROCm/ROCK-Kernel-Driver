#
# Makefile for the drm device driver.  This driver provides support for
# the Direct Rendering Infrastructure (DRI) in XFree86 4.x.
#

# drm.o is a fake target -- it is never built
# The real targets are in the module-list
O_TARGET	:= drm.o

module-list     := gamma.o tdfx.o r128.o ffb.o mga.o i810.o
export-objs     := $(patsubst %.o,%_drv.o,$(module-list))

# libs-objs are included in every module so that radical changes to the
# architecture of the DRM support library can be made at a later time.
#
# The downside is that each module is larger, and a system that uses
# more than one module (i.e., a dual-head system) will use more memory
# (but a system that uses exactly one module will use the same amount of
# memory).
#
# The upside is that if the DRM support library ever becomes insufficient
# for new families of cards, a new library can be implemented for those new
# cards without impacting the drivers for the old cards.  This is significant,
# because testing architectural changes to old cards may be impossible, and
# may delay the implementation of a better architecture.  We've traded slight
# memory waste (in the dual-head case) for greatly improved long-term
# maintainability.
#
# NOTE: lib-objs will be eliminated in future versions, thereby
# eliminating the need to compile the .o files into every module, but
# for now we still need them.
#

lib-objs   := init.o memory.o proc.o auth.o context.o drawable.o bufs.o
lib-objs   += lists.o lock.o ioctl.o fops.o vm.o dma.o ctxbitmap.o

ifeq ($(CONFIG_AGP),y)
 lib-objs  += agpsupport.o
else
 ifeq ($(CONFIG_AGP),m)
  lib-objs  += agpsupport.o
 endif
endif

gamma-objs := gamma_drv.o gamma_dma.o
tdfx-objs  := tdfx_drv.o                tdfx_context.o
r128-objs  := r128_drv.o  r128_cce.o    r128_context.o r128_bufs.o r128_state.o
ffb-objs   := ffb_drv.o                 ffb_context.o
mga-objs   := mga_drv.o   mga_dma.o     mga_context.o  mga_bufs.o  mga_state.o
i810-objs  := i810_drv.o  i810_dma.o    i810_context.o i810_bufs.o

obj-$(CONFIG_DRM_GAMMA) += gamma.o
obj-$(CONFIG_DRM_TDFX)  += tdfx.o
obj-$(CONFIG_DRM_R128)  += r128.o
obj-$(CONFIG_DRM_FFB)   += ffb.o
obj-$(CONFIG_DRM_MGA)   += mga.o
obj-$(CONFIG_DRM_I810)  += i810.o


# When linking into the kernel, link the library just once. 
# If making modules, we include the library into each module

lib-objs-mod := $(patsubst %.o,%-mod.o,$(lib-objs))

ifdef MAKING_MODULES
  lib = drmlib-mod.a
else
  obj-y += drmlib.a
endif

include $(TOPDIR)/Rules.make

$(patsubst %.o,%.c,$(lib-objs-mod)): 
	@ln -sf $(subst -mod,,$@) $@

drmlib-mod.a: $(lib-objs-mod)
	rm -f $@
	$(AR) $(EXTRA_ARFLAGS) rcs $@ $(lib-objs-mod)

drmlib.a: $(lib-objs)
	rm -f $@
	$(AR) $(EXTRA_ARFLAGS) rcs $@ $(lib-objs)

gamma.o: $(gamma-objs) $(lib)
	$(LD) -r -o $@ $(gamma-objs) $(lib)

tdfx.o: $(tdfx-objs) $(lib)
	$(LD) -r -o $@ $(tdfx-objs) $(lib)

mga.o: $(mga-objs) $(lib)
	$(LD) -r -o $@ $(mga-objs) $(lib)

i810.o: $(i810-objs) $(lib)
	$(LD) -r -o $@ $(i810-objs) $(lib)

r128.o: $(r128-objs) $(lib)
	$(LD) -r -o $@ $(r128-objs) $(lib)

ffb.o: $(ffb-objs) $(lib)
	$(LD) -r -o $@ $(ffb-objs) $(lib)
