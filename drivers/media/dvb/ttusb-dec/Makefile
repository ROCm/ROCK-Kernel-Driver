
obj-$(CONFIG_DVB_TTUSB_DEC) += ttusb_dec.o dec2000_frontend.o

EXTRA_CFLAGS = -Idrivers/media/dvb/dvb-core/

host-progs	:= fdump

$(obj)/ttusb_dec.o: $(obj)/dsp_dec2000.h

$(obj)/dsp_dec2000.h: $(patsubst "%", %, $(CONFIG_DVB_TTUSB_DEC_FIRMWARE_FILE)) $(obj)/fdump
	$(obj)/fdump $< dsp_dec2000 > $@
