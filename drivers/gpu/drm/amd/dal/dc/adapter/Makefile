#
# Makefile for the 'adapter' sub-component of DAL.
# It provides the control and status of HW adapter.

ADAPTER = adapter_service.o hw_ctx_adapter_service.o wireless_data_source.o

AMD_DAL_ADAPTER = $(addprefix $(AMDDALPATH)/dc/adapter/,$(ADAPTER))

AMD_DAL_FILES += $(AMD_DAL_ADAPTER)

###############################################################################
# DCE 8x
###############################################################################

ifdef CONFIG_DRM_AMD_DAL_DCE8_0
AMD_DAL_FILES += $(AMDDALPATH)/dc/adapter/dce80/hw_ctx_adapter_service_dce80.o
endif


###############################################################################
# DCE 11x
###############################################################################

ifdef CONFIG_DRM_AMD_DAL_DCE11_0
AMD_DAL_FILES += $(AMDDALPATH)/dc/adapter/dce110/hw_ctx_adapter_service_dce110.o
endif

ifdef CONFIG_DRM_AMD_DAL_DCE11_2
AMD_DAL_FILES += $(AMDDALPATH)/dc/adapter/dce112/hw_ctx_adapter_service_dce112.o
endif

###############################################################################
# FPGA Diagnositcs
###############################################################################

AMD_DAL_FILES += $(AMDDALPATH)/dc/adapter/diagnostics/hw_ctx_adapter_service_diag.o
