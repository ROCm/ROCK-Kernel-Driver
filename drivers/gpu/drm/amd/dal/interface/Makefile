#
# Makefile for the 'dal_services' sub-component of DAL.
# It provides the entry point for all DAL services to the driver.


INTERFACE = dal.o timing_list_query.o dal_early_init_program.o

AMD_DAL_INTERFACE = $(addprefix $(AMDDALPATH)/interface/,$(INTERFACE))

AMD_DAL_FILES += $(AMD_DAL_INTERFACE)
