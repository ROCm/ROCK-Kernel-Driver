#
# ia64/platform/dig/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

obj-y := setup.o

ifndef CONFIG_NUMA
obj-$(CONFIG_IA64_DIG) += topology.o
endif

obj-$(CONFIG_IA64_GENERIC) += machvec.o
