# 
# Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

O_TARGET = os.o

obj-y = file.o process.o tty.o

include $(TOPDIR)/Rules.make

$(obj-y) : %.o: %.c
	$(CC) $(CFLAGS_$@) $(USER_CFLAGS) -c -o $@ $<

clean :

archmrproper:
