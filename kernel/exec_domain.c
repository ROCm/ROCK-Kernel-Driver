/*
 * Handling of different ABIs (personalities).
 *
 * We group personalities into execution domains which have their
 * own handlers for kernel entry points, signal mapping, etc...
 *
 * 2001-05-06	Complete rewrite,  Christoph Hellwig (hch@infradead.org)
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/personality.h>
#include <linux/sched.h>
#include <linux/sysctl.h>
#include <linux/types.h>


static void default_handler(int, struct pt_regs *);

static struct exec_domain *exec_domains = &default_exec_domain;
static rwlock_t exec_domains_lock = RW_LOCK_UNLOCKED;


static u_long ident_map[32] = {
	0,	1,	2,	3,	4,	5,	6,	7,
	8,	9,	10,	11,	12,	13,	14,	15,
	16,	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30,	31
};

struct exec_domain default_exec_domain = {
	.name		= "Linux",		/* name */
	.handler	= default_handler,	/* lcall7 causes a seg fault. */
	.pers_low	= 0, 			/* PER_LINUX personality. */
	.pers_high	= 0,			/* PER_LINUX personality. */
	.signal_map	= ident_map,		/* Identity map signals. */
	.signal_invmap	= ident_map,		/*  - both ways. */
};


static void
default_handler(int segment, struct pt_regs *regp)
{
	u_long			pers = 0;

	/*
	 * This may have been a static linked SVr4 binary, so we would
	 * have the personality set incorrectly. Or it might have been
	 * a Solaris/x86 binary. We can tell which because the former
	 * uses lcall7, while the latter used lcall 0x27.
	 * Try to find or load the appropriate personality, and fall back
	 * to just forcing a SEGV.
	 *
	 * XXX: this is IA32-specific and should be moved to the MD-tree.
	 */
	switch (segment) {
#ifdef __i386__
	case 0x07:
		pers = abi_defhandler_lcall7;
		break;
	case 0x27:
		pers = PER_SOLARIS;
		break;
#endif
	}
	set_personality(pers);

	if (current_thread_info()->exec_domain->handler != default_handler)
		current_thread_info()->exec_domain->handler(segment, regp);
	else
		send_sig(SIGSEGV, current, 1);
}

static struct exec_domain *
lookup_exec_domain(u_long personality)
{
	struct exec_domain *	ep;
	u_long			pers = personality(personality);
		
	read_lock(&exec_domains_lock);
	for (ep = exec_domains; ep; ep = ep->next) {
		if (pers >= ep->pers_low && pers <= ep->pers_high)
			if (try_module_get(ep->module))
				goto out;
	}

#ifdef CONFIG_KMOD
	read_unlock(&exec_domains_lock);
	request_module("personality-%ld", pers);
	read_lock(&exec_domains_lock);

	for (ep = exec_domains; ep; ep = ep->next) {
		if (pers >= ep->pers_low && pers <= ep->pers_high)
			if (try_module_get(ep->module))
				goto out;
	}
#endif

	ep = &default_exec_domain;
out:
	read_unlock(&exec_domains_lock);
	return (ep);
}

int
register_exec_domain(struct exec_domain *ep)
{
	struct exec_domain	*tmp;
	int			err = -EBUSY;

	if (ep == NULL)
		return -EINVAL;

	if (ep->next != NULL)
		return -EBUSY;

	write_lock(&exec_domains_lock);
	for (tmp = exec_domains; tmp; tmp = tmp->next) {
		if (tmp == ep)
			goto out;
	}

	ep->next = exec_domains;
	exec_domains = ep;
	err = 0;

out:
	write_unlock(&exec_domains_lock);
	return (err);
}

int
unregister_exec_domain(struct exec_domain *ep)
{
	struct exec_domain	**epp;

	epp = &exec_domains;
	write_lock(&exec_domains_lock);
	for (epp = &exec_domains; *epp; epp = &(*epp)->next) {
		if (ep == *epp)
			goto unregister;
	}
	write_unlock(&exec_domains_lock);
	return -EINVAL;

unregister:
	*epp = ep->next;
	ep->next = NULL;
	write_unlock(&exec_domains_lock);
	return 0;
}

