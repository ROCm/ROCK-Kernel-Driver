/*
 * linux/include/asm-arm/arch-brutus/uncompress.h
 *
 * (C) 1999 Nicolas Pitre <nico@cam.org>
 *
 * Reorganised to use machine_is_*() macros.
 */

#include "hardware.h"

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

#define UART(x)		(*(volatile unsigned long *)(serial_port + (x)))

static void puts( const char *s )
{
	unsigned long serial_port;

	if (machine_is_assabet()) {
		if( machine_has_neponset() )
			serial_port = _Ser3UTCR0;
		else
			serial_port = _Ser1UTCR0;
	} else if (machine_is_brutus()||machine_is_nanoengine() ||
		   machine_is_pangolin() || machine_is_freebird() ||
		   machine_is_pfs168() || machine_is_flexanet())
		serial_port = _Ser1UTCR0;
	else if (machine_is_empeg() || machine_is_bitsy() ||
		 machine_is_victor() || machine_is_lart() ||
		 machine_is_sherman() || machine_is_yopy() ||
		 machine_is_huw_webpanel() || machine_is_itsy() )
		serial_port = _Ser3UTCR0;
	else
		return;

	for (; *s; s++) {
		/* wait for space in the UART's transmiter */
		while (!(UART(UTSR1) & UTSR1_TNF));

		/* send the character out. */
		UART(UTDR) = *s;

		/* if a LF, also do CR... */
		if (*s == 10) {
			while (!(UART(UTSR1) & UTSR1_TNF));
			UART(UTDR) = 13;
		}
	}
}

/*
 * Nothing to do for these
 */
#define arch_decomp_wdog()
