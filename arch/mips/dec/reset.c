/*
 *  $Id: $
 *
 *  Reset a DECstation machine.
 *
 */

void (*back_to_prom)(void) = (void (*)(void))0xBFC00000;

void dec_machine_restart(char *command)
{
	back_to_prom();
}

void dec_machine_halt(void)
{
	back_to_prom();
}

void dec_machine_power_off(void)
{
    /* DECstations don't have a software power switch */
	back_to_prom();
}

