#ifndef _ASM_SN_SN1_LED_H
#define _ASM_SN_SN1_LED_H

/*
 * Copyright (C) 2000 Silicon Graphics, Inc
 * Copyright (C) 2000 Jack Steiner (steiner@sgi.com)
 */

#include <asm/smp.h>

#define LED0		0xc0000b00100000c0LL	/* ZZZ fixme */



#define LED_AP_START	0x01		/* AP processor started */
#define LED_AP_IDLE	0x01

/*
 * Basic macros for flashing the LEDS on an SGI, SN1.
 */

extern __inline__ void
HUB_SET_LED(int val)
{
	long	*ledp;
	int	eid;

	eid = hard_processor_sapicid() & 3;
	ledp = (long*) (LED0 + (eid<<3));
	*ledp = val;
}


#endif /* _ASM_SN_SN1_LED_H */

