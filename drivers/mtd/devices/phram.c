/**
 *
 * $Id: phram.c,v 1.1 2003/08/21 17:52:30 joern Exp $
 *
 * Copyright (c) Jochen Schaeuble <psionic@psionic.de>
 * 07/2003	rewritten by Joern Engel <joern@wh.fh-wedel.de>
 *
 * DISCLAIMER:  This driver makes use of Rusty's excellent module code,
 * so it will not work for 2.4 without changes and it wont work for 2.4
 * as a module without major changes.  Oh well!
 *
 * Usage:
 *
 * one commend line parameter per device, each in the form:
 *   phram=<name>,<start>,<len>
 * <name> may be up to 63 characters.
 * <start> and <len> can be octal, decimal or hexadecimal.  If followed
 * by "k", "M" or "G", the numbers will be interpreted as kilo, mega or
 * gigabytes.
 *
 */

#include <asm/io.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mtd/mtd.h>

#define ERROR(fmt, args...) printk(KERN_ERR "phram: " fmt , ## args)

struct phram_mtd_list {
	struct list_head list;
	struct mtd_info *mtdinfo;
};

static LIST_HEAD(phram_list);



int phram_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	u_char *start = (u_char *)mtd->priv;

	if (instr->addr + instr->len > mtd->size)
		return -EINVAL;
	
	memset(start + instr->addr, 0xff, instr->len);

	/* This'll catch a few races. Free the thing before returning :) 
	 * I don't feel at all ashamed. This kind of thing is possible anyway
	 * with flash, but unlikely.
	 */

	instr->state = MTD_ERASE_DONE;

	if (instr->callback)
		(*(instr->callback))(instr);
	else
		kfree(instr);

	return 0;
}

int phram_point(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char **mtdbuf)
{
	u_char *start = (u_char *)mtd->priv;

	if (from + len > mtd->size)
		return -EINVAL;
	
	*mtdbuf = start + from;
	*retlen = len;
	return 0;
}

void phram_unpoint(struct mtd_info *mtd, u_char *addr, loff_t from, size_t len)
{
}

int phram_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char *buf)
{
	u_char *start = (u_char *)mtd->priv;

	if (from + len > mtd->size)
		return -EINVAL;
	
	memcpy(buf, start + from, len);

	*retlen = len;
	return 0;
}

int phram_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	u_char *start = (u_char *)mtd->priv;

	if (to + len > mtd->size)
		return -EINVAL;
	
	memcpy(start + to, buf, len);

	*retlen = len;
	return 0;
}



static void unregister_devices(void)
{
	struct phram_mtd_list *this;

	list_for_each_entry(this, &phram_list, list) {
		del_mtd_device(this->mtdinfo);
		iounmap(this->mtdinfo->priv);
		kfree(this->mtdinfo);
		kfree(this);
	}
}

static int register_device(char *name, unsigned long start, unsigned long len)
{
	struct phram_mtd_list *new;
	int ret = -ENOMEM;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		goto out0;

	new->mtdinfo = kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
	if (!new->mtdinfo)
		goto out1;
	
	memset(new->mtdinfo, 0, sizeof(struct mtd_info));

	ret = -EIO;
	new->mtdinfo->priv = ioremap(start, len);
	if (!new->mtdinfo->priv) {
		ERROR("ioremap failed\n");
		goto out2;
	}


	new->mtdinfo->name = name;
	new->mtdinfo->size = len;
	new->mtdinfo->flags = MTD_CAP_RAM | MTD_ERASEABLE | MTD_VOLATILE;
        new->mtdinfo->erase = phram_erase;
	new->mtdinfo->point = phram_point;
	new->mtdinfo->unpoint = phram_unpoint;
	new->mtdinfo->read = phram_read;
	new->mtdinfo->write = phram_write;
	new->mtdinfo->owner = THIS_MODULE;
	new->mtdinfo->type = MTD_RAM;
	new->mtdinfo->erasesize = 0x0;

	ret = -EAGAIN;
	if (add_mtd_device(new->mtdinfo)) {
		ERROR("Failed to register new device\n");
		goto out3;
	}

	list_add_tail(&new->list, &phram_list);
	return 0;	

out3:
	iounmap(new->mtdinfo->priv);
out2:
	kfree(new->mtdinfo);
out1:
	kfree(new);
out0:
	return ret;
}

