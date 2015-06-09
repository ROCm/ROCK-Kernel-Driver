#
# Makefile for the 'display_path' sub-component of DAL.
# It provides the control and status of HW adapter resources,
# that are global for the ASIC and sharable between pipes.

DISPLAY_PATH = display_path.o stream_context.o

AMD_DAL_DISPLAY_PATH = $(addprefix $(AMDDALPATH)/display_path/,$(DISPLAY_PATH))

AMD_DAL_FILES += $(AMD_DAL_DISPLAY_PATH)
