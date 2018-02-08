#
# Makefile for the 'utils' sub-component of DAL.
# It provides the general basic services required by other DAL
# subcomponents.

dml_ccflags := -mhard-float -msse -mpreferred-stack-boundary=4

CFLAGS_display_mode_vba.o := $(dml_ccflags)
CFLAGS_display_mode_lib.o := $(dml_ccflags)
CFLAGS_display_pipe_clocks.o := $(dml_ccflags)
CFLAGS_dml1_display_rq_dlg_calc.o := $(dml_ccflags)
CFLAGS_display_rq_dlg_helpers.o := $(dml_ccflags)
CFLAGS_soc_bounding_box.o := $(dml_ccflags)
CFLAGS_dml_common_defs.o := $(dml_ccflags)

DML = display_mode_lib.o display_rq_dlg_helpers.o dml1_display_rq_dlg_calc.o \
	  soc_bounding_box.o dml_common_defs.o

AMD_DAL_DML = $(addprefix $(AMDDALPATH)/dc/dml/,$(DML))

AMD_DISPLAY_FILES += $(AMD_DAL_DML)
