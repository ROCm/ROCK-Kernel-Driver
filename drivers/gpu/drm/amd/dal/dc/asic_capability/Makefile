#
# Makefile for the 'asic_capability' sub-component of DAL.
#

ASIC_CAPABILITY = asic_capability.o

AMD_DAL_ASIC_CAPABILITY = \
	$(addprefix $(AMDDALPATH)/dc/asic_capability/,$(ASIC_CAPABILITY))

AMD_DAL_FILES += $(AMD_DAL_ASIC_CAPABILITY)


###############################################################################
# DCE 11x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
ASIC_CAPABILITY_DCE11 = carrizo_asic_capability.o

AMD_DAL_ASIC_CAPABILITY_DCE11 = \
	$(addprefix $(AMDDALPATH)/dc/asic_capability/,$(ASIC_CAPABILITY_DCE11))

AMD_DAL_FILES += $(AMD_DAL_ASIC_CAPABILITY_DCE11)
endif
