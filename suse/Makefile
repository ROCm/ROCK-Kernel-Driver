
obj-$(CONFIG_PROC_CONFIG) = config_gz.o

CLEAN_FILES += config_gz.h

suse/config_gz.h: .config
	gzip -c $< | hexdump -v -e '1/1 "0x%02x,"' > $@
	echo >> $@ # make gcc 3.x happy

suse/config_gz.o: suse/config_gz.c suse/config_gz.h
