#
# Makefile for the kernel software RAID and LVM drivers.
#

dm-mod-objs	:= dm.o dm-table.o dm-target.o dm-linear.o dm-stripe.o \
		   dm-ioctl.o
raid6-objs	:= raid6main.o raid6algos.o raid6recov.o raid6tables.o \
		   raid6int1.o raid6int2.o raid6int4.o \
		   raid6int8.o raid6int16.o raid6int32.o \
		   raid6mmx.o raid6sse1.o raid6sse2.o
host-progs	:= mktables

# Note: link order is important.  All raid personalities
# and xor.o must come before md.o, as they each initialise 
# themselves, and md.o may use the personalities when it 
# auto-initialised.

obj-$(CONFIG_MD_LINEAR)		+= linear.o
obj-$(CONFIG_MD_RAID0)		+= raid0.o
obj-$(CONFIG_MD_RAID1)		+= raid1.o
obj-$(CONFIG_MD_RAID5)		+= raid5.o xor.o
obj-$(CONFIG_MD_RAID6)		+= raid6.o xor.o
obj-$(CONFIG_MD_MULTIPATH)	+= multipath.o
obj-$(CONFIG_BLK_DEV_MD)	+= md.o
obj-$(CONFIG_BLK_DEV_DM)	+= dm-mod.o

# Files generated that shall be removed upon make clean
clean-files := raid6int*.c raid6tables.c mktables

$(obj)/raid6int1.c:   $(src)/raid6int.uc $(src)/unroll.pl
	$(PERL) $(src)/unroll.pl 1 < $< > $@ || ( rm -f $@ && exit 1 )

$(obj)/raid6int2.c:   $(src)/raid6int.uc $(src)/unroll.pl
	$(PERL) $(src)/unroll.pl 2 < $< > $@ || ( rm -f $@ && exit 1 )

$(obj)/raid6int4.c:   $(src)/raid6int.uc $(src)/unroll.pl
	$(PERL) $(src)/unroll.pl 4 < $< > $@ || ( rm -f $@ && exit 1 )

$(obj)/raid6int8.c:   $(src)/raid6int.uc $(src)/unroll.pl
	$(PERL) $(src)/unroll.pl 8 < $< > $@ || ( rm -f $@ && exit 1 )

$(obj)/raid6int16.c:  $(src)/raid6int.uc $(src)/unroll.pl
	$(PERL) $(src)/unroll.pl 16 < $< > $@ || ( rm -f $@ && exit 1 )

$(obj)/raid6int32.c:  $(src)/raid6int.uc $(src)/unroll.pl
	$(PERL) $(src)/unroll.pl 32 < $< > $@ || ( rm -f $@ && exit 1 )

$(obj)/raid6tables.c: $(obj)/mktables
	$(obj)/mktables > $@ || ( rm -f $@ && exit 1 )
