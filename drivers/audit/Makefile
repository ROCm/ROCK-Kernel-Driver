AUDIT_OBJS := control.o message.o args.o fileset.o filter.o syscall.o \
	sysctl.o
ifeq ($(CONFIG_COMPAT),y)
AUDIT_OBJS += ioctl.o
endif

ifeq ($(CONFIG_AUDIT),y)
obj-y      := $(AUDIT_OBJS) stubs.o
endif

ifeq ($(CONFIG_AUDIT),m)
audit-objs := $(AUDIT_OBJS)
obj-y      := stubs.o
obj-m      := audit.o
endif
