#
# Makefile for the 'mode' sub-component of DAL.
# It provides the mode management services for the driver.

BEST_VIEW = best_view.o candidate_list.o solution.o

MODE_QUERY = mode_query.o mode_query_set.o mode_query_allow_pan.o \
	mode_query_no_pan.o mode_query_tiled_display_preferred.o

MODE_MGR = $(BEST_VIEW) $(MODE_QUERY) cofunctional_mode_validator.o \
	display_view_solution_container.o mode_manager.o \
	mode_manager_types.o view_solution.o cofunctional_mode_query_validator.o

AMD_DAL_MODE = $(addprefix $(AMDDALPATH)/mode_manager/,$(MODE_MGR))

AMD_DAL_FILES += $(AMD_DAL_MODE)
