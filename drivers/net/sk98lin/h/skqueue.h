/******************************************************************************
 *
 * Name:	skqueue.h
 * Project:	Genesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.12 $
 * Date:	$Date: 1998/09/08 08:48:01 $
 * Purpose:	Defines for the Event queue
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
 *	$Log: skqueue.h,v $
 *	Revision 1.12  1998/09/08 08:48:01  gklug
 *	add: init level handling
 *	
 *	Revision 1.11  1998/09/03 14:15:11  gklug
 *	add: CSUM and HWAC Eventclass and function.
 *	fix: pParaPtr according to CCC
 *	
 *	Revision 1.10  1998/08/20 12:43:03  gklug
 *	add: typedef SK_QUEUE
 *	
 *	Revision 1.9  1998/08/19 09:50:59  gklug
 *	fix: remove struct keyword from c-code (see CCC) add typedefs
 *	
 *	Revision 1.8  1998/08/18 07:00:01  gklug
 *	fix: SK_PTR not defined use void * instead.
 *	
 *	Revision 1.7  1998/08/17 13:43:19  gklug
 *	chg: Parameter will be union of 64bit para, 2 times SK_U32 or SK_PTR
 *	
 *	Revision 1.6  1998/08/14 07:09:30  gklug
 *	fix: chg pAc -> pAC
 *	
 *	Revision 1.5  1998/08/11 14:26:44  gklug
 *	chg: Event Dispatcher returns now int.
 *	
 *	Revision 1.4  1998/08/11 12:15:21  gklug
 *	add: Error numbers of skqueue module
 *	
 *	Revision 1.3  1998/08/07 12:54:23  gklug
 *	fix: first compiled version
 *	
 *	Revision 1.2  1998/08/07 09:34:00  gklug
 *	adapt structure defs to CCC
 *	add: prototypes for functions
 *	
 *	Revision 1.1  1998/07/30 14:52:12  gklug
 *	Initial version.
 *	Defines Event Classes, Event structs and queue management variables.
 *	
 *	
 *
 ******************************************************************************/

/*
 * SKQUEUE.H	contains all defines and types for the event queue
 */

#ifndef _SKQUEUE_H_
#define _SKQUEUE_H_


/*
 * define the event classes to be served
 */
#define	SKGE_DRV	1	/* Driver Event Class */
#define	SKGE_RLMT	2	/* RLMT Event Class */
#define	SKGE_I2C	3	/* i2C Event Class */
#define	SKGE_PNMI	4	/* PNMI Event Class */
#define	SKGE_CSUM	5	/* Checksum Event Class */
#define	SKGE_HWAC	6	/* Hardware Access Event Class */

/*
 * define event queue as circular buffer
 */
#define SK_MAX_EVENT	64

/*
 * Parameter union for the Para stuff
 */
typedef	union u_EvPara {
	void	*pParaPtr;	/* Parameter Pointer */
	SK_U64	Para64;		/* Parameter 64bit version */
	SK_U32	Para32[2];	/* Parameter Array of 32bit parameters */
} SK_EVPARA;

/*
 * Event Queue
 *	skqueue.c
 * events are class/value pairs
 *	class	is addressee, e.g. RMT, PCM etc.
 *	value	is command, e.g. line state change, ring op change etc.
 */
typedef	struct s_EventElem {
	SK_U32		Class ;			/* Event class */
	SK_U32		Event ;			/* Event value */
	SK_EVPARA	Para ;			/* Event parameter */
} SK_EVENTELEM;

typedef	struct s_Queue {
	SK_EVENTELEM	EvQueue[SK_MAX_EVENT];
	SK_EVENTELEM	*EvPut ;
	SK_EVENTELEM	*EvGet ;
} SK_QUEUE;

extern	void SkEventInit(SK_AC *pAC, SK_IOC Ioc, int Level);
extern	void SkEventQueue(SK_AC *pAC, SK_U32 Class, SK_U32 Event,
	SK_EVPARA Para);
extern	int SkEventDispatcher(SK_AC *pAC,SK_IOC Ioc);


/* Define Error Numbers and messages */
#define	SKERR_Q_E001	(SK_ERRBASE_QUEUE+0)
#define	SKERR_Q_E001MSG	"Event queue overflow"
#define	SKERR_Q_E002	(SKERR_Q_E001+1)
#define	SKERR_Q_E002MSG	"Undefined event class"
#endif	/* _SKQUEUE_H_ */

