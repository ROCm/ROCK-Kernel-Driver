#
# Makefile for the 'utils' sub-component of DAL.
# It provides the general basic services required by other DAL
# subcomponents.

CONTAINERS = vector.o flat_set.o
BASICS = signal_types.o grph_object_id.o fixed31_32.o fixed32_32.o \
	$(CONTAINERS) logger.o register_logger.o

AMD_DAL_BASICS = $(addprefix $(AMDDALPATH)/basics/,$(BASICS))

AMD_DAL_FILES += $(AMD_DAL_BASICS)
