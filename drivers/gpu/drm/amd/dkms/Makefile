ifneq ($(KERNELRELEASE),)
include $(src)/amd/dkms/Kbuild
else
KERNELVER := $(shell uname -r)
kernel_build_dir := /lib/modules/$(KERNELVER)/build
module_src_dir := $(CURDIR)
module_build_dir := $(shell mktemp -ut amd.XXXXXXXX)
module_build_flags :=
num_cpu_cores := $(shell which nproc > /dev/null && nproc || echo "1")
CC := gcc
Q := @

ifeq ($(wildcard $(kernel_build_dir)/include/config/auto.conf),)
$(error "invalid kernel obj dir, is kernel-devel installed?")
endif

.PHONY: modules sanity-check pre-build clean
modules:

include $(kernel_build_dir)/include/config/auto.conf

ifndef CONFIG_DRM
$(error CONFIG_DRM disabled, exit...)
endif

ifeq (y,$(CONFIG_DRM_AMDGPU_AMDKCL))
$(error DRM_AMDGPU is built-in, exit...)
endif

ifndef CONFIG_KALLSYMS
$(error CONFIG_KALLSYMS disabled, kallsyms_lookup_name() is absent, exit...)
endif

ifneq ($(CONFIG_CC_IS_CLANG),)
CC := clang
module_build_flags += CC=clang
endif
ifneq ($(CONFIG_LD_IS_LLD),)
module_build_flags += LD=ld.lld
endif

config-file ?= amd/dkms/config/config.h
KCL_MACRO_CHECK_COMMAND=$(shell grep $(1) $(config-file) | grep -q "define" && echo "y")

modules: sanity-check
	$(Q)$(shell cat $(module_build_dir)/.env) make -j$(num_cpu_cores) \
		TTM_NAME=amdttm \
		SCHED_NAME=amd-sched \
		-C $(kernel_build_dir) \
		M=$(module_build_dir) $(module_build_flags)
	$(Q)amd/dkms/post-build.sh $(module_build_dir)

sanity-check: pre-build $(config-file)
	$(if $(call KCL_MACRO_CHECK_COMMAND, HAVE_DMA_RESV_SEQ_BUG), $(error dma_resv->seq is missing. exit...))
	$(if $(call KCL_MACRO_CHECK_COMMAND, HAVE_RESERVATION_WW_CLASS_BUG), $(error reservation_ww_class is missing. exit...))

pre-build:
	$(Q)amd/dkms/pre-build.sh $(KERNELVER) $(module_src_dir) $(module_build_dir) $(CC)

clean:
	$(Q)make -C $(kernel_build_dir) M=$(module_src_dir) clean
endif
