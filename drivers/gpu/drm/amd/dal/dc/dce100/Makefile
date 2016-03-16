#
# Makefile for the 'controller' sub-component of DAL.
# It provides the control and status of HW CRTC block.

DCE100 = dce100_resource.o dce100_hw_sequencer.o dce100_link_encoder.o

AMD_DAL_DCE100 = $(addprefix $(AMDDALPATH)/dc/dce100/,$(DCE100))

AMD_DAL_FILES += $(AMD_DAL_DCE100)


###############################################################################
# DCE 10x
###############################################################################
ifdef 0#CONFIG_DRM_AMD_DAL_DCE11_0
TG_DCE100 = dce100_resource.o

AMD_DAL_TG_DCE100 = $(addprefix \
	$(AMDDALPATH)/dc/dce100/,$(TG_DCE100))

AMD_DAL_FILES += $(AMD_DAL_TG_DCE100)
endif

