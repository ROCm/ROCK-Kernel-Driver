/*
 *  acpi.h - ACPI driver interface
 *
 *  Copyright (C) 1999 Andrew Henroid
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H

#include <linux/types.h>
#include <linux/ioctl.h>
#ifdef __KERNEL__
#include <linux/sched.h>
#include <linux/wait.h>
#endif /* __KERNEL__ */

u64 acpi_get_rsdp_ptr(void);

/*
 * System sleep states
 */
enum
{
	ACPI_S0, /* working */
	ACPI_S1, /* sleep */
	ACPI_S2, /* sleep */
	ACPI_S3, /* sleep */
	ACPI_S4, /* non-volatile sleep */
	ACPI_S5, /* soft-off */
};

typedef int acpi_sstate_t;

/*
 * Device states
 */
enum
{
	ACPI_D0, /* fully-on */
	ACPI_D1, /* partial-on */
	ACPI_D2, /* partial-on */
	ACPI_D3, /* fully-off */
};

typedef int acpi_dstate_t;

/* RSDP location */
#define ACPI_BIOS_ROM_BASE (0x0e0000)
#define ACPI_BIOS_ROM_END  (0x100000)

/* Table signatures */
#define ACPI_RSDP1_SIG 0x20445352 /* 'RSD ' */
#define ACPI_RSDP2_SIG 0x20525450 /* 'PTR ' */
#define ACPI_RSDT_SIG  0x54445352 /* 'RSDT' */
#define ACPI_FADT_SIG  0x50434146 /* 'FACP' */
#define ACPI_DSDT_SIG  0x54445344 /* 'DSDT' */
#define ACPI_FACS_SIG  0x53434146 /* 'FACS' */

#define ACPI_SIG_LEN		4
#define ACPI_FADT_SIGNATURE	"FACP"

/* PM1_STS/EN flags */
#define ACPI_TMR    0x0001
#define ACPI_BM	    0x0010
#define ACPI_GBL    0x0020
#define ACPI_PWRBTN 0x0100
#define ACPI_SLPBTN 0x0200
#define ACPI_RTC    0x0400
#define ACPI_WAK    0x8000

/* PM1_CNT flags */
#define ACPI_SCI_EN   0x0001
#define ACPI_BM_RLD   0x0002
#define ACPI_GBL_RLS  0x0004
#define ACPI_SLP_TYP0 0x0400
#define ACPI_SLP_TYP1 0x0800
#define ACPI_SLP_TYP2 0x1000
#define ACPI_SLP_EN   0x2000

#define ACPI_SLP_TYP_MASK  0x1c00
#define ACPI_SLP_TYP_SHIFT 10

/* PM_TMR masks */
#define ACPI_TMR_VAL_EXT 0x00000100
#define ACPI_TMR_MASK	 0x00ffffff
#define ACPI_TMR_HZ	 3580000 /* 3.58 MHz */

/* strangess to avoid integer overflow */
#define ACPI_MICROSEC_TO_TMR_TICKS(val) \
  (((val) * (ACPI_TMR_HZ / 10000)) / 100)
#define ACPI_TMR_TICKS_TO_MICROSEC(ticks) \
  (((ticks) * 100) / (ACPI_TMR_HZ / 10000))

/* PM2_CNT flags */
#define ACPI_ARB_DIS 0x01

/* FADT flags */
#define ACPI_WBINVD	  0x00000001
#define ACPI_WBINVD_FLUSH 0x00000002
#define ACPI_PROC_C1	  0x00000004
#define ACPI_P_LVL2_UP	  0x00000008
#define ACPI_PWR_BUTTON	  0x00000010
#define ACPI_SLP_BUTTON	  0x00000020
#define ACPI_FIX_RTC	  0x00000040
#define ACPI_RTC_64	  0x00000080
#define ACPI_TMR_VAL_EXT  0x00000100
#define ACPI_DCK_CAP	  0x00000200

/* FADT BOOT_ARCH flags */
#define FADT_BOOT_ARCH_LEGACY_DEVICES	0x0001
#define FADT_BOOT_ARCH_KBD_CONTROLLER	0x0002

/* FACS flags */
#define ACPI_S4BIOS	  0x00000001

/* processor block offsets */
#define ACPI_P_CNT	  0x00000000
#define ACPI_P_LVL2	  0x00000004
#define ACPI_P_LVL3	  0x00000005

/* C-state latencies (microseconds) */
#define ACPI_MAX_P_LVL2_LAT 100
#define ACPI_MAX_P_LVL3_LAT 1000
#define ACPI_INFINITE_LAT   (~0UL)

/*
 * Sysctl declarations
 */

enum
{
	CTL_ACPI = 10
};

enum
{
	ACPI_FADT = 1,
	ACPI_DSDT,
	ACPI_PM1_ENABLE,
	ACPI_GPE_ENABLE,
	ACPI_GPE_LEVEL,
	ACPI_EVENT,
	ACPI_P_BLK,
	ACPI_ENTER_LVL2_LAT,
	ACPI_ENTER_LVL3_LAT,
	ACPI_P_LVL2_LAT,
	ACPI_P_LVL3_LAT,
	ACPI_C1_TIME,
	ACPI_C2_TIME,
	ACPI_C3_TIME,
	ACPI_S0_SLP_TYP,
	ACPI_S1_SLP_TYP,
	ACPI_S5_SLP_TYP,
	ACPI_SLEEP,
	ACPI_FACS,
	ACPI_XSDT,
	ACPI_PMTIMER,
	ACPI_BATTERY,
};

#define ACPI_SLP_TYP_DISABLED	(~0UL)

#endif /* _LINUX_ACPI_H */
