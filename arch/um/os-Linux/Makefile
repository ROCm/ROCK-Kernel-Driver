# 
# Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

obj-y = aio.o file.o process.o time.o tty.o user_syms.o drivers/

USER_OBJS := $(foreach file,aio.o file.o process.o time.o tty.o,$(obj)/$(file))

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$(notdir $@)) $(USER_CFLAGS) -c -o $@ $<

HAVE_AIO_ABI = $(shell [ -e /usr/include/linux/aio_abi.h ] && \
	echo -DHAVE_AIO_ABI)
HAVE_AIO_LIBC = $(shell objdump -T /lib/libc-*.so | grep io_submit && \
	echo -DHAVE_AIO_LIBC)
CFLAGS_aio.o = $(HAVE_AIO_ABI) $(HAVE_AIO_LIBC)
