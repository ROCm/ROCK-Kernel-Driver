# SPDX-License-Identifier: MIT
#
# Makefile for DCN21.

DCN21 = dcn21_init.o dcn21_hubp.o dcn21_hubbub.o dcn21_resource.o \
	 dcn21_hwseq.o dcn21_link_encoder.o

ifeq (1,$(AMDKCL_KBUILD_TAKE_PATH))
AMDKCL_KBUILD_TAKE_PATH_CURRENT=$(AMDDALPATH)/dc/dcn21/
else
AMDKCL_KBUILD_TAKE_PATH_CURRENT=
endif

CFLAGS_$(AMDKCL_KBUILD_TAKE_PATH_CURRENT)dcn21_resource.o := -mhard-float -msse

ifeq ($(call cc-ifversion, -lt, 0701, y), y)
IS_OLD_GCC = 1
endif

ifdef IS_OLD_GCC
# Stack alignment mismatch, proceed with caution.
# GCC < 7.1 cannot compile code using `double` and -mpreferred-stack-boundary=3
# (8B stack alignment).
CFLAGS_$(AMDKCL_KBUILD_TAKE_PATH_CURRENT)dcn21_resource.o += -mpreferred-stack-boundary=4
else
CFLAGS_$(AMDKCL_KBUILD_TAKE_PATH_CURRENT)dcn21_resource.o += -msse2
endif

AMD_DAL_DCN21 = $(addprefix $(AMDDALPATH)/dc/dcn21/,$(DCN21))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN21)
