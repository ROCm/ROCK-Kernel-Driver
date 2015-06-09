#
# Makefile for DSAT, which is a  sub-component
# of AMDGPU drm driver.
# It provides additional controls for debugging.

#AMDDALDSATPATH = ../amd/dsat
AMDDALDSATPATH = ../dsat

subdir-ccflags-y += -I$(AMDDALDSATPATH)

DSAT = amdgpu_dsat_ioctl.o dsat.o

AMDGPU_DSAT = $(addprefix $(AMDDALDSATPATH)/,$(DSAT))

amdgpu-y += $(AMDGPU_DSAT)


