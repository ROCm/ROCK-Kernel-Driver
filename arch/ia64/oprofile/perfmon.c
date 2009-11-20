/**
 * @file perfmon.c
 *
 * @remark Copyright 2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/perfmon_kern.h>
#include <asm/ptrace.h>
#include <asm/errno.h>

static int allow_ints;

static int
perfmon_handler(struct pfm_context *ctx,
		unsigned long ip, u64 stamp, void *data)
{
	struct pt_regs *regs;
	struct pfm_ovfl_arg *arg;

	regs = data;
	arg = &ctx->ovfl_arg;
 
	arg->ovfl_ctrl = PFM_OVFL_CTRL_RESET;

	/* the owner of the oprofile event buffer may have exited
	 * without perfmon being shutdown (e.g. SIGSEGV)
	 */
	if (allow_ints)
		oprofile_add_sample(regs, arg->pmd_eventid);
	return 0;
}


static int perfmon_start(void)
{
	allow_ints = 1;
	return 0;
}


static void perfmon_stop(void)
{
	allow_ints = 0;
}

static struct pfm_smpl_fmt oprofile_fmt = {
	.fmt_name = "OProfile",
	.fmt_handler = perfmon_handler,
	.fmt_flags = PFM_FMT_BUILTIN_FLAG,
	.owner = THIS_MODULE
};


static char *get_cpu_type(void)
{
	__u8 family = local_cpu_data->family;

	switch (family) {
		case 0x07:
			return "ia64/itanium";
		case 0x1f:
			return "ia64/itanium2";
		default:
			return "ia64/ia64";
	}
}


/* all the ops are handled via userspace for IA64 perfmon */

static int using_perfmon;

int perfmon_init(struct oprofile_operations *ops)
{
	int ret = pfm_fmt_register(&oprofile_fmt);
	if (ret)
		return -ENODEV;

	ops->cpu_type = get_cpu_type();
	ops->start = perfmon_start;
	ops->stop = perfmon_stop;
	using_perfmon = 1;
	printk(KERN_INFO "oprofile: using perfmon.\n");
	return 0;
}


void __exit perfmon_exit(void)
{
	if (!using_perfmon)
		return;

	pfm_fmt_unregister(&oprofile_fmt);
}
