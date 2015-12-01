#
# Makefile for the 'controller' sub-component of DAL.
# It provides the control and status of HW CRTC block.

DCE_BASE = dce_base_resource.o

AMD_DAL_DCE_BASE = $(addprefix $(AMDDALPATH)/dc/dce_base/,$(DCE_BASE))

AMD_DAL_FILES += $(AMD_DAL_DCE_BASE)


###############################################################################
# DCE 11x
###############################################################################
ifdef 0#CONFIG_DRM_AMD_DAL_DCE11_0
TG_DCE_BASE = dce_base_resource.o

AMD_DAL_TG_DCE110 = $(addprefix \
	$(AMDDALPATH)/dc/dce_base/,$(TG_DCE_BASE))

AMD_DAL_FILES += $(AMD_DAL_TG_DCE_BASE)
endif

