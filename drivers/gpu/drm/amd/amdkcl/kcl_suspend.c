#include <kcl/kcl_suspend.h>
#include "kcl_common.h"

#ifndef HAVE_KSYS_SYNC_HELPER

long (*_kcl_ksys_sync)(void);

void _kcl_ksys_sync_helper(void)
{
	pr_info("Syncing filesystems ... ");
	_kcl_ksys_sync();
	pr_cont("done.\n");
}
EXPORT_SYMBOL(_kcl_ksys_sync_helper);

static bool _kcl_sys_sync_stub(void)
{
	printk_once(KERN_WARNING "kernel symbol [k]sys_sync not found!\n");
	return false;
}

void amdkcl_suspend_init(void)
{
	_kcl_ksys_sync = amdkcl_fp_setup("ksys_sync", _kcl_sys_sync_stub);
	if (_kcl_ksys_sync != _kcl_sys_sync_stub) {
		return;
	}

	_kcl_ksys_sync = amdkcl_fp_setup("sys_sync", _kcl_sys_sync_stub);
	if (_kcl_ksys_sync != _kcl_sys_sync_stub) {
		return;
	}

	printk_once(KERN_ERR "Error: fail to get symbol [k]sys_sync!\n");
	BUG();
}

#endif /* HAVE_KSYS_SYNC_HELPER */
