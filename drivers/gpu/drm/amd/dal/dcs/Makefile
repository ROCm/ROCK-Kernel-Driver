#
# Makefile for the 'gpu' sub-component of DAL.
# It provides the control and status of HW adapter resources,
# that are global for the ASIC and sharable between pipes.

EDID = edid/edid_base.o edid/edid13.o edid/edid14.o edid/edid20.o \
	edid/edid_ext_cea.o edid/edid_ext_di.o edid/edid_ext_vtb.o \
	edid/edid_ext_unknown.o edid/edid.o edid_mgr.o edid_patch.o

DCS = $(EDID) dcs.o dcs_list.o default_modes_dco.o vbios_dco.o ddc_service.o \
	ddc_i2caux_helper.o monitor_tables.o \
	remote_display_receiver_modes.o

AMD_DAL_DCS = $(addprefix $(AMDDALPATH)/dcs/,$(DCS))

AMD_DAL_FILES += $(AMD_DAL_DCS)
