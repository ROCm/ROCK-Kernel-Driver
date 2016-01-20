#
# Makefile for Display Core (dc) component.
#

DC_LIBS = adapter asic_capability audio basics bios calcs \
dcs gpio gpu i2caux irq

ifdef CONFIG_DRM_AMD_DAL_DCE11_0
DC_LIBS += dce110
endif

ifdef CONFIG_DRM_AMD_DAL_DCE10_0
DC_LIBS += dce100
endif

AMD_DC = $(addsuffix /Makefile, $(addprefix $(FULL_AMD_DAL_PATH)/dc/,$(DC_LIBS)))

include $(AMD_DC)

DISPLAY_CORE = dc.o dc_link.o dc_resource.o dc_target.o dc_sink.o dc_stream.o \
dc_hw_sequencer.o dc_surface.o dc_link_hwss.o dc_link_dp.o

AMD_DISPLAY_CORE = $(addprefix $(AMDDALPATH)/dc/core/,$(DISPLAY_CORE))

AMD_DAL_FILES += $(AMD_DISPLAY_CORE)



