/*
 *  arch/s390/kernel/irqextras390.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *
 *  Some channel code by D.J. Barrow
 */

/*
	
*/
#include<asm/irqextras390.h>
#include<asm/lowcore.h>

#if 0
// fixchannelprogram is now obselete
void fixchannelprogram(orb_bits_t *orbptr)
{
	__u32	newAddress=orbptr->ccw_program_address;
	fixccws(orbptr->ccw_program_address);
	orbptr->ccw_program_address=newAddress;
	orbptr->ccw_program_address=(ccw1_t *)(((__u32)orbptr->ccw_program_address));
}
#endif

void fixccws(ccw1_bits_t *ccwptr)
{
	for(;;ccwptr++)
	{	// Just hope nobody starts doing prefixing
		if(!ccwptr->cc)
			break;
	}
}
