#
# Makefile for the 'asic_capability' sub-component of DAL.
#

ASIC_CAPABILITY = asic_capability.o

AMD_DAL_ASIC_CAPABILITY = \
	$(addprefix $(AMDDALPATH)/dc/asic_capability/,$(ASIC_CAPABILITY))

AMD_DAL_FILES += $(AMD_DAL_ASIC_CAPABILITY)

###############################################################################
# DCE 8x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE8_0
ASIC_CAPABILITY_DCE80 = hawaii_asic_capability.o

AMD_DAL_ASIC_CAPABILITY_DCE80 = \
	$(addprefix $(AMDDALPATH)/dc/asic_capability/,$(ASIC_CAPABILITY_DCE80))

AMD_DAL_FILES += $(AMD_DAL_ASIC_CAPABILITY_DCE80)
endif


###############################################################################
# DCE 10x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE10_0
ASIC_CAPABILITY_DCE10 = tonga_asic_capability.o

AMD_DAL_ASIC_CAPABILITY_DCE10 = \
	$(addprefix $(AMDDALPATH)/dc/asic_capability/,$(ASIC_CAPABILITY_DCE10))

AMD_DAL_FILES += $(AMD_DAL_ASIC_CAPABILITY_DCE10)
endif


###############################################################################
# DCE 11x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
ASIC_CAPABILITY_DCE11 = carrizo_asic_capability.o

AMD_DAL_ASIC_CAPABILITY_DCE11 = \
	$(addprefix $(AMDDALPATH)/dc/asic_capability/,$(ASIC_CAPABILITY_DCE11))

AMD_DAL_FILES += $(AMD_DAL_ASIC_CAPABILITY_DCE11)
endif

ifdef CONFIG_DRM_AMD_DAL_DCE11_2
ASIC_CAPABILITY_DCE112 = polaris10_asic_capability.o

AMD_DAL_ASIC_CAPABILITY_DCE112 = \
	$(addprefix $(AMDDALPATH)/dc/asic_capability/,$(ASIC_CAPABILITY_DCE112))

AMD_DAL_FILES += $(AMD_DAL_ASIC_CAPABILITY_DCE112)
endif
