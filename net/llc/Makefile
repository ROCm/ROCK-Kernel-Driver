###########################################################################
# Makefile for the Linux 802.2 LLC (fully-functional) layer.
#
# Note 1! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...
#
# Copyright (c) 1997 by Procom Technology,Inc.
#		2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
#
# This program can be redistributed or modified under the terms of the 
# GNU General Public License as published by the Free Software Foundation.
# This program is distributed without any warranty or implied warranty
# of merchantability or fitness for a particular purpose.
#
# See the GNU General Public License for more details.
###########################################################################

O_TARGET := llc.o

obj-y := llc_if.o llc_c_ev.o llc_c_ac.o llc_mac.o llc_sap.o llc_s_st.o \
	 llc_main.o llc_s_ac.o llc_conn.o llc_c_st.o llc_stat.o llc_actn.o \
	 llc_s_ev.o llc_evnt.o llc_pdu.o

ifeq ($(CONFIG_LLC_UI),y)
  obj-y += llc_sock.o
endif

# Objects that export symbols.
export-objs := llc_if.o

ifeq ($(CONFIG_LLC),m)
  obj-m += $(O_TARGET)
endif

include $(TOPDIR)/Rules.make
