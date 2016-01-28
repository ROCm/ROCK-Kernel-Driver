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
# DCE 11x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0

ifdef CONFIG_DRM_AMD_DAL_VBIOS_PRESENT
AMD_DAL_FILES += $(AMDDALPATH)/dc/bios/dce110/bios_parser_helper_dce110.o
endif

AMD_DAL_FILES += $(AMDDALPATH)/dc/bios/dce110/command_table_helper_dce110.o
endif
