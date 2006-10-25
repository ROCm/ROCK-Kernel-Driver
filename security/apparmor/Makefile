# Makefile for AppArmor Linux Security Module
#
# kernel build Makefile is the Kbuild file

REPO_VERSION := $(shell if [ -x /usr/bin/svn ] ; then \
       /usr/bin/svn info . 2> /dev/null | grep "^Last Changed Rev:" | sed "s/^Last Changed Rev: //" ; \
       fi)

ifeq ("${REPO_VERSION}", "")
REPO_VERSION := "unknown"
endif

KERNELVER := $(shell uname -r)

KERNELDIR := /lib/modules/${KERNELVER}/build

all:
	$(MAKE) -C $(KERNELDIR) M=`pwd` modules CONFIG_SECURITY_APPARMOR=m \
					APPARMOR_VER=${REPO_VERSION}
	mv apparmor.ko apparmor-${KERNELVER}.ko
	mv match/aamatch_pcre.ko aamatch_pcre-${KERNELVER}.ko

clean:
	rm -f *~ *.o *.ko *.mod.c .*.cmd Modules.symvers \
		match/*~ match/*.o match/*.ko match/.*.cmd match/*.mod.c
	rm -rf .tmp_versions


