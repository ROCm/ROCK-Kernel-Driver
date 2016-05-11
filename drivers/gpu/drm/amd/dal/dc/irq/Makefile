#
# Makefile for the 'audio' sub-component of DAL.
# It provides the control and status of HW adapter resources,
# that are global for the ASIC and sharable between pipes.

IRQ = irq_service.o

AMD_DAL_IRQ = $(addprefix $(AMDDALPATH)/dc/irq/,$(IRQ))

AMD_DAL_FILES += $(AMD_DAL_IRQ)

###############################################################################
# DCE 8x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE8_0
IRQ_DCE80 = irq_service_dce80.o

AMD_DAL_IRQ_DCE80 = $(addprefix $(AMDDALPATH)/dc/irq/dce80/,$(IRQ_DCE80))

AMD_DAL_FILES += $(AMD_DAL_IRQ_DCE80)
endif

###############################################################################
# DCE 11x
###############################################################################
ifdef CONFIG_DRM_AMD_DAL_DCE11_0
IRQ_DCE11 = irq_service_dce110.o

AMD_DAL_IRQ_DCE11 = $(addprefix $(AMDDALPATH)/dc/irq/dce110/,$(IRQ_DCE11))

AMD_DAL_FILES += $(AMD_DAL_IRQ_DCE11)
endif
