#
# Makefile for kHTTPd
#

obj-$(CONFIG_KHTTPD) += khttpd.o

khttpd-objs := 	main.o accept.o datasending.o logging.o misc.o rfc.o \
		rfc_time.o security.o sockets.o sysctl.o userspace.o \
		waitheaders.o

host-progs := make_times_h

include $(TOPDIR)/Rules.make

# Dependencies on generated files need to be listed explicitly

$(obj)/rfc_time.o: $(obj)/times.h

# Generated files

$(obj)/times.h: $(obj)/make_times_h
	$< >$@
