#
# Makefile for the soundmodem device driver.
#

obj-$(CONFIG_SOUNDMODEM) += soundmodem.o

soundmodem-y					:= sm.o
soundmodem-$(CONFIG_SOUNDMODEM_SBC)		+= sm_sbc.o
soundmodem-$(CONFIG_SOUNDMODEM_WSS)		+= sm_wss.o
soundmodem-$(CONFIG_SOUNDMODEM_AFSK1200)	+= sm_afsk1200.o
soundmodem-$(CONFIG_SOUNDMODEM_AFSK2400_7)	+= sm_afsk2400_7.o
soundmodem-$(CONFIG_SOUNDMODEM_AFSK2400_8)	+= sm_afsk2400_8.o
soundmodem-$(CONFIG_SOUNDMODEM_AFSK2666)	+= sm_afsk2666.o
soundmodem-$(CONFIG_SOUNDMODEM_HAPN4800)	+= sm_hapn4800.o
soundmodem-$(CONFIG_SOUNDMODEM_PSK4800)		+= sm_psk4800.o
soundmodem-$(CONFIG_SOUNDMODEM_FSK9600)		+= sm_fsk9600.o
soundmodem-objs					:= $(soundmodem-y)

all:		all_targets
.PHONY:		all

gentbl:		gentbl.c
		$(HOSTCC) $(HOSTCFLAGS) $< -o $@ -lm

TBLHDR		:= sm_tbl_afsk1200.h sm_tbl_afsk2400_8.h 
TBLHDR		+= sm_tbl_afsk2666.h sm_tbl_psk4800.h
TBLHDR		+= sm_tbl_hapn4800.h sm_tbl_fsk9600.h

$(TBLHDR):	gentbl
		./gentbl

fastdep:	$(TBLHDR)

include $(TOPDIR)/Rules.make
