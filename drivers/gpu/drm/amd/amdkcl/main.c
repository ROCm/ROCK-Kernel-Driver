#include <linux/kernel.h>
#include <linux/module.h>

extern void amdkcl_dev_cgroup_init(void);
extern void amdkcl_reservation_init(void);
extern void amdkcl_fence_init(void);
extern void amdkcl_io_init(void);
extern void amdkcl_kthread_init(void);
extern void amdkcl_mm_init(void);


int __init amdkcl_init(void)
{
	amdkcl_dev_cgroup_init();
	amdkcl_reservation_init();
	amdkcl_fence_init();
	amdkcl_io_init();
	amdkcl_kthread_init();
	amdkcl_mm_init();
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
