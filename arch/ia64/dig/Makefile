#
# ia64/platform/dig/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

USE_STANDARD_AS_RULE := true

all: dig.a

O_TARGET := dig.a

obj-y := setup.o
obj-$(CONFIG_IA64_GENERIC) += machvec.o

clean::

include $(TOPDIR)/Rules.make
