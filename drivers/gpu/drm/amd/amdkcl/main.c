#include <linux/kernel.h>
#include <linux/module.h>
#include <kcl/kcl_drm_global.h>

extern void amdkcl_kthread_init(void);
extern void amdkcl_drm_init(void);
extern void amdkcl_fence_init(void);
extern void amdkcl_io_init(void);

int __init amdkcl_init(void)
{
	amdkcl_kthread_init();
	amdkcl_drm_init();
	amdkcl_fence_init();
	amdkcl_io_init();
	kcl_drm_global_init();
	return 0;
}
module_init(amdkcl_init);

void __exit amdkcl_exit(void)
{
	kcl_drm_global_release();
}

MODULE_AUTHOR("AMD linux driver team");
MODULE_DESCRIPTION("Module for OS kernel compatible layer");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
