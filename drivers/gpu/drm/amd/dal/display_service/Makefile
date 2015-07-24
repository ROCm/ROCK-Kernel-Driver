#
# Makefile for the 'display_service' sub-component of DAL.
# It provides the control and status of availible display services.

DS = display_service.o path_mode_set.o set_mode_params.o \
	ds_dispatch_adjustment.o ds_overlay.o ds_calculation.o grph_colors_group.o \
	adjustment_container.o ds_dispatch_info_frame.o \
	ds_dispatch_mode_setting.o ds_dispatch_planes.o ds_translation.o adjustment_api.o \
	scaler_adj_group.o backlight_adj_group.o single_adj_group.o \
	gamut_space.o color_temperature.o path_mode_set_with_data.o gamma_lut.o

AMD_DAL_DS = $(addprefix $(AMDDALPATH)/display_service/,$(DS))

AMD_DAL_FILES += $(AMD_DAL_DS)
