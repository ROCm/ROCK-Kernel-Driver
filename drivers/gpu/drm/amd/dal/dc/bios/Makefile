#
# Makefile for the 'bios' sub-component of DAL.
# It provides the parsing and executing controls for atom bios image.

BIOS = bios_parser.o bios_parser_helper.o command_table.o command_table_helper.o

AMD_DAL_BIOS = $(addprefix $(AMDDALPATH)/dc/bios/,$(BIOS))

AMD_DAL_FILES += $(AMD_DAL_BIOS)

ifndef CONFIG_DRM_AMD_DAL_VBIOS_PRESENT
AMD_DAL_FILES := $(filter-out $(AMDDALPATH)/dc/bios/bios_parser_helper.o,$(AMD_DAL_FILES))
endif

###############################################################################
# DCE 8x
###############################################################################
# All DCE8.x are derived from DCE8.0, so 8.0 MUST be defined if ANY of
# DCE8.x is compiled.
ifdef CONFIG_DRM_AMD_DAL_DCE8_0

ifdef CONFIG_DRM_AMD_DAL_VBIOS_PRESENT
AMD_DAL_FILES += $(AMDDALPATH)/dc/bios/dce80/bios_parser_helper_dce80.o
endif

AMD_DAL_FILES += $(AMDDALPATH)/dc/bios/dce80/command_table_helper_dce80.o
endif

###############################################################################
# DCE 11x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0

ifdef CONFIG_DRM_AMD_DAL_VBIOS_PRESENT
AMD_DAL_FILES += $(AMDDALPATH)/dc/bios/dce110/bios_parser_helper_dce110.o
endif

AMD_DAL_FILES += $(AMDDALPATH)/dc/bios/dce110/command_table_helper_dce110.o
endif

ifdef CONFIG_DRM_AMD_DAL_DCE11_2
ccflags-y += -DLATEST_ATOM_BIOS_SUPPORT
ifdef CONFIG_DRM_AMD_DAL_VBIOS_PRESENT
AMD_DAL_FILES += $(AMDDALPATH)/dc/bios/dce112/bios_parser_helper_dce112.o
endif

AMD_DAL_FILES += $(AMDDALPATH)/dc/bios/dce112/command_table_helper_dce112.o
endif
