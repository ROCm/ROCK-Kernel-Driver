#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
extern void amdkcl_pci_init(void);
#endif

extern void amdkcl_kthread_init(void);
extern void amdkcl_drm_init(void);
#if !defined(OS_NAME_RHEL_7_X)
extern void amdkcl_fence_init(void);
#endif
extern void amdkcl_io_init(void);
extern void amdkcl_dev_cgroup_init(void);

int __init amdkcl_init(void)
{
	amdkcl_kthread_init();
	amdkcl_drm_init();
#if !defined(OS_NAME_RHEL_7_X)
	amdkcl_fence_init();
#endif
	amdkcl_io_init();
	amdkcl_dev_cgroup_init();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
	amdkcl_pci_init();
#endif
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
