#
# Makefile for alpha-specific library files..
#

EXTRA_AFLAGS := $(CFLAGS)
L_TARGET := lib.a

# Many of these routines have implementations tuned for ev6.
# Choose them iff we're targeting ev6 specifically.
ev6 :=
ifeq ($(CONFIG_ALPHA_EV6),y)
  ev6 := ev6-
endif

# Several make use of the cttz instruction introduced in ev67.
ev67 :=
ifeq ($(CONFIG_ALPHA_EV67),y)
  ev67 := ev67-
endif

obj-y =	__divqu.o __remqu.o __divlu.o __remlu.o \
	udelay.o \
	$(ev6)memset.o \
	$(ev6)memcpy.o \
	memmove.o \
	io.o \
	checksum.o \
	csum_partial_copy.o \
	$(ev67)strlen.o \
	$(ev67)strcat.o \
	strcpy.o \
	$(ev67)strncat.o \
	strncpy.o \
	$(ev6)stxcpy.o \
	$(ev6)stxncpy.o \
	$(ev67)strchr.o \
	$(ev67)strrchr.o \
	$(ev6)memchr.o \
	$(ev6)copy_user.o \
	$(ev6)clear_user.o \
	$(ev6)strncpy_from_user.o \
	$(ev67)strlen_user.o \
	$(ev6)csum_ipv6_magic.o \
	$(ev6)clear_page.o \
	$(ev6)copy_page.o \
	strcasecmp.o \
	fpreg.o \
	callback_srm.o srm_puts.o srm_printk.o

obj-$(CONFIG_SMP) += dec_and_lock.o

include $(TOPDIR)/Rules.make

$(obj)/__divqu.o: $(obj)/$(ev6)divide.S
	$(CC) $(AFLAGS) -DDIV -c -o $(obj)/__divqu.o $(obj)/$(ev6)divide.S

$(obj)/__remqu.o: $(obj)/$(ev6)divide.S
	$(CC) $(AFLAGS) -DREM -c -o $(obj)/__remqu.o $(obj)/$(ev6)divide.S

$(obj)/__divlu.o: $(obj)/$(ev6)divide.S
	$(CC) $(AFLAGS) -DDIV -DINTSIZE \
		-c -o $(obj)/__divlu.o $(obj)/$(ev6)divide.S

$(obj)/__remlu.o: $(obj)/$(ev6)divide.S
	$(CC) $(AFLAGS) -DREM -DINTSIZE \
		-c -o $(obj)/__remlu.o $(obj)/$(ev6)divide.S
