/******************************************************************************
 *
 * Name:	skgedrv.h
 * Project:	Gigabit Ethernet Adapters, Common Modules
 * Version:	$Revision: 1.10 $
 * Date:	$Date: 2003/07/04 12:25:01 $
 * Purpose:	Interface with the driver
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * History:
 *
 *	$Log: skgedrv.h,v $
 *	Revision 1.10  2003/07/04 12:25:01  rschmidt
 *	Added event SK_DRV_DOWNSHIFT_DET for Downshift 4-Pair / 2-Pair
 *	
 *	Revision 1.9  2003/05/13 17:24:21  mkarl
 *	Added events SK_DRV_LINK_UP and SK_DRV_LINK_DOWN for drivers not using
 *	RLMT (SK_NO_RLMT).
 *	Editorial changes.
 *	
 *	Revision 1.8  2003/03/31 07:18:54  mkarl
 *	Corrected Copyright.
 *	
 *	Revision 1.7  2003/03/18 09:43:47  rroesler
 *	Added new event for timer
 *	
 *	Revision 1.6  2002/07/15 15:38:01  rschmidt
 *	Power Management support
 *	Editorial changes
 *	
 *	Revision 1.5  2002/04/25 11:05:47  rschmidt
 *	Editorial changes
 *	
 *	Revision 1.4  1999/11/22 13:52:46  cgoos
 *	Changed license header to GPL.
 *	
 *	Revision 1.3  1998/12/01 13:31:39  cgoos
 *	SWITCH INTERN Event added.
 *	
 *	Revision 1.2  1998/11/25 08:28:38  gklug
 *	rmv: PORT SWITCH Event
 *	
 *	Revision 1.1  1998/09/29 06:14:07  gklug
 *	add: driver events (initial version)
 *	
 *
 ******************************************************************************/

#ifndef __INC_SKGEDRV_H_
#define __INC_SKGEDRV_H_

/* defines ********************************************************************/

/*
 * Define the driver events.
 * Usually the events are defined by the destination module.
 * In case of the driver we put the definition of the events here.
 */
#define SK_DRV_PORT_RESET		 1	/* The port needs to be reset */
#define SK_DRV_NET_UP   		 2	/* The net is operational */
#define SK_DRV_NET_DOWN			 3	/* The net is down */
#define SK_DRV_SWITCH_SOFT		 4	/* Ports switch with both links connected */
#define SK_DRV_SWITCH_HARD		 5	/* Port switch due to link failure */
#define SK_DRV_RLMT_SEND		 6	/* Send a RLMT packet */
#define SK_DRV_ADAP_FAIL		 7	/* The whole adapter fails */
#define SK_DRV_PORT_FAIL		 8	/* One port fails */
#define SK_DRV_SWITCH_INTERN	 9	/* Port switch by the driver itself */
#define SK_DRV_POWER_DOWN		10	/* Power down mode */
#define SK_DRV_TIMER			11	/* Timer for free use */
#ifdef SK_NO_RLMT
#define SK_DRV_LINK_UP  		12	/* Link Up event for driver */
#define SK_DRV_LINK_DOWN		13	/* Link Down event for driver */
#endif
#define SK_DRV_DOWNSHIFT_DET	14	/* Downshift 4-Pair / 2-Pair (YUKON only) */
#endif /* __INC_SKGEDRV_H_ */
