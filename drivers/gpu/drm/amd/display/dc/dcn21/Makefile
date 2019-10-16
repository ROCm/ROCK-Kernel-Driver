#
# Makefile for DCN21.

DCN21 = dcn21_hubp.o dcn21_hubbub.o dcn21_resource.o dcn21_hwseq.o dcn21_link_encoder.o

CFLAGS_dcn21_resource.o := -mhard-float -msse

ifdef CONFIG_CC_IS_GCC
CFLAGS_dcn21_resource.o += -mpreferred-stack-boundary=4
endif

ifdef CONFIG_CC_IS_CLANG
CFLAGS_dcn21_resource.o += -msse2
endif

AMD_DAL_DCN21 = $(addprefix $(AMDDALPATH)/dc/dcn21/,$(DCN21))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN21)
