/*
 *  linux/fs/filesystems.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  table of configured filesystems
 */

#include <linux/config.h>
#include <linux/fs.h>

#include <linux/devfs_fs_kernel.h>
#include <linux/nfs_fs.h>
#include <linux/auto_fs.h>
#include <linux/devpts_fs.h>
#include <linux/major.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/nfsd/interface.h>

#ifdef CONFIG_DEVPTS_FS
extern int init_devpts_fs(void);
#endif

void __init filesystem_setup(void)
{
	init_devfs_fs();  /*  Header file may make this empty  */

#ifdef CONFIG_NFS_FS
	init_nfs_fs();
#endif

#ifdef CONFIG_DEVPTS_FS
	init_devpts_fs();
#endif
}

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
