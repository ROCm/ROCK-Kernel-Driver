obj-y = proxy.o ptrace.o sysdep.o wait.o

USER_OBJS := $(foreach file,$(obj-y),arch/um/ptproxy/$(file))

include $(TOPDIR)/Rules.make

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$@) $(USER_CFLAGS) -c -o $@ $<

clean:
	rm -f *.o core child ptproxy

