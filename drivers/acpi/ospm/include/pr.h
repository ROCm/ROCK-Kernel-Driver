/******************************************************************************
 *
 * Module Name: processor.h
 *              $Revision: 13 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 Andrew Grover
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

#ifndef __PR_H__
#define __PR_H__

#include <bm.h>


/*****************************************************************************
 *                             Types & Other Defines
 *****************************************************************************/


#define PR_MAX_POWER_STATES	4
#define PR_MAX_THROTTLE_STATES  8
#define PR_MAX_PERF_STATES	32
#define PR_MAX_C2_LATENCY	100
#define PR_MAX_C3_LATENCY	1000


/*
 * Commands:
 * ---------
 */
#define PR_COMMAND_GET_POWER_INFO ((BM_COMMAND) 0x80)
#define PR_COMMAND_SET_POWER_INFO ((BM_COMMAND) 0x81)
#define PR_COMMAND_GET_PERF_INFO ((BM_COMMAND) 0x82)
#define PR_COMMAND_GET_PERF_STATE ((BM_COMMAND) 0x83)
#define PR_COMMAND_SET_PERF_LIMIT ((BM_COMMAND) 0x84)


/*
 * Notifications:
 * --------------
 */
#define PR_NOTIFY_PERF_STATES	((BM_NOTIFY) 0x80)
#define PR_NOTIFY_POWER_STATES	((BM_NOTIFY) 0x81)


/*
 * Performance Control:
 * --------------------
 */
#define PR_PERF_DEC		0x00
#define PR_PERF_INC		0x01
#define PR_PERF_MAX		0xFF


/*
 * Power States:
 * -------------
 */
#define PR_C0			0x00
#define PR_C1			0x01
#define PR_C2			0x02
#define PR_C3			0x03

#define PR_C1_FLAG		0x01;
#define PR_C2_FLAG		0x02;
#define PR_C3_FLAG		0x04;


/*
 * PR_CX_POLICY_VALUES:
 * --------------------
 */
typedef struct
{
	u32			time_threshold;
	u32			count_threshold;
	u32                     bm_threshold;
	u32         		target_state;
	u32			count;
} PR_CX_POLICY_VALUES;


/*
 * PR_CX:
 * ------
 */
typedef struct
{
	u32                     latency;
	u32                     utilization;
	u8                      is_valid;
	PR_CX_POLICY_VALUES     promotion;
	PR_CX_POLICY_VALUES     demotion;
} PR_CX;


/*
 * PR_POWER:
 * ---------
 */
typedef struct
{
	ACPI_PHYSICAL_ADDRESS   p_lvl2;
	ACPI_PHYSICAL_ADDRESS   p_lvl3;
	u32                     bm_activity;
	u32                     active_state;
	u32			default_state;
	u32			busy_metric;
	u32                     state_count;
	PR_CX                   state[PR_MAX_POWER_STATES];
} PR_POWER;


/*
 * PR_PERFORMANCE_STATE:
 * ---------------------
 */
typedef struct
{
	u32                     performance;
	u32                     power;
} PR_PERFORMANCE_STATE;


/*
 * PR_PERFORMANCE:
 * ---------------
 */
typedef struct
{
	u32                     active_state;
	u32			thermal_limit;
	u32			power_limit;
	u32                     state_count;
	PR_PERFORMANCE_STATE    state[PR_MAX_PERF_STATES];
} PR_PERFORMANCE;


/*
 * PR_PBLOCK:
 * ----------
 */
typedef struct
{
	u32                     length;
	ACPI_PHYSICAL_ADDRESS   address;
} PR_PBLOCK;


/*
 * PR_CONTEXT:
 * -----------
 */
typedef struct
{
	BM_HANDLE               device_handle;
	acpi_handle             acpi_handle;
	u32                     uid;
	PR_PBLOCK               pblk;
	PR_POWER		power;
	PR_PERFORMANCE		performance;
} PR_CONTEXT;


/******************************************************************************
 *                             Function Prototypes
 *****************************************************************************/

/* processor.c */

acpi_status
pr_initialize(void);

acpi_status
pr_terminate(void);

acpi_status
pr_notify (
	BM_NOTIFY               notify_type,
	BM_HANDLE               device_handle,
	void                    **context);

acpi_status
pr_request(
	BM_REQUEST		*request,
	void                    *context);

/* prpower.c */

void
pr_power_idle (void);

acpi_status
pr_power_add_device (
	PR_CONTEXT              *processor);

acpi_status
pr_power_remove_device (
	PR_CONTEXT              *processor);

acpi_status
pr_power_initialize (void);

acpi_status
pr_power_terminate (void);

/* prperf.c */

acpi_status
pr_perf_get_state (
	PR_CONTEXT              *processor,
	u32                     *state);

acpi_status
pr_perf_set_state (
	PR_CONTEXT              *processor,
	u32                     state);

acpi_status
pr_perf_set_limit (
	PR_CONTEXT              *processor,
	u32                     limit);

acpi_status
pr_perf_add_device (
	PR_CONTEXT              *processor);

acpi_status
pr_perf_remove_device (
	PR_CONTEXT              *processor);

/* Processor Driver OSL */

acpi_status
pr_osl_add_device (
	PR_CONTEXT		*processor);

acpi_status
pr_osl_remove_device (
	PR_CONTEXT		*processor);

acpi_status
pr_osl_generate_event (
	u32			event,
	PR_CONTEXT		*processor);


#endif  /* __PR_H__ */
