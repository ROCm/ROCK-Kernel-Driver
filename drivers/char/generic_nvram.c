/*
 * Generic /dev/nvram driver for architectures providing some
 * "generic" hooks, that is :
 *
 * nvram_read_byte, nvram_write_byte, nvram_sync
 *
 * Note that an additional hook is supported for PowerMac only
 * for getting the nvram "partition" informations
 *
 */

#define NVRAM_VERSION "1.1"

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <asm/nvram.h>
#include <asm/semaphore.h>

#define NVRAM_SIZE	8192

static DECLARE_MUTEX(nvram_sem);

static loff_t nvram_llseek(struct file *file, loff_t offset, int origin)
{
	down(&nvram_sem);
	switch (origin) {
	case 1:
		offset += file->f_pos;
		break;
	case 2:
		offset += NVRAM_SIZE;
		break;
	}
	if (offset < 0) {
		up(&nvram_sem);
		return -EINVAL;
	}
	file->f_pos = offset;
	up(&nvram_sem);
	return file->f_pos;
}

static ssize_t read_nvram(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	loff_t i;
	char __user *p = buf;
	
	if (verify_area(VERIFY_WRITE, buf, count))
		return -EFAULT;
		
	down(&nvram_sem);
	/* If we are already off the end then we report 0 anyway .. */
	for (i = *ppos; count > 0 && i < NVRAM_SIZE; ++i, ++p, --count)
		if (__put_user(nvram_read_byte(i), p))
		{
			up(&nvram_sem);
			return -EFAULT;
		}
	*ppos = i;
	up(&nvram_sem);
	return p - buf;
}

static ssize_t write_nvram(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	loff_t i;
	const char __user *p = buf;
	char c;

	if (verify_area(VERIFY_READ, buf, count))
		return -EFAULT;
		
	down(&nvram_sem);
	/* if *ppos > end then we return 0 anyway */
	for (i = *ppos; count > 0 && i < NVRAM_SIZE; ++i, ++p, --count) {
		if (__get_user(c, p)) {
			up(&nvram_sem);
			return -EFAULT;
		}
		nvram_write_byte(c, i);
	}
	*ppos = i;
	up(&nvram_sem);
	return p - buf;
}

static int nvram_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	switch(cmd) {
#ifdef CONFIG_PPC_PMAC
	case OBSOLETE_PMAC_NVRAM_GET_OFFSET:
		printk(KERN_WARNING "nvram: Using obsolete PMAC_NVRAM_GET_OFFSET ioctl\n");
	case IOC_NVRAM_GET_OFFSET: {
		int part, offset;

		if (_machine != _MACH_Pmac)
			return -EINVAL;
		if (copy_from_user(&part, (void __user*)arg, sizeof(part)) != 0)
			return -EFAULT;
		if (part < pmac_nvram_OF || part > pmac_nvram_NR)
			return -EINVAL;
		offset = pmac_get_partition(part);
		if (copy_to_user((void __user*)arg, &offset, sizeof(offset)) != 0)
			return -EFAULT;
		break;
	}
#endif /* CONFIG_PPC_PMAC */
	case IOC_NVRAM_SYNC:
		nvram_sync();
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

struct file_operations nvram_fops = {
	.owner		= THIS_MODULE,
	.llseek		= nvram_llseek,
	.read		= read_nvram,
	.write		= write_nvram,
	.ioctl		= nvram_ioctl,
};

static struct miscdevice nvram_dev = {
	NVRAM_MINOR,
	"nvram",
	&nvram_fops
};

int __init nvram_init(void)
{
	printk(KERN_INFO "Macintosh non-volatile memory driver v%s\n",
		NVRAM_VERSION);
	return misc_register(&nvram_dev);
}

void __exit nvram_cleanup(void)
{
        misc_deregister( &nvram_dev );
}

module_init(nvram_init);
module_exit(nvram_cleanup);
MODULE_LICENSE("GPL");
