#
# Makefile for drivers/acorn/scsi
#

USE_STANDARD_AS_RULE := true

L_TARGET	:= acorn-scsi.a

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

export-objs		:= fas216.o queue.o msgqueue.o
list-multi		:= acornscsi_mod.o
acornscsi_mod-objs	:= acornscsi.o acornscsi-io.o

obj-$(CONFIG_SCSI_ACORNSCSI_3)	+= acornscsi_mod.o queue.o msgqueue.o
obj-$(CONFIG_SCSI_ARXESCSI)	+= arxescsi.o fas216.o queue.o msgqueue.o
obj-$(CONFIG_SCSI_CUMANA_1)	+= cumana_1.o
obj-$(CONFIG_SCSI_CUMANA_2)	+= cumana_2.o fas216.o queue.o msgqueue.o
obj-$(CONFIG_SCSI_ECOSCSI)	+= ecoscsi.o
obj-$(CONFIG_SCSI_OAK1)		+= oak.o
obj-$(CONFIG_SCSI_POWERTECSCSI)	+= powertec.o fas216.o queue.o msgqueue.o
obj-$(CONFIG_SCSI_EESOXSCSI)	+= eesox.o fas216.o queue.o msgqueue.o

# Extract lists of the multi-part drivers.
# The 'int-*' lists are intermediate files used to build the multi's.

multi-y		:= $(filter $(list-multi), $(obj-y))
multi-m		:= $(filter $(list-multi), $(obj-m))
int-y		:= $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular; remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))
int-m		:= $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.

obj-y		:= $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.

L_OBJS		:= $(filter-out $(export-objs), $(obj-y))
LX_OBJS		:= $(filter     $(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make

acornscsi_mod.o: acornscsi.o acornscsi-io.o
	$(LD) $(LD_RFLAG) -r -o $@ acornscsi.o acornscsi-io.o
