#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>

extern void amdkcl_kthread_init(void);
extern void amdkcl_drm_init(void);
extern void amdkcl_fence_init(void);
extern void amdkcl_dev_cgroup_init(void);
extern void amdkcl_pci_init(void);
extern void amdkcl_suspend_init(void);
extern void amdkcl_perf_event_init(void);

int __init amdkcl_init(void)
{
	amdkcl_kthread_init();
	amdkcl_drm_init();
	amdkcl_fence_init();
	amdkcl_dev_cgroup_init();
	amdkcl_pci_init();
	amdkcl_suspend_init();
	amdkcl_perf_event_init();

	return 0;
}
module_init(amdkcl_init);

void __exit amdkcl_exit(void)
{

}
module_exit(amdkcl_exit);

MODULE_AUTHOR("AMD linux driver team");
MODULE_DESCRIPTION("Module for OS kernel compatible layer");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
