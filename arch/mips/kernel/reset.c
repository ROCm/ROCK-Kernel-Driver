/*
 *  linux/arch/mips/sni/process.c
 *
 *  Reset the machine.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/reboot.h>
#include <asm/reboot.h>

/*
 * Urgs ...  Too many MIPS machines to handle this in a generic way.
 * So handle all using function pointers to machine specific
 * functions.
 */

void machine_restart(char *command)
{
	_machine_restart(command);
}

void machine_halt(void)
{
	_machine_halt();
}

void machine_power_off(void)
{
	_machine_power_off();
}
