#
# Copyright (c) 2005-2006 Hewlett-Packard Development Company, L.P.
# Contributed by Stephane Eranian <eranian@hpl.hp.com>
#
obj-y = perfmon_init.o perfmon_rw.o perfmon_res.o           \
	perfmon_pmu.o perfmon_sysfs.o perfmon_syscalls.o    \
	perfmon_file.o perfmon_ctxsw.o perfmon_intr.o	    \
	perfmon_dfl_smpl.o perfmon_sets.o perfmon_hotplug.o \
	perfmon_msg.o perfmon_smpl.o perfmon_attach.o       \
	perfmon_activate.o perfmon_ctx.o perfmon_fmt.o	    \
	perfmon_control.o

obj-$(CONFIG_PERFMON_DEBUG_FS) +=  perfmon_debugfs.o
