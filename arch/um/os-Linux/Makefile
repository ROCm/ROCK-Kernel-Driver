# 
# Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

obj-y = file.o process.o tty.o drivers/

USER_OBJS := $(foreach file,file.o process.o tty.o,$(obj)/$(file))

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$(notdir $@)) $(USER_CFLAGS) -c -o $@ $<

clean :

archmrproper:
