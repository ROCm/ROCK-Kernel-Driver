
obj-$(CONFIG_PROC_CONFIG) = config_gz.o

CLEAN_FILES += config_gz.h

rpmify/config_gz.h: .config
	gzip -c $< | hexdump -v -e '1/1 "0x%02x,"' > $@
	echo >> $@ # make gcc 3.x happy

rpmify/config_gz.o: rpmify/config_gz.c rpmify/config_gz.h
