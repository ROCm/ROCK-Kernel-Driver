# 
# Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

obj-y = file.o process.o tty.o

USER_OBJS := $(foreach file,$(obj-y),arch/um/os-Linux/$(file))

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$@) $(USER_CFLAGS) -c -o $@ $<

clean :

archmrproper:
