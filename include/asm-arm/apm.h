/* -*- linux-c -*-
 *
 * (C) 2003 zecke@handhelds.org
 *
 * GPL version 2
 *
 * based on arch/arm/kernel/apm.c
 * factor out the information needed by architectures to provide
 * apm status
 *
 *
 */
#ifndef ARM_ASM_SA1100_APM_H
#define ARM_ASM_SA1100_APM_H

#include <linux/config.h>

#ifdef CONFIG_APM


#define APM_AC_OFFLINE 0
#define APM_AC_ONLINE 1
#define APM_AC_BACKUP 2
#define APM_AC_UNKNOWN 0xFF

#define APM_BATTERY_STATUS_HIGH 0
#define APM_BATTERY_STATUS_LOW  1
#define APM_BATTERY_STATUS_CRITICAL 2
#define APM_BATTERY_STATUS_CHARGING 3
#define APM_BATTERY_STATUS_UNKNOWN 0xFF

#define APM_BATTERY_LIFE_UNKNOWN 0xFFFF
#define APM_BATTERY_LIFE_MINUTES 0x8000
#define APM_BATTERY_LIFE_VALUE_MASK 0x7FFF

/*
 * This structure gets filled in by the machine specific 'get_power_status'
 * implementation.  Any fields which are not set default to a safe value.
 */
struct apm_power_info {
	unsigned char	ac_line_status;
	unsigned char	battery_status;
	unsigned char	battery_flag;
	unsigned char	battery_life;
	int		time;
	int		units;
};

/*
 * This allows machines to provide their own "apm get power status" function.
 */
extern void (*apm_get_power_status)(struct apm_power_info *);
#endif


#endif
