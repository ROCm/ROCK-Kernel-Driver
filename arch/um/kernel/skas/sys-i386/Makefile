# 
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

obj-y = sigcontext.o

USER_OBJS = sigcontext.o
USER_OBJS := $(foreach file,$(USER_OBJS),$(obj)/$(file))

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$(notdir $@)) $(USER_CFLAGS) -c -o $@ $<
