#
# Makefile for kHTTPd
#

obj-$(CONFIG_KHTTPD) += khttpd.o

khttpd-objs := 	main.o accept.o datasending.o logging.o misc.o rfc.o \
		rfc_time.o security.o sockets.o sysctl.o userspace.o \
		waitheaders.o

include $(TOPDIR)/Rules.make

rfc_time.o: times.h

make_times_h: make_times_h.c
	$(HOSTCC) $(HOSTCFLAGS) -o make_times_h make_times_h.c

times.h: make_times_h
	./make_times_h
