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

struct acpi_enter_sx_ctx
{
	wait_queue_head_t wait;
	unsigned int      state;
};

volatile acpi_sstate_t acpi_sleep_state = ACPI_STATE_S0;

/*
 * Enter system sleep state
 */
static void
acpi_enter_sx_async(void *context)
{
	struct acpi_enter_sx_ctx *ctx = (struct acpi_enter_sx_ctx*) context;
	ACPI_OBJECT_LIST arg_list;
	ACPI_OBJECT arg;

	acpi_enter_sleep_state(ctx->state);

	/* either we are in S1, or the transition failed, as the other states resume
           from the waking vector */
	if (ctx->state != ACPI_STATE_S1) {
		printk(KERN_ERR "Could not enter S%d\n", ctx->state);
		goto out;
	}

	/* wait until S1 is entered */
	while (!(acpi_hw_register_bit_access(ACPI_READ, ACPI_MTX_LOCK, WAK_STS)))
		safe_halt();

	/* run the _WAK method */
	memset(&arg_list, 0, sizeof(arg_list));
	arg_list.count = 1;
	arg_list.pointer = &arg;

	memset(&arg, 0, sizeof(arg));
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = ctx->state;

	acpi_evaluate_object(NULL, "\\_WAK", &arg_list, NULL);

 out:
	acpi_sleep_state = ACPI_STATE_S0;

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
	
	init_waitqueue_head(&ctx.wait);
	ctx.state = ACPI_STATE_S5;
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

	for (sx = ACPI_STATE_S0; sx <= ACPI_STATE_S5; sx++) {
		if (ACPI_SUCCESS(
			   acpi_hw_obtain_sleep_type_register_data(sx,
								   &type_a,
								   &type_b))) {

			printk(" S%d", sx);
		}
	}
	printk("\n");
	
	pm_power_off = acpi_power_off;

	return 0;
}
