/*
 * platform driver support 
 */

#include <linux/platform.h>
#include <linux/module.h>
#include <linux/errno.h>


void default_reboot(char * cmd)
{
	/* nothing */
}

void default_halt(void)
{
	/* nothing */
}

int default_suspend(int state, int flags)
{
	return -ENOSYS;
}

static struct platform_t default_platform = {
	.name		= "Default Platform",
	.suspend_states	= 0,
	.reboot		= default_reboot,
	.halt		= default_halt,
	.power_off	= default_halt,
	.suspend	= default_suspend,
	.idle		= default_idle,
};

struct platform_t * platform = &default_platform;
static spinlock_t platform_lock = SPIN_LOCK_UNLOCKED;

/**
 * set_platform_driver - set the platform driver.
 * @pf:	driver to set it to
 *
 * Return -EEXIST if someone else already owns it.
 */
int set_platform_driver(struct platform_t * pf)
{
	if (!pf)
		return -EINVAL;
	spin_lock(&platform_lock);
	if (platform != &default_platform) {
		spin_unlock(&platform_lock);
		return -EEXIST;
	}
	platform = pf;
	spin_unlock(&platform_lock);
	return 0;
}

void remove_platform_driver(struct platform_t * pf)
{
	spin_lock(&platform_lock);
	if (platform == pf)
		platform = &default_platform;
	spin_unlock(&platform_lock);
}

EXPORT_SYMBOL(default_reboot);
EXPORT_SYMBOL(default_halt);
EXPORT_SYMBOL(default_suspend);

EXPORT_SYMBOL(platform);
EXPORT_SYMBOL(set_platform_driver);
EXPORT_SYMBOL(remove_platform_driver);
