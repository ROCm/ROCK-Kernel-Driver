#
# Makefile for the 'topology' sub-component of DAL.
# It facilitates the control on the sharable HW blocks and resources
# of the adapter.

TOPOLOGY = tm_resource_mgr.o tm_detection_mgr.o tm_resource_builder.o \
	tm_utils.o tm_display_path_set.o tm_subsets_cache.o tm_resource.o \
	topology.o

AMD_DAL_TOPOLOGY = $(addprefix $(AMDDALPATH)/topology/,$(TOPOLOGY))

AMD_DAL_FILES += $(AMD_DAL_TOPOLOGY)
