#
# Makefile for SCTP support code.
#

obj-$(CONFIG_IP_SCTP) += sctp.o

sctp-y := sctp_sm_statetable.o sctp_sm_statefuns.o sctp_sm_sideeffect.o \
	  sctp_protocol.o sctp_endpointola.o sctp_associola.o \
	  sctp_transport.o sctp_sm_make_chunk.o sctp_ulpevent.o \
	  sctp_inqueue.o sctp_outqueue.o sctp_ulpqueue.o sctp_command.o \
	  sctp_tsnmap.o sctp_bind_addr.o sctp_socket.o sctp_primitive.o \
	  sctp_output.o sctp_input.o sctp_hashdriver.o sctp_sla1.o \
	  sctp_debug.o

ifeq ($(CONFIG_SCTP_ADLER32), y)
sctp-y += sctp_adler32.o
else
sctp-y += sctp_crc32c.o
endif

sctp-$(CONFIG_SCTP_DBG_OBJCNT) += sctp_objcnt.o
sctp-$(CONFIG_SYSCTL) += sctp_sysctl.o

sctp-$(subst m,y,$(CONFIG_IPV6))	+= sctp_ipv6.o

sctp-objs := $(sctp-y)

include $(TOPDIR)/Rules.make
