/*
 *  c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * /dev/nvram driver for PPC64
 *
 * This perhaps should live in drivers/char
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/nvram.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/nvram.h>
#include <asm/rtas.h>
#include <asm/prom.h>

static unsigned int rtas_nvram_size;
static unsigned int nvram_fetch, nvram_store;
static char nvram_buf[4];	/* assume this is in the first 4GB */

static loff_t nvram_llseek(struct file *file, loff_t offset, int origin)
{
	switch (origin) {
	case 1:
		offset += file->f_pos;
		break;
	case 2:
		offset += rtas_nvram_size;
		break;
	}
	if (offset < 0)
		return -EINVAL;
	file->f_pos = offset;
	return file->f_pos;
}


static ssize_t read_nvram(struct file *file, char *buf,
			  size_t count, loff_t *ppos)
{
	unsigned int i;
	unsigned long len;
	char *p = buf;

	if (verify_area(VERIFY_WRITE, buf, count))
		return -EFAULT;
	if (*ppos >= rtas_nvram_size)
		return 0;
	for (i = *ppos; count > 0 && i < rtas_nvram_size; ++i, ++p, --count) {
		if ((rtas_call(nvram_fetch, 3, 2, &len, i, __pa(nvram_buf), 1) != 0) ||
		    len != 1)
			return -EIO;
		if (__put_user(nvram_buf[0], p))
			return -EFAULT;
	}
	*ppos = i;
	return p - buf;
}

static ssize_t write_nvram(struct file *file, const char *buf,
			   size_t count, loff_t *ppos)
{
	unsigned int i;
	unsigned long len;
	const char *p = buf;
	char c;

	if (verify_area(VERIFY_READ, buf, count))
		return -EFAULT;
	if (*ppos >= rtas_nvram_size)
		return 0;
	for (i = *ppos; count > 0 && i < rtas_nvram_size; ++i, ++p, --count) {
		if (__get_user(c, p))
			return -EFAULT;
		nvram_buf[0] = c;
		if ((rtas_call(nvram_store, 3, 2, &len, i, __pa(nvram_buf), 1) != 0) ||
		    len != 1)
			return -EIO;
	}
	*ppos = i;
	return p - buf;
}

static int nvram_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

struct file_operations nvram_fops = {
	.owner =	THIS_MODULE,
	.llseek =	nvram_llseek,
	.read =		read_nvram,
	.write =	write_nvram,
	.ioctl =	nvram_ioctl,
};

static struct miscdevice nvram_dev = {
	NVRAM_MINOR,
	"nvram",
	&nvram_fops
};

int __init nvram_init(void)
{
	struct device_node *nvram;
	unsigned int *nbytes_p, proplen;
	if ((nvram = find_type_devices("nvram")) != NULL) {
		nbytes_p = (unsigned int *)get_property(nvram, "#bytes", &proplen);
		if (nbytes_p && proplen == sizeof(unsigned int)) {
			rtas_nvram_size = *nbytes_p;
		}
	}
	nvram_fetch = rtas_token("nvram-fetch");
	nvram_store = rtas_token("nvram-store");
	printk(KERN_INFO "PPC64 nvram contains %d bytes\n", rtas_nvram_size);

	return misc_register(&nvram_dev);
}

void __exit nvram_cleanup(void)
{
        misc_deregister( &nvram_dev );
}

module_init(nvram_init);
module_exit(nvram_cleanup);
MODULE_LICENSE("GPL");
