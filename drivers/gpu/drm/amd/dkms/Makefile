ifneq ($(KERNELRELEASE),)
include $(src)/amd/dkms/Kbuild
else
KERNELVER := $(shell uname -r)
kernel_build_dir := /lib/modules/$(KERNELVER)/build
module_src_dir := $(CURDIR)
module_build_dir := $(shell mktemp -ut amd.XXXXXXXX)
module_build_flags :=
num_cpu_cores := $(shell which nproc > /dev/null && nproc || echo "1")
Q := @

ifeq ($(wildcard $(kernel_build_dir)/include/config/auto.conf),)
$(error "invalid kernel obj dir, is kernel-devel installed?")
endif

.PHONY: modules pre-build clean

include $(kernel_build_dir)/include/config/auto.conf

ifneq ($(CONFIG_CC_IS_CLANG),)
module_build_flags += CC=clang
endif
ifneq ($(CONFIG_LD_IS_LLD),)
module_build_flags += LD=ld.lld
endif

modules:pre-build
	$(Q)$(shell cat $(module_build_dir)/.env) make -j$(num_cpu_cores) \
		TTM_NAME=amdttm \
		SCHED_NAME=amd-sched \
		-C $(kernel_build_dir) \
		M=$(module_build_dir) $(module_build_flags)
	$(Q)amd/dkms/post-build.sh $(module_build_dir)

pre-build:
	$(Q)amd/dkms/pre-build.sh $(KERNELVER) $(module_src_dir) $(module_build_dir)

clean:
	$(Q)make -C $(kernel_build_dir) M=$(module_src_dir) clean
endif
