
swsusp-smp-$(CONFIG_SMP)	+= smp.o

obj-y				:= main.o process.o console.o pm.o
obj-$(CONFIG_SOFTWARE_SUSPEND)	+= swsusp.o $(swsusp-smp-y)
obj-$(CONFIG_PM_DISK)		+= disk.o pmdisk.o

obj-$(CONFIG_MAGIC_SYSRQ)	+= poweroff.o
