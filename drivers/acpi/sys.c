/*
 *  sys.c - System management (suspend, ...)
 *
 *  Copyright (C) 2000 Andrew Henroid
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

#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include "acpi.h"
#include "driver.h"

#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("sys")

#define ACPI_SLP_TYP(typa, typb)	(((int)(typa) << 8) | (int)(typb))
#define ACPI_SLP_TYPA(value)		((value) >> 8)
#define ACPI_SLP_TYPB(value)		((value) & 0xff)

struct acpi_enter_sx_ctx
{
	wait_queue_head_t wait;
	unsigned int      state;
};

volatile acpi_sstate_t acpi_sleep_state = ACPI_S0;
static unsigned long acpi_slptyp[ACPI_S5 + 1] = {ACPI_INVALID,};

/*
 * Enter system sleep state
 */
static void
acpi_enter_sx_async(void *context)
{
	struct acpi_enter_sx_ctx *ctx = (struct acpi_enter_sx_ctx*) context;
	ACPI_OBJECT_LIST arg_list;
	ACPI_OBJECT arg;

	/*
         * _PSW methods could be run here to enable wake-on keyboard, LAN, etc.
	 */

	// run the _PTS method
	memset(&arg_list, 0, sizeof(arg_list));
	arg_list.count = 1;
	arg_list.pointer = &arg;

	memset(&arg, 0, sizeof(arg));
	arg.type = ACPI_TYPE_NUMBER;
	arg.number.value = ctx->state;

	acpi_evaluate_object(NULL, "\\_PTS", &arg_list, NULL);
	
	// clear wake status by writing a 1
	acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_LOCK, WAK_STS, 1);
		
	acpi_sleep_state = ctx->state;

	// set ACPI_SLP_TYPA/b and ACPI_SLP_EN
	acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_LOCK, SLP_TYPE_A,
		ACPI_SLP_TYPA(acpi_slptyp[ctx->state]));
	acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_LOCK, SLP_TYPE_B,
		ACPI_SLP_TYPB(acpi_slptyp[ctx->state]));
	acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_LOCK, SLP_EN, 1);
	
	if (ctx->state != ACPI_S1) {
		/* we should have just shut off - what are we doing here? */
		printk(KERN_ERR "ACPI: S%d failed\n", ctx->state);
		goto out;
	}

	// wait until S1 is entered
	while (!(acpi_hw_register_bit_access(ACPI_READ, ACPI_MTX_LOCK, WAK_STS)))
		safe_halt();

	// run the _WAK method
	memset(&arg_list, 0, sizeof(arg_list));
	arg_list.count = 1;
	arg_list.pointer = &arg;

	memset(&arg, 0, sizeof(arg));
	arg.type = ACPI_TYPE_NUMBER;
	arg.number.value = ctx->state;

	acpi_evaluate_object(NULL, "\\_WAK", &arg_list, NULL);

 out:
	acpi_sleep_state = ACPI_S0;

	if (waitqueue_active(&ctx->wait))
		wake_up_interruptible(&ctx->wait);
}

/*
 * Enter soft-off (S5)
 */
static void
acpi_power_off(void)
{
	struct acpi_enter_sx_ctx ctx;

	if ((STRNCMP(acpi_fadt.header.signature, ACPI_FADT_SIGNATURE, ACPI_SIG_LEN) != 0)
	    || acpi_slptyp[ACPI_S5] == ACPI_INVALID)
		return;
	
	init_waitqueue_head(&ctx.wait);
	ctx.state = ACPI_S5;
	acpi_enter_sx_async(&ctx);
}

/*
 * Enter system sleep state and wait for completion
 */
int
acpi_enter_sx(acpi_sstate_t state)
{
	struct acpi_enter_sx_ctx ctx;
	DECLARE_WAITQUEUE(wait, current);
	int ret = 0;

	if ((STRNCMP(acpi_fadt.header.signature, ACPI_FADT_SIGNATURE, ACPI_SIG_LEN) != 0)
	    || acpi_slptyp[state] == ACPI_INVALID)
		return -EINVAL;
	
	init_waitqueue_head(&ctx.wait);
	ctx.state = state;

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&ctx.wait, &wait);

	if (acpi_os_queue_for_execution(0, acpi_enter_sx_async, &ctx))
		ret = -1;

	if (!ret)
		schedule();

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ctx.wait, &wait);

	if (!ret && signal_pending(current))
		ret = -ERESTARTSYS;

	return ret;
}

int
acpi_sys_init(void)
{
	u8 sx;
	u8 type_a;
	u8 type_b;

	printk(KERN_INFO "ACPI: System firmware supports:");

	for (sx = ACPI_S0; sx <= ACPI_S5; sx++) {
		int ca_sx = (sx <= ACPI_S4) ? sx : (sx + 1);
		if (ACPI_SUCCESS(
			   acpi_hw_obtain_sleep_type_register_data(ca_sx,
								   &type_a,
								   &type_b))) {

			acpi_slptyp[sx] = ACPI_SLP_TYP(type_a, type_b);
			printk(" S%d", sx);
		}
		else {
			acpi_slptyp[sx] = ACPI_INVALID;
		}
	}
	printk("\n");
	
	pm_power_off = acpi_power_off;

	return 0;
}
