/*
 * linux/drivers/firmware/smbios.c
 *  Copyright (C) 2004 Dell Inc.
 *  by Michael Brown <Michael_E_Brown@dell.com>
 *  vim:noet:ts=8:sw=8:filetype=c:textwidth=80:
 *
 * BIOS SMBIOS Table access
 * conformant to DMTF SMBIOS definition
 *   at http://www.dmtf.org/standards/smbios
 *
 * This code takes information provided by SMBIOS tables
 * and presents it in sysfs as:
 *    /sys/firmware/smbios
 *			|--> /table_entry_point
 *			|--> /table
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "smbios.h"

MODULE_AUTHOR("Michael Brown <Michael_E_Brown@Dell.com>");
MODULE_DESCRIPTION("sysfs interface to SMBIOS information");
MODULE_LICENSE("GPL");

#define SMBIOS_VERSION "1.0 2004-04-19"

struct smbios_device {
	struct smbios_table_entry_point table_eps;
	unsigned int smbios_table_real_length;
};

/* there shall be only one */
static struct smbios_device the_smbios_device;

#define to_smbios_device(obj) container_of(obj,struct smbios_device,kobj)

/* don't currently have any "normal" attributes, so we don't need a way to
 * show them. */
static struct sysfs_ops smbios_attr_ops = { };

static __init int
checksum_eps(struct smbios_table_entry_point *table_eps)
{
	u8 *p = (u8 *)table_eps;
	u8 checksum = 0;
	int i=0;
	for (i=0; i < table_eps->eps_length && i < sizeof(*table_eps); ++i) {
		checksum += p[i];
	}
	return( checksum == 0 );
}

static __init int
find_table_entry_point(struct smbios_device *sdev)
{
	struct smbios_table_entry_point *table_eps = &(sdev->table_eps);
	u32 fp = 0xF0000;
	while (fp < 0xFFFFF) {
		isa_memcpy_fromio(table_eps, fp, sizeof(*table_eps));
		if (memcmp(table_eps->anchor, "_SM_", 4)==0 &&
					checksum_eps(table_eps)) {
			return 0;
		}
		fp += 16;
	}

	printk(KERN_INFO "SMBIOS table entry point not found in "
			"0xF0000 - 0xFFFFF\n");
	return -ENODEV;
}

static __init int
find_table_max_address(struct smbios_device *sdev)
{
	/* break out on one of three conditions:
	 *   -- hit table_eps.table_length
	 *   -- hit number of items that table claims we have
	 *   -- hit structure type 127
	 */

	u8 *buf = ioremap(sdev->table_eps.table_address,
			sdev->table_eps.table_length);
	u8 *ptr = buf;
	int count = 0, keep_going = 1;
	int max_count = sdev->table_eps.table_num_structs;
	int max_length = sdev->table_eps.table_length;
	while(keep_going && ((ptr - buf) <= max_length) && count < max_count){
		if( ptr[0] == 0x7F )   /* ptr[0] is type */
			keep_going = 0;

		ptr += ptr[1]; /* ptr[1] is length, skip structure */
		/* skip strings at end of structure */
		while((ptr-buf) < max_length && (ptr[0] || ptr[1]))
			++ptr;

		/* string area ends in double-null. skip it. */
		ptr += 2;
		++count;
	}
	sdev->smbios_table_real_length = (ptr - buf);
	iounmap(buf);

	if( count != max_count )
		printk(KERN_INFO "Warning: SMBIOS table structure count"
				" does not match count specified in the"
				" table entry point.\n"
				" Table entry point count: %d\n"
				" Actual count: %d\n",
				max_count, count );

	if(keep_going != 0)
		printk(KERN_INFO "Warning: SMBIOS table does not end with a"
				" structure type 127. This may indicate a"
				" truncated table.\n");

	if(sdev->smbios_table_real_length != max_length)
		printk(KERN_INFO "Warning: BIOS specified SMBIOS table length"
				" does not match calculated length.\n"
				" BIOS specified: %d\n"
				" calculated length: %d\n",
				max_length, sdev->smbios_table_real_length);

	return sdev->smbios_table_real_length;
}

static ssize_t
smbios_read_table_entry_point(struct kobject *kobj, char *buffer,
				loff_t pos, size_t size)
{
	struct smbios_device *sdev = &the_smbios_device;
	const char *p = (const char *)&(sdev->table_eps);
	unsigned int count =
		size > sizeof(sdev->table_eps) ?
			sizeof(sdev->table_eps) : size;
	memcpy( buffer, p, count );
	return count;
}

static ssize_t
smbios_read_table(struct kobject *kobj, char *buffer,
				loff_t pos, size_t size)
{
	struct smbios_device *sdev = &the_smbios_device;
	u8 *buf;
	unsigned int count = sdev->smbios_table_real_length - pos;
	int i = 0;
	count = count < size ? count : size;

	if (pos > sdev->smbios_table_real_length)
		return 0;

	buf = ioremap(sdev->table_eps.table_address, sdev->smbios_table_real_length);
	if (buf == NULL)
		return -ENXIO;

	/* memcpy( buffer, buf+pos, count ); */
	for (i = 0; i < count; ++i) {
		buffer[i] = readb( buf+pos+i );
	}

	iounmap(buf);

	return count;
}

static struct bin_attribute tep_attr = {
	.attr = {.name = "table_entry_point", .owner = THIS_MODULE, .mode = 0444},
	.size = sizeof(struct smbios_table_entry_point),
	.read = smbios_read_table_entry_point,
	/* not writeable */
};

static struct bin_attribute table_attr = {
	.attr = { .name = "table", .owner = THIS_MODULE, .mode = 0444 },
	/* size set later, we don't know it here. */
	.read = smbios_read_table,
	/* not writeable */
};

/* no default attributes yet. */
static struct attribute * def_attrs[] = { NULL, };

static struct kobj_type ktype_smbios = {
	.sysfs_ops	= &smbios_attr_ops,
	.default_attrs	= def_attrs,
	/* statically allocated, no release method necessary */
};

static decl_subsys(smbios,&ktype_smbios,NULL);

static void smbios_device_unregister(void)
{
	sysfs_remove_bin_file(&smbios_subsys.kset.kobj, &tep_attr );
	sysfs_remove_bin_file(&smbios_subsys.kset.kobj, &table_attr );
}

static void __init smbios_device_register(void)
{
	sysfs_create_bin_file(&smbios_subsys.kset.kobj, &tep_attr );
	sysfs_create_bin_file(&smbios_subsys.kset.kobj, &table_attr );
}

static int __init
smbios_init(void)
{
	int rc=0;

	printk(KERN_INFO "SMBIOS facility v%s\n", SMBIOS_VERSION );

	rc = find_table_entry_point(&the_smbios_device);
	if (rc)
		return rc;

	table_attr.size = find_table_max_address(&the_smbios_device);

	rc = firmware_register(&smbios_subsys);
	if (rc)
		return rc;

	smbios_device_register();

	return rc;
}

static void __exit
smbios_exit(void)
{
	smbios_device_unregister();
	firmware_unregister(&smbios_subsys);
}

late_initcall(smbios_init);
module_exit(smbios_exit);
