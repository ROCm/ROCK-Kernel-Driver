/*
 * linux/include/asm-arm/arch-brutus/uncompress.h
 *
 * (C) 1999 Nicolas Pitre <nico@cam.org>
 *
 * Reorganised to use machine_is_*() macros.
 */

#include "hardware.h"
#include "serial_reg.h"

#include <asm/mach-types.h>

/* Assabet's Status Control "Register" */
unsigned long SCR_value;

/* sa1100_setup() will perform any special initialization for UART, etc. */
extern void sa1100_setup( int arch_id );
#define arch_decomp_setup()	sa1100_setup(arch_id)

/*
 * The following code assumes the serial port has already been
 * initialized by the bootloader or such...
 */
static void puts( const char *s )
{
	volatile unsigned long *serial_port;

	if (machine_is_assabet()) {
		if( machine_has_neponset() )
			serial_port = (unsigned long *)_Ser3UTCR0;
		else
			serial_port = (unsigned long *)_Ser1UTCR0;
	} else if (machine_is_brutus()||machine_is_nanoengine())
		serial_port = (unsigned long *)_Ser1UTCR0;
	else if (machine_is_empeg() || machine_is_bitsy() ||
		 machine_is_victor() || machine_is_lart())
		serial_port = (unsigned long *)_Ser3UTCR0;
	else
		return;

	for (; *s; s++) {
		/* wait for space in the UART's transmiter */
		while (!(serial_port[UTSR1] & UTSR1_TNF));

		/* send the character out. */
		serial_port[UART_TX] = *s;

		/* if a LF, also do CR... */
		if (*s == 10) {
			while (!(serial_port[UTSR1] & UTSR1_TNF));
			serial_port[UART_TX] = 13;
		}
	}
}

/*
 * Nothing to do for these
 */
#define arch_decomp_wdog()
