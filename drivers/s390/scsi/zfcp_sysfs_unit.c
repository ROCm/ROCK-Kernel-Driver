/*
 * linux/drivers/s390/scsi/zfcp_sysfs_unit.c
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * sysfs unit related routines
 *
 * Copyright (C) 2003 IBM Entwicklung GmbH, IBM Corporation
 * Authors:
 *      Martin Peschke <mpeschke@de.ibm.com>
 *	Heiko Carstens <heiko.carstens@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define ZFCP_SYSFS_UNIT_C_REVISION "$Revision: 1.17 $"

#include <linux/init.h>
#include <linux/module.h>
#include <asm/ccwdev.h>
#include "zfcp_ext.h"
#include "zfcp_def.h"

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_CONFIG

/**
 * zfcp_sysfs_unit_release - gets called when a struct device unit is released
 * @dev: pointer to belonging device
 */
void
zfcp_sysfs_unit_release(struct device *dev)
{
	struct zfcp_unit *unit;

	unit = dev_get_drvdata(dev);
	zfcp_unit_dequeue(unit);
	return;
}

/**
 * ZFCP_DEFINE_UNIT_ATTR
 * @_name:   name of show attribute
 * @_format: format string
 * @_value:  value to print
 *
 * Generates attribute for a unit.
 */
#define ZFCP_DEFINE_UNIT_ATTR(_name, _format, _value)                    \
static ssize_t zfcp_sysfs_unit_##_name##_show(struct device *dev,        \
                                              char *buf)                 \
{                                                                        \
        struct zfcp_unit *unit;                                          \
                                                                         \
        unit = dev_get_drvdata(dev);                                     \
        return sprintf(buf, _format, _value);                            \
}                                                                        \
                                                                         \
static DEVICE_ATTR(_name, S_IRUGO, zfcp_sysfs_unit_##_name##_show, NULL);

ZFCP_DEFINE_UNIT_ATTR(status, "0x%08x\n", atomic_read(&unit->status));
ZFCP_DEFINE_UNIT_ATTR(scsi_lun, "0x%x\n", unit->scsi_lun);

/**
 * zfcp_sysfs_unit_failed_store - failed state of unit
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 * @count: number of bytes in buffer
 *
 * Store function of the "failed" attribute of a unit.
 * If a "0" gets written to "failed", error recovery will be
 * started for the belonging unit.
 */
static ssize_t
zfcp_sysfs_unit_failed_store(struct device *dev, const char *buf, size_t count)
{
	struct zfcp_unit *unit;
	unsigned int val;
	char *endp;
	int retval = 0;

	down(&zfcp_data.config_sema);
	unit = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status)) {
		retval = -EBUSY;
		goto out;
	}

	val = simple_strtoul(buf, &endp, 0);
	if (((endp + 1) < (buf + count)) || (val != 0)) {
		retval = -EINVAL;
		goto out;
	}

	/* restart error recovery only if adapter is online */
	if (unit->port->adapter->ccw_device->online != 1) {
		retval = -ENXIO;
		goto out;
	}
	zfcp_erp_modify_unit_status(unit, ZFCP_STATUS_COMMON_RUNNING, ZFCP_SET);
	zfcp_erp_unit_reopen(unit, ZFCP_STATUS_COMMON_ERP_FAILED);
 out:
	up(&zfcp_data.config_sema);
	return retval ? retval : count;
}

/**
 * zfcp_sysfs_unit_failed_show - failed state of unit
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 *
 * Show function of "failed" attribute of unit. Will be
 * "0" if unit is working, otherwise "1".
 */
static ssize_t
zfcp_sysfs_unit_failed_show(struct device *dev, char *buf)
{
	struct zfcp_unit *unit;

	unit = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &unit->status))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static DEVICE_ATTR(failed, S_IWUSR | S_IRUGO, zfcp_sysfs_unit_failed_show,
		   zfcp_sysfs_unit_failed_store);

/**
 * zfcp_sysfs_unit_in_recovery_show - recovery state of unit
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 *
 * Show function of "in_recovery" attribute of unit. Will be
 * "0" if no error recovery is pending for unit, otherwise "1".
 */
static ssize_t
zfcp_sysfs_unit_in_recovery_show(struct device *dev, char *buf)
{
	struct zfcp_unit *unit;

	unit = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &unit->status))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static DEVICE_ATTR(in_recovery, S_IRUGO, zfcp_sysfs_unit_in_recovery_show,
		   NULL);

static struct attribute *zfcp_unit_attrs[] = {
	&dev_attr_scsi_lun.attr,
	&dev_attr_failed.attr,
	&dev_attr_in_recovery.attr,
	&dev_attr_status.attr,
	NULL
};

static struct attribute_group zfcp_unit_attr_group = {
	.attrs = zfcp_unit_attrs,
};

/** 
 * zfcp_sysfs_create_unit_files - create sysfs unit files
 * @dev: pointer to belonging device
 *
 * Create all attributes of the sysfs representation of a unit.
 */
int
zfcp_sysfs_unit_create_files(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &zfcp_unit_attr_group);
}

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
