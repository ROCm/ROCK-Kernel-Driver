/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004 Silicon Graphics, Inc.  All Rights Reserved.
 */


/*
 * Cross Partition Communication (XPC)'s dbgtk related definitions.
 */


#ifndef _IA64_SN_KERNEL_XPC_DBGTK_H
#define _IA64_SN_KERNEL_XPC_DBGTK_H


//>>> #define DBGTK_USE_DPRINTK
#include <asm/sn/xp_dbgtk.h>


/*
 * Define the XPC debug sets and other items used with DPRINTK for the
 * partition related messages.
 */
#define XPC_DBG_P_CONSOLE	0x0000000000000001
#define XPC_DBG_P_ERROR		0x0000000000000002

#define XPC_DBG_P_INIT		0x0000000000000010
#define XPC_DBG_P_HEARTBEAT	0x0000000000000020
#define XPC_DBG_P_DISCOVERY	0x0000000000000040
#define XPC_DBG_P_ACT		0x0000000000000080

#define XPC_DBG_P_INITV		0x0000000000000100
#define XPC_DBG_P_HEARTBEATV	0x0000000000000200
#define XPC_DBG_P_DISCOVERYV	0x0000000000000400
#define XPC_DBG_P_ACTV		0x0000000000000800


#define XPC_DBG_P_SET_DESCRIPTION "\n" \
		"\t0x001 Console\n" \
		"\t0x002 Error\n" \
		"\t0x010 Initialization\n" \
		"\t0x020 Heartbeat related\n" \
		"\t0x040 Discovery related\n" \
		"\t0x080 Activation/Deact\n" \
		"\t0x100 Verbose Initialization\n" \
		"\t0x200 Verbose Heartbeat related\n" \
		"\t0x400 Verbose Discovery related\n" \
		"\t0x800 Verbose Activation/Deact\n"

#define XPC_DBG_P_DEFCAPTURE_SETS	(XPC_DBG_P_CONSOLE | \
					 XPC_DBG_P_ERROR | \
					 XPC_DBG_P_INIT | \
					 XPC_DBG_P_INITV | \
					 XPC_DBG_P_HEARTBEAT | \
					 XPC_DBG_P_DISCOVERY | \
					 XPC_DBG_P_DISCOVERYV | \
					 XPC_DBG_P_ACT | \
					 XPC_DBG_P_ACTV)

#define XPC_DBG_P_DEFCONSOLE_SETS	(XPC_DBG_P_CONSOLE | \
					 XPC_DBG_P_ERROR)

EXTERN_DPRINTK(xpc_part);



/*
 * Define the XPC debug sets and other items used with DPRINTK for the
 * channel related messages.
 */

#define XPC_DBG_C_CONSOLE	0x0000000000000001	/* console */
#define XPC_DBG_C_ERROR		0x0000000000000002	/* error */

#define XPC_DBG_C_INIT		0x0000000000000004	/* XPC module load */
#define XPC_DBG_C_EXIT		0x0000000000000008	/* XPC module unload */
#define XPC_DBG_C_SETUP		0x0000000000000010	/* partition setup */
#define XPC_DBG_C_TEARDOWN	0x0000000000000020	/* partition teardown */
#define XPC_DBG_C_CONNECT	0x0000000000000040	/* channel connect */
#define XPC_DBG_C_DISCONNECT	0x0000000000000080	/* channel disconnect */
#define XPC_DBG_C_SEND		0x0000000000000100	/* msg send */
#define XPC_DBG_C_RECEIVE	0x0000000000000200	/* msg receive */
#define XPC_DBG_C_IPI		0x0000000000000400	/* IPI handling */
#define XPC_DBG_C_KTHREAD	0x0000000000000800	/* kthread management */

#define XPC_DBG_C_GP		0x0000000000001000	/* Get/Put changes */


#define XPC_DBG_C_DEFCAPTURE_SETS (-1ul)	/* capture all sets */
#define XPC_DBG_C_DEFCONSOLE_SETS (XPC_DBG_C_CONSOLE | XPC_DBG_C_ERROR)
						/* sets sent to console */

#define XPC_DBG_C_SET_DESCRIPTION	"\n" \
		"\t0x0001 Console\n" \
		"\t0x0002 Error\n" \
		"\t0x0004 Init\n" \
		"\t0x0008 Exit\n" \
		"\t0x0010 Setup\n" \
		"\t0x0020 Teardown\n" \
		"\t0x0040 Connect\n" \
		"\t0x0080 Disconnect\n" \
		"\t0x0100 Send\n" \
		"\t0x0200 Receive\n" \
		"\t0x0400 IPI\n" \
		"\t0x0800 Kthread\n" \
		"\t0x1000 Get/Put\n"

EXTERN_DPRINTK(xpc_chan);


#endif /* _IA64_SN_KERNEL_XPC_DBGTK_H */

