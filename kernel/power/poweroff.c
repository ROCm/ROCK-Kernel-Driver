/*
 * poweroff.c - sysrq handler to gracefully power down machine.
 *
 * This file is released under the GPL v2
 */

#include <linux/kernel.h>
#include <linux/sysrq.h>
#include <linux/init.h>
#include <linux/pm.h>


/**
 * handle_poweroff	-	sysrq callback for power down
 * @key: key pressed (unused)
 * @pt_regs: register state (unused)
 * @kbd: keyboard state (unused)
 * @tty: tty involved (unused)
 *
 * When the user hits Sys-Rq o to power down the machine this is the
 * callback we use.
 */

static void handle_poweroff (int key, struct pt_regs *pt_regs,
			     struct tty_struct *tty)
{
	if (pm_power_off)
		pm_power_off();
}

static struct sysrq_key_op	sysrq_poweroff_op = {
	.handler        = handle_poweroff,
	.help_msg       = "powerOff",
	.action_msg     = "Power Off\n"
};


static int pm_sysrq_init(void)
{
	register_sysrq_key('o', &sysrq_poweroff_op);
	return 0;
}

subsys_initcall(pm_sysrq_init);
