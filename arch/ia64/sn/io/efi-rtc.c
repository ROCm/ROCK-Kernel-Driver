/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Silicon Graphics, Inc.
 * Copyright (C) 2001 by Ralf Baechle
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <asm/efi.h>
#include <asm/sn/klclock.h>

/*
 * No locking necessary when this is called from efirtc which protects us
 * from racing by efi_rtc_lock.
 */
#define __swizzle(addr) ((u8 *)((unsigned long)(addr) ^ 3))
#define read_io_port(addr) (*(volatile u8 *) __swizzle(addr))
#define write_io_port(addr, data) (*(volatile u8 *) __swizzle(addr) = (data))

#define TOD_SGS_M48T35		1
#define TOD_DALLAS_DS1386	2

static unsigned long nvram_base = 0;
static int tod_chip_type;

static int
get_tod_chip_type(void)
{
	unsigned char testval;

	write_io_port(RTC_DAL_CONTROL_ADDR, RTC_DAL_UPDATE_DISABLE);
	write_io_port(RTC_DAL_DAY_ADDR, 0xff);
	write_io_port(RTC_DAL_CONTROL_ADDR, RTC_DAL_UPDATE_ENABLE);

	testval = read_io_port(RTC_DAL_DAY_ADDR);
	if (testval == 0xff)
		return TOD_SGS_M48T35;

	return TOD_DALLAS_DS1386;
}

efi_status_t
ioc3_get_time(efi_time_t *time, efi_time_cap_t *caps)
{
	if (!nvram_base) {
		printk(KERN_CRIT "nvram_base is zero\n");
		return EFI_UNSUPPORTED;
	}

	memset(time, 0, sizeof(*time));

	switch (tod_chip_type) {
	case TOD_SGS_M48T35:
		write_io_port(RTC_SGS_CONTROL_ADDR, RTC_SGS_READ_PROTECT);

		time->year = BCD_TO_INT(read_io_port(RTC_SGS_YEAR_ADDR)) + YRREF;
		time->month = BCD_TO_INT(read_io_port(RTC_SGS_MONTH_ADDR));
		time->day = BCD_TO_INT(read_io_port(RTC_SGS_DATE_ADDR));
		time->hour = BCD_TO_INT(read_io_port(RTC_SGS_HOUR_ADDR));
		time->minute = BCD_TO_INT(read_io_port(RTC_SGS_MIN_ADDR));
		time->second = BCD_TO_INT(read_io_port(RTC_SGS_SEC_ADDR));
		time->nanosecond = 0;

		write_io_port(RTC_SGS_CONTROL_ADDR, 0);
		break;

	case TOD_DALLAS_DS1386:
		write_io_port(RTC_DAL_CONTROL_ADDR, RTC_DAL_UPDATE_DISABLE);

		time->nanosecond = 0;
		time->second = BCD_TO_INT(read_io_port(RTC_DAL_SEC_ADDR));
		time->minute = BCD_TO_INT(read_io_port(RTC_DAL_MIN_ADDR));
		time->hour = BCD_TO_INT(read_io_port(RTC_DAL_HOUR_ADDR));
		time->day = BCD_TO_INT(read_io_port(RTC_DAL_DATE_ADDR));
		time->month = BCD_TO_INT(read_io_port(RTC_DAL_MONTH_ADDR));
		time->year = BCD_TO_INT(read_io_port(RTC_DAL_YEAR_ADDR)) + YRREF;

		write_io_port(RTC_DAL_CONTROL_ADDR, RTC_DAL_UPDATE_ENABLE);
		break;

	default:
		break;
	}

	if (caps) {
		caps->resolution = 50000000;	/*  50PPM */
		caps->accuracy = 1000;		/*  1ms */
		caps->sets_to_zero = 0;
	}

	return EFI_SUCCESS;
}

static efi_status_t ioc3_set_time (efi_time_t *t)
{
	if (!nvram_base) {
		printk(KERN_CRIT "nvram_base is zero\n");
		return EFI_UNSUPPORTED;
	}

	switch (tod_chip_type) {
	case TOD_SGS_M48T35:
		write_io_port(RTC_SGS_CONTROL_ADDR, RTC_SGS_WRITE_ENABLE);
        	write_io_port(RTC_SGS_YEAR_ADDR, INT_TO_BCD((t->year - YRREF)));
		write_io_port(RTC_SGS_MONTH_ADDR,INT_TO_BCD(t->month));
		write_io_port(RTC_SGS_DATE_ADDR, INT_TO_BCD(t->day));
		write_io_port(RTC_SGS_HOUR_ADDR, INT_TO_BCD(t->hour));
		write_io_port(RTC_SGS_MIN_ADDR,  INT_TO_BCD(t->minute));
		write_io_port(RTC_SGS_SEC_ADDR,  INT_TO_BCD(t->second));
		write_io_port(RTC_SGS_CONTROL_ADDR, 0);
		break;

	case TOD_DALLAS_DS1386:
		write_io_port(RTC_DAL_CONTROL_ADDR, RTC_DAL_UPDATE_DISABLE);
		write_io_port(RTC_DAL_SEC_ADDR,  INT_TO_BCD(t->second));
		write_io_port(RTC_DAL_MIN_ADDR,  INT_TO_BCD(t->minute));
		write_io_port(RTC_DAL_HOUR_ADDR, INT_TO_BCD(t->hour));
		write_io_port(RTC_DAL_DATE_ADDR, INT_TO_BCD(t->day));
		write_io_port(RTC_DAL_MONTH_ADDR,INT_TO_BCD(t->month));
		write_io_port(RTC_DAL_YEAR_ADDR, INT_TO_BCD((t->year - YRREF)));
		write_io_port(RTC_DAL_CONTROL_ADDR, RTC_DAL_UPDATE_ENABLE);
		break;

	default:
		break;
	}

	return EFI_SUCCESS;
}

/* The following two are not supported atm.  */
static efi_status_t
ioc3_get_wakeup_time (efi_bool_t *enabled, efi_bool_t *pending, efi_time_t *tm)
{
	return EFI_UNSUPPORTED;
}

static efi_status_t
ioc3_set_wakeup_time (efi_bool_t enabled, efi_time_t *tm)
{
	return EFI_UNSUPPORTED;
}

/*
 * It looks like the master IOC3 is usually on bus 0, device 4.  Hope
 * that's right
 */
static __init int efi_ioc3_time_init(void)
{
	struct pci_dev *dev;
	static struct ioc3 *ioc3;

	dev = pci_find_slot(0, PCI_DEVFN(4, 0));
	if (!dev) {
		printk(KERN_CRIT "Couldn't find master IOC3\n");

		return -ENODEV;
	}

	ioc3 = ioremap(pci_resource_start(dev, 0), pci_resource_len(dev, 0));
	nvram_base = (unsigned long) ioc3 + IOC3_BYTEBUS_DEV0;

	tod_chip_type = get_tod_chip_type();
	if (tod_chip_type == 1)
		printk(KERN_NOTICE "TOD type is SGS M48T35\n");
	else if (tod_chip_type == 2)
		printk(KERN_NOTICE "TOD type is Dallas DS1386\n");
	else
		printk(KERN_CRIT "No or unknown TOD\n");

	efi.get_time = ioc3_get_time;
	efi.set_time = ioc3_set_time;
	efi.get_wakeup_time = ioc3_get_wakeup_time;
	efi.set_wakeup_time = ioc3_set_wakeup_time;

	return 0;
}

module_init(efi_ioc3_time_init);