static int ustrtoul(const char *cp, char **endp, unsigned int base)
{
	unsigned long result = simple_strtoul(cp, endp, base);

	switch (**endp) {
	case 'G':
		result *= 1024;
	case 'M':
		result *= 1024;
	case 'k':
		result *= 1024;
		endp++;
	}
	return result;
}

static int parse_num32(uint32_t *num32, const char *token)
{
	char *endp;
	unsigned long n;

	n = ustrtoul(token, &endp, 0);
	if (*endp)
		return -EINVAL;

	*num32 = n;
	return 0;
}

static int parse_name(char **pname, const char *token)
{
	size_t len;
	char *name;

	len = strlen(token) + 1;
	if (len > 64)
		return -ENOSPC;

	name = kmalloc(len, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	strcpy(name, token);

	*pname = name;
	return 0;
}

#define parse_err(fmt, args...) do {	\
	ERROR(fmt , ## args);	\
	return 0;		\
} while (0)

static int phram_setup(const char *val, struct kernel_param *kp)
{
	char buf[64+12+12], *str = buf;
	char *token[3];
	char *name;
	uint32_t start;
	uint32_t len;
	int i, ret;

	if (strnlen(val, sizeof(str)) >= sizeof(str))
		parse_err("parameter too long\n");

	strcpy(str, val);

	for (i=0; i<3; i++)
		token[i] = strsep(&str, ",");

	if (str)
		parse_err("too many arguments\n");

	if (!token[2])
		parse_err("not enough arguments\n");

	ret = parse_name(&name, token[0]);
	if (ret == -ENOMEM)
		parse_err("out of memory\n");
	if (ret == -ENOSPC)
		parse_err("name too long\n");
	if (ret)
		return 0;

	ret = parse_num32(&start, token[1]);
	if (ret)
		parse_err("illegal start address\n");

	ret = parse_num32(&len, token[2]);
	if (ret)
		parse_err("illegal device length\n");

	register_device(name, start, len);

	return 0;
}

module_param_call(phram, phram_setup, NULL, NULL, 000);
MODULE_PARM_DESC(phram, "Memory region to map. \"map=<name>,<start><length>\"");

/*
 * Just for compatibility with slram, this is horrible and should go someday.
 */
static int __init slram_setup(const char *val, struct kernel_param *kp)
{
	char buf[256], *str = buf;

	if (!val || !val[0])
		parse_err("no arguments to \"slram=\"\n");

	if (strnlen(val, sizeof(str)) >= sizeof(str))
		parse_err("parameter too long\n");

	strcpy(str, val);

	while (str) {
		char *token[3];
		char *name;
		uint32_t start;
		uint32_t len;
		int i, ret;

		for (i=0; i<3; i++) {
			token[i] = strsep(&str, ",");
			if (token[i])
				continue;
			parse_err("wrong number of arguments to \"slram=\"\n");
		}

		/* name */
		ret = parse_name(&name, token[0]);
		if (ret == -ENOMEM)
			parse_err("of memory\n");
		if (ret == -ENOSPC)
			parse_err("too long\n");
		if (ret)
			return 1;

		/* start */
		ret = parse_num32(&start, token[1]);
		if (ret)
			parse_err("illegal start address\n");

		/* len */
		if (token[2][0] == '+')
			ret = parse_num32(&len, token[2] + 1);
		else
			ret = parse_num32(&len, token[2]);

		if (ret)
			parse_err("illegal device length\n");

		if (token[2][0] != '+') {
			if (len < start)
				parse_err("end < start\n");
			len -= start;
		}

		register_device(name, start, len);
	}
	return 1;
}

module_param_call(slram, slram_setup, NULL, NULL, 000);
MODULE_PARM_DESC(slram, "List of memory regions to map. \"map=<name>,<start><length/end>\"");


int __init init_phram(void)
{
	printk(KERN_ERR "phram loaded\n");
	return 0;
}

static void __exit cleanup_phram(void)
{
	unregister_devices();
}

module_init(init_phram);
module_exit(cleanup_phram);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jörn Engel <joern@wh.fh-wedel.de>");
MODULE_DESCRIPTION("MTD driver for physical RAM");
