/******************************************************************************
 *
 * Name:	skhwt.h
 * Project:	Genesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.4 $
 * Date:	$Date: 1998/08/19 09:50:58 $
 * Purpose:	Defines for the hardware timer functions
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1989-1998 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *	All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SYSKONNECT
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	This Module contains Proprietary Information of SysKonnect
 *	and should be treated as Confidential.
 *
 *	The information in this file is provided for the exclusive use of
 *	the licensees of SysKonnect.
 *	Such users have the right to use, modify, and incorporate this code
 *	into products for purposes authorized by the license agreement
 *	provided they include this notice and the associated copyright notice
 *	with any such product.
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * History:
 *
 *	$Log: skgehwt.h,v $
 *	Revision 1.4  1998/08/19 09:50:58  gklug
 *	fix: remove struct keyword from c-code (see CCC) add typedefs
 *	
 *	Revision 1.3  1998/08/14 07:09:29  gklug
 *	fix: chg pAc -> pAC
 *	
 *	Revision 1.2  1998/08/07 12:54:21  gklug
 *	fix: first compiled version
 *	
 *	Revision 1.1  1998/08/07 09:32:58  gklug
 *	first version
 *	
 *	
 *	
 *	
 *
 ******************************************************************************/

/*
 * SKGEHWT.H	contains all defines and types for the timer functions
 */

#ifndef	_SKGEHWT_H_
#define _SKGEHWT_H_

/*
 * SK Hardware Timer
 * - needed wherever the HWT module is used
 * - use in Adapters context name pAC->Hwt
 */
typedef	struct s_Hwt {
	SK_U32		TStart ;	/* HWT start */
	SK_U32		TStop ;		/* HWT stop */
	int		TActive ;	/* HWT: flag : active/inactive */
} SK_HWT;

extern void SkHwtInit(SK_AC *pAC, SK_IOC Ioc);
extern void SkHwtStart(SK_AC *pAC, SK_IOC Ioc, SK_U32 Time);
extern void SkHwtStop(SK_AC *pAC, SK_IOC Ioc);
extern SK_U32 SkHwtRead(SK_AC *pAC,SK_IOC Ioc);
extern void SkHwtIsr(SK_AC *pAC, SK_IOC Ioc);
#endif	/* _SKGEHWT_H_ */