int
__set_personality(u_long personality)
{
	struct exec_domain	*ep, *oep;

	ep = lookup_exec_domain(personality);
	if (ep == current_thread_info()->exec_domain) {
		current->personality = personality;
		return 0;
	}

	if (atomic_read(&current->fs->count) != 1) {
		struct fs_struct *fsp, *ofsp;

		fsp = copy_fs_struct(current->fs);
		if (fsp == NULL) {
			module_put(ep->module);
			return -ENOMEM;;
		}

		task_lock(current);
		ofsp = current->fs;
		current->fs = fsp;
		task_unlock(current);

		put_fs_struct(ofsp);
	}

	/*
	 * At that point we are guaranteed to be the sole owner of
	 * current->fs.
	 */

	current->personality = personality;
	oep = current_thread_info()->exec_domain;
	current_thread_info()->exec_domain = ep;
	set_fs_altroot();

	module_put(oep->module);
	return 0;
}

int
get_exec_domain_list(char *page)
{
	struct exec_domain	*ep;
	int			len = 0;

	read_lock(&exec_domains_lock);
	for (ep = exec_domains; ep && len < PAGE_SIZE - 80; ep = ep->next)
		len += sprintf(page + len, "%d-%d\t%-16s\t[%s]\n",
			       ep->pers_low, ep->pers_high, ep->name,
			       module_name(ep->module));
	read_unlock(&exec_domains_lock);
	return (len);
}

asmlinkage long
sys_personality(u_long personality)
{
	u_long old = current->personality;;

	if (personality != 0xffffffff) {
		set_personality(personality);
		if (current->personality != personality)
			return -EINVAL;
	}

	return (long)old;
}


EXPORT_SYMBOL(register_exec_domain);
EXPORT_SYMBOL(unregister_exec_domain);
EXPORT_SYMBOL(__set_personality);

/*
 * We have to have all sysctl handling for the Linux-ABI
 * in one place as the dynamic registration of sysctls is
 * horribly crufty in Linux <= 2.4.
 *
 * I hope the new sysctl schemes discussed for future versions
 * will obsolete this.
 *
 * 				--hch
 */

u_long abi_defhandler_coff = PER_SCOSVR3;
u_long abi_defhandler_elf = PER_LINUX;
u_long abi_defhandler_lcall7 = PER_SVR4;
u_long abi_defhandler_libcso = PER_SVR4;
u_int abi_traceflg;
int abi_fake_utsname;

static struct ctl_table abi_table[] = {
	{
		.ctl_name	= ABI_DEFHANDLER_COFF,
		.procname	= "defhandler_coff",
		.data		= &abi_defhandler_coff,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
	},
	{
		.ctl_name	= ABI_DEFHANDLER_ELF,
		.procname	= "defhandler_elf",
		.data		= &abi_defhandler_elf,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
	},
	{
		.ctl_name	= ABI_DEFHANDLER_LCALL7,
		.procname	= "defhandler_lcall7",
		.data		= &abi_defhandler_lcall7,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
	},
	{
		.ctl_name	= ABI_DEFHANDLER_LIBCSO,
		.procname	= "defhandler_libcso",
		.data		= &abi_defhandler_libcso,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
	},
	{
		.ctl_name	= ABI_TRACE,
		.procname	= "trace",
		.data		= &abi_traceflg,
		.maxlen		= sizeof(u_int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= ABI_FAKE_UTSNAME,
		.procname	= "fake_utsname",
		.data		= &abi_fake_utsname,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 }
};

static struct ctl_table abi_root_table[] = {
	{
		.ctl_name	= CTL_ABI,
		.procname	= "abi",
		.mode		= 0555,
		.child		= abi_table,
	},
	{ .ctl_name = 0 }
};

static int __init
abi_register_sysctl(void)
{
	register_sysctl_table(abi_root_table, 1);
	return 0;
}

__initcall(abi_register_sysctl);


EXPORT_SYMBOL(abi_defhandler_coff);
EXPORT_SYMBOL(abi_defhandler_elf);
EXPORT_SYMBOL(abi_defhandler_lcall7);
EXPORT_SYMBOL(abi_defhandler_libcso);
EXPORT_SYMBOL(abi_traceflg);
EXPORT_SYMBOL(abi_fake_utsname);
