#/*******************************************************************
# * This file is part of the Emulex Linux Device Driver for         *
# * Enterprise Fibre Channel Host Bus Adapters.                     *
# * Refer to the README file included with this package for         *
# * driver version and adapter support.                             *
# * Copyright (C) 2004 Emulex Corporation.                          *
# * www.emulex.com                                                  *
# *                                                                 *
# * This program is free software; you can redistribute it and/or   *
# * modify it under the terms of the GNU General Public License     *
# * as published by the Free Software Foundation; either version 2  *
# * of the License, or (at your option) any later version.          *
# *                                                                 *
# * This program is distributed in the hope that it will be useful, *
# * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
# * GNU General Public License for more details, a copy of which    *
# * can be found in the file COPYING included with this package.    *
# *******************************************************************/
# This Makefile selects either the 2.4 or 2.6 Makefile
######################################################################

#$Id: Makefile 1.51 2004/10/18 16:39:16EDT sf_support Exp  $
BUILD_FC_TRANS=1
ifeq (,$(KERNELVERSION))
   KERNELVERSION := $(shell uname -r)
endif

ifeq (,$(BASEINCLUDE))
   BASEINCLUDE := /lib/modules/$(KERNELVERSION)/build
endif

ifneq ($(GCOV),)
  EXTRA_CFLAGS += -fprofile-arcs -ftest-coverage
  EXTRA_CFLAGS += -O0
  export EXTRA_CFLAGS
endif

ifeq ($(BUILD_NO_DEBUG),)
  EXTRA_CFLAGS += -DDFC_DEBUG
  export EXTRA_CFLAGS
endif

ifneq ($(BUILD_FC_TRANS),)
ifeq ($(BUILD_FC_TRANS),2)
  EXTRA_CFLAGS += -DFC_TRANS_VER2 -DFC_TRANS_VER1
else
ifeq ($(BUILD_FC_TRANS),1)
  EXTRA_CFLAGS += -DFC_TRANS_VER1
else
ifeq ($(BUILD_FC_TRANS),0)
  EXTRA_CFLAGS += -DFC_TRANS_265_BLKPATCH
endif
endif
endif

  export EXTRA_CFLAGS

endif

# KERNELRELEASE is in the second pass of the 2.6 make process
ifneq (,$(KERNELRELEASE))

obj-$(CONFIG_SCSI_LPFC) := lpfc.o

lpfc-objs := lpfc_mem.o lpfc_sli.o lpfc_ct.o lpfc_els.o \
lpfc_hbadisc.o lpfc_init.o lpfc_mbox.o lpfc_nportdisc.o lpfc_scsiport.o \
lpfc_fcp.o

else

# Building for 2.6 kernel

clean-files := *.o *.ko *.mod.o *.mod.c .*.cmd .*.flags .tmp_versions

build:
	$(MAKE) -C $(BASEINCLUDE) SUBDIRS=`pwd` CONFIG_SCSI_LPFC=m modules

install:
	@rm -f /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/lpfc.ko
	install -d /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/lpfc
	install -c lpfc.ko /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/lpfc
	depmod -a

clean:
#	$(MAKE) -C $(BASEINCLUDE) SUBDIRS=$(PWD) CONFIG_SCSI_LPFC=m clean
	rm -rf $(clean-files) 
endif
