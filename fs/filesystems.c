/*
 *  linux/fs/filesystems.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  table of configured filesystems
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/kmod.h>
#include <linux/nfsd/interface.h>

#if defined(CONFIG_NFSD_MODULE)
struct nfsd_linkage *nfsd_linkage = NULL;

long
asmlinkage sys_nfsservctl(int cmd, void *argp, void *resp)
{
	int ret = -ENOSYS;
	
	lock_kernel();

	if (nfsd_linkage ||
	    (request_module ("nfsd") == 0 && nfsd_linkage))
		ret = nfsd_linkage->do_nfsservctl(cmd, argp, resp);

	unlock_kernel();
	return ret;
}
EXPORT_SYMBOL(nfsd_linkage);

#elif ! defined (CONFIG_NFSD)
asmlinkage int sys_nfsservctl(int cmd, void *argp, void *resp)
{
	return -ENOSYS;
}
#endif /* CONFIG_NFSD */
