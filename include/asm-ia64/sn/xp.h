/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 Silicon Graphics, Inc. All rights reserved.
 */


/*
 * External Cross Partition (XP) structures and defines.
 */


#ifndef _ASM_IA64_SN_XP_H
#define _ASM_IA64_SN_XP_H


#ifndef	SN_PROM
#include <linux/version.h>
#include <linux/cache.h>
#include <asm/sn/types.h>
#include <asm/sn/bte.h>
#include <asm/hardirq.h>
#else /* ! SN_PROM */
#include "xpc_types.h"
#endif /* ! SN_PROM */


#ifdef CONFIG_KDB
#include <linux/kdb.h>
#include <linux/kdbprivate.h>

#define XP_ASSERT_ALWAYS(_expr)						\
	if (!(_expr)) {							\
		printk(KERN_EMERG "Assertion [ %s ] failed!\n"		\
			"Assertion is in %s() [%s, line %d]!\n",	\
			#_expr, __FUNCTION__, __FILE__, __LINE__);	\
		KDB_ENTER();						\
	}
#else /* CONFIG_KDB */
#ifndef	SN_PROM
#define XP_ASSERT_ALWAYS(_expr)						\
	if (!(_expr)) {							\
		printk(KERN_EMERG "Assertion [ %s ] failed!\n"		\
			"Assertion is in %s() [%s, line %d]!\n",	\
			#_expr, __FUNCTION__, __FILE__, __LINE__);	\
		BUG();							\
	}
#else /* ! SN_PROM */
#define XP_ASSERT_ALWAYS(_expr)						\
	if (!(_expr)) {							\
		printf("XP_ASSERT %s failed at %s line %d\n",		\
			#_expr, __FILE__, __LINE__);			\
	}
#endif /* ! SN_PROM */
#endif /* CONFIG_KDB */


#ifdef XP_ASSERT_ON
#define XP_ASSERT(_expr)	XP_ASSERT_ALWAYS(_expr)
#else
#define XP_ASSERT(_expr)
#endif


/*
 * Define the number of u64s required to represent all the C-brick nasids
 * as a bitmap.  The cross-partition kernel modules deal only with
 * C-brick nasids, thus the need for bitmaps which don't account for
 * odd-numbered (non C-brick) nasids.
 */
#define XP_MAX_NASIDS		(MAX_NASIDS / 2)
#define XP_NUM_NASID_WORDS	((XP_MAX_NASIDS + 63)/ 64)


/*
 * Wrapper for bte_copy() that should it return a failure status will retry
 * the bte_copy() once in the hope that the failure was due to a temporary
 * aberration (i.e., the link going down temporarily).
 *
 * See bte_copy for definition of the input parameters.
 */
static __inline__ bte_result_t
xp_bte_copy(u64 src, u64 dest, u64 len, u64 mode, void *notification)
{
	bte_result_t ret;


	ret = bte_copy(src, dest, len, mode, notification);

	if (ret != BTE_SUCCESS) {
		if (!in_interrupt()) {
			cond_resched();
		}
		ret = bte_copy(src, dest, len, mode, notification);
	}

	return ret;
}


/*
 * XPC establishes channel connections between the local partition and any
 * other partition that is currently up. Over these channels, kernel-level
 * `users' can communicate with their counterparts on the other partitions.
 *
 * The maxinum number of channels is limited to eight. For performance reasons,
 * the internal cross partition structures require sixteen bytes per channel,
 * and eight allows all of this interface-shared info to fit in one cache line.
 *
 * XPC_NCHANNELS reflects the total number of channels currently defined.
 * If the need for additional channels arises, one can simply increase
 * XPC_NCHANNELS accordingly. If the day should come where that number
 * exceeds the MAXIMUM number of channels allowed (eight), then one will need
 * to make changes to the XPC code to allow for this.
 *
 * 
 *    CHANNEL | USER          | PURPOSE                                  
 *   ---------+---------------+---------------------------------------------
 *    0       | XPMEM         | cross partition memory driver
 *   ---------+---------------+---------------------------------------------
 *    1       | XPNET         | cross partition network driver
 *   ---------+---------------+---------------------------------------------
 *    2       | XPCT          | XPC test channel
 *   ---------+---------------+---------------------------------------------
 *    3-7     | unused        | reserved for future use
 *   ---------+---------------+---------------------------------------------
 *
 */
#define XPC_MEM_CHANNEL		0	/* memory channel number */
#define	XPC_NET_CHANNEL		1	/* network channel number */
#define	XPC_TEST_CHANNEL	2	/* test channel number */

#define	XPC_NCHANNELS		3	/* #of defined channels */
#define XPC_MAX_NCHANNELS	8	/* max #of channels allowed */

#if XPC_NCHANNELS > XPC_MAX_NCHANNELS
#error	XPC_NCHANNELS exceeds MAXIMUM allowed.
#endif


/*
 * The format of an XPC message is as follows:
 *
 *      +-------+--------------------------------+
 *      | flags |////////////////////////////////|
 *      +-------+--------------------------------+
 *      |             message #                  |
 *      +----------------------------------------+
 *      |     payload (user-defined message)     |
 *      |                                        |
 *         		:
 *      |                                        |
 *      +----------------------------------------+
 *
 * The size of the payload is defined by the user via xpc_connect(). A user-
 * defined message resides in the payload area.
 *
 * The user should have no dealings with the message header, but only the
 * message's payload. When a message entry is allocated (via xpc_allocate())
 * a pointer to the payload area is returned and not the actual beginning of
 * the XPC message. The user then constructs a message in the payload area
 * and passes that pointer as an argument on xpc_send() or xpc_send_notify().
 *
 * The size of a message entry (within a message queue) must be a cacheline
 * sized multiple in order to facilitate the BTE transfer of messages from one
 * message queue to another. A macro, XPC_MSG_SIZE(), is provided for the user
 * that wants to fit as many msg entries as possible in a given memory size
 * (e.g. a memory page).
 */
typedef struct {
	volatile u8 flags;	/* FOR XPC INTERNAL USE ONLY */
	u8 reserved[7];		/* FOR XPC INTERNAL USE ONLY */
	s64 number;		/* FOR XPC INTERNAL USE ONLY */

	u64 payload;		/* user defined portion of message */
} xpc_msg_t;


#define XPC_MSG_PAYLOAD_OFFSET	(u64) (&((xpc_msg_t *)0)->payload)
#define XPC_MSG_SIZE(_payload_size)					\
	L1_CACHE_ALIGN(XPC_MSG_PAYLOAD_OFFSET + (_payload_size))


/*
 * Define the return values and values passed to user's callout functions. 
 * (It is important to add new value codes at the end just preceding
 * xpcUnknownReason, which must have the highest numerical value.)
 */
typedef enum {
	xpcSuccess = 0,

	xpcNotConnected,	/*  1: channel is not connected */
	xpcConnected,		/*  2: channel connected (opened) */
	xpcRETIRED1,		/*  3: (formerly xpcDisconnected) */

	xpcMsgReceived,		/*  4: message received */
	xpcMsgDelivered,	/*  5: message delivered and acknowledged */

	xpcRETIRED2,		/*  6: (formerly xpcTransferFailed) */

	xpcNoWait,		/*  7: operation would require wait */
	xpcRetry,		/*  8: retry operation */
	xpcTimeout,		/*  9: timeout in xpc_allocate_msg_wait() */
	xpcInterrupted,		/* 10: interrupted wait */

	xpcUnequalMsgSizes,	/* 11: message size disparity between sides */
	xpcInvalidAddress,	/* 12: invalid address */

	xpcNoMemory,		/* 13: no memory available for XPC structures */
	xpcLackOfResources,	/* 14: insufficient resources for operation */
	xpcUnregistered,	/* 15: channel is not registered */
	xpcAlreadyRegistered,	/* 16: channel is already registered */

	xpcPartitionDown,	/* 17: remote partition is down */
	xpcNotLoaded,		/* 18: XPC module is not loaded */
	xpcUnloading,		/* 19: this side is unloading XPC module */

	xpcBadMagic,		/* 20: XPC MAGIC string not found */

	xpcReactivating,	/* 21: remote partition was reactivated */

	xpcUnregistering,	/* 22: this side is unregistering channel */
	xpcOtherUnregistering,	/* 23: other side is unregistering channel */

	xpcCloneKThread,	/* 24: cloning kernel thread */
	xpcCloneKThreadFailed,	/* 25: cloning kernel thread failed */

	xpcNoHeartbeat,		/* 26: remote partition has no heartbeat */

	xpcPioReadError,	/* 27: PIO read error */
	xpcPhysAddrRegFailed,	/* 28: registration of phys addr range failed */

	xpcBteDirectoryError,	/* 29: maps to BTEFAIL_DIR */
	xpcBtePoisonError,	/* 30: maps to BTEFAIL_POISON */
	xpcBteWriteError,	/* 31: maps to BTEFAIL_WERR */
	xpcBteAccessError,	/* 32: maps to BTEFAIL_ACCESS */
	xpcBtePWriteError,	/* 33: maps to BTEFAIL_PWERR */
	xpcBtePReadError,	/* 34: maps to BTEFAIL_PRERR */
	xpcBteTimeOutError,	/* 35: maps to BTEFAIL_TOUT */
	xpcBteXtalkError,	/* 36: maps to BTEFAIL_XTERR */
	xpcBteNotAvailable,	/* 37: maps to BTEFAIL_NOTAVAIL */
	xpcBteUnmappedError,	/* 38: unmapped BTEFAIL_ error */

	xpcBadVersion,		/* 39: bad version number */
	xpcVarsNotSet,		/* 40: the XPC variables are not set up */
	xpcNoRsvdPageAddr,	/* 41: unable to get rsvd page's phys addr */
	xpcInvalidPartid,	/* 42: invalid partition ID */
	xpcLocalPartid,		/* 43: local partition ID */

	xpcUnknownReason	/* 44: unknown reason -- must be last in list */
} xpc_t;


//>>> is inline a good idea with all the strings?
static __inline__ char *
xpc_get_ascii_reason_code(xpc_t reason)
{
	switch (reason) {
	case xpcSuccess:		return "";
	case xpcNotConnected:		return "xpcNotConnected";
	case xpcConnected:		return "xpcConnected";
	case xpcRETIRED1:		return "xpcRETIRED1";
	case xpcMsgReceived:		return "xpcMsgReceived";
	case xpcMsgDelivered:		return "xpcMsgDelivered";
	case xpcRETIRED2:		return "xpcRETIRED2";
	case xpcNoWait:			return "xpcNoWait";
	case xpcRetry:			return "xpcRetry";
	case xpcTimeout:		return "xpcTimeout";
	case xpcInterrupted:		return "xpcInterrupted";
	case xpcUnequalMsgSizes:	return "xpcUnequalMsgSizes";
	case xpcInvalidAddress:		return "xpcInvalidAddress";
	case xpcNoMemory:		return "xpcNoMemory";
	case xpcLackOfResources:	return "xpcLackOfResources";
	case xpcUnregistered:		return "xpcUnregistered";
	case xpcAlreadyRegistered:	return "xpcAlreadyRegistered";
	case xpcPartitionDown:		return "xpcPartitionDown";
	case xpcNotLoaded:		return "xpcNotLoaded";
	case xpcUnloading:		return "xpcUnloading";
	case xpcBadMagic:		return "xpcBadMagic";
	case xpcReactivating:		return "xpcReactivating";
	case xpcUnregistering:		return "xpcUnregistering";
	case xpcOtherUnregistering:	return "xpcOtherUnregistering";
	case xpcCloneKThread:		return "xpcCloneKThread";
	case xpcCloneKThreadFailed:	return "xpcCloneKThreadFailed";
	case xpcNoHeartbeat:		return "xpcNoHeartbeat";
	case xpcPioReadError:		return "xpcPioReadError";
	case xpcPhysAddrRegFailed:	return "xpcPhysAddrRegFailed";
	case xpcBteDirectoryError:	return "xpcBteDirectoryError";
	case xpcBtePoisonError:		return "xpcBtePoisonError";
	case xpcBteWriteError:		return "xpcBteWriteError";
	case xpcBteAccessError:		return "xpcBteAccessError";
	case xpcBtePWriteError:		return "xpcBtePWriteError";
	case xpcBtePReadError:		return "xpcBtePReadError";
	case xpcBteTimeOutError:	return "xpcBteTimeOutError";
	case xpcBteXtalkError:		return "xpcBteXtalkError";
	case xpcBteNotAvailable:	return "xpcBteNotAvailable";
	case xpcBteUnmappedError:	return "xpcBteUnmappedError";
	case xpcBadVersion:		return "xpcBadVersion";
	case xpcVarsNotSet:		return "xpcVarsNotSet";
	case xpcNoRsvdPageAddr:		return "xpcNoRsvdPageAddr";
	case xpcInvalidPartid:		return "xpcInvalidPartid";
	case xpcLocalPartid:		return "xpcLocalPartid";
	case xpcUnknownReason:		return "xpcUnknownReason";
	default:			return "undefined reason code";
	}
}


/*
 * Define the callout function types used by XPC to update the user on
 * connection activity and state changes (via the user function registered by
 * xpc_connect()) and to notify them of messages received and delivered (via
 * the user function registered by xpc_send_notify()).
 *
 * The two function types are xpc_channel_func_t and xpc_notify_func_t and
 * both share the following arguments, with the exception of "data", which
 * only xpc_channel_func_t has.
 *
 * Arguments:
 *
 *	reason - reason code. (See following table.)
 *	partid - partition ID associated with condition.
 *	ch_number - channel # associated with condition.
 *	data - pointer to optional data. (See following table.)
 *	key - pointer to optional user-defined value provided as the "key"
 *	      argument to xpc_connect() or xpc_send_notify().
 *
 * In the following table the "Optional Data" column applies to callouts made
 * to functions registered by xpc_connect(). A "NA" in that column indicates
 * that this reason code can be passed to functions registered by
 * xpc_send_notify() (i.e. they don't have data arguments).
 *
 * Also, the first three reason codes in the following table indicate
 * success, whereas the others indicate failure. When a failure reason code
 * is received, one can assume that the channel is not connected.
 *
 *
 * Reason Code          | Cause                          | Optional Data
 * =====================+================================+=====================
 * xpcConnected         | connection has been established| max #of entries
 *                      | to the specified partition on  | allowed in message
 *                      | the specified channel          | queue 
 * ---------------------+--------------------------------+---------------------
 * xpcMsgReceived       | an XPC message arrived from    | address of payload
 *                      | the specified partition on the |
 *                      | specified channel              | [the user must call
 *                      |                                | xpc_received() when
 *                      |                                | finished with the
 *                      |                                | payload]
 * ---------------------+--------------------------------+---------------------
 * xpcMsgDelivered      | notification that the message  | NA
 *                      | was delivered to the intended  |
 *                      | recipient and that they have   |
 *                      | acknowledged its receipt by    |
 *                      | calling xpc_received()         |
 * =====================+================================+=====================
 * xpcUnequalMsgSizes   | can't connect to the specified | NULL
 *                      | partition on the specified     |
 *                      | channel because of mismatched  |
 *                      | message sizes                  |
 * ---------------------+--------------------------------+---------------------
 * xpcNoMemory          | insufficient memory avaiable   | NULL
 *                      | to allocate message queue      | 
 * ---------------------+--------------------------------+---------------------
 * xpcLackOfResources   | lack of resources to create    | NULL
 *                      | the necessary kthreads to      |
 *                      | support the channel            |
 * ---------------------+--------------------------------+---------------------
 * xpcUnregistering     | this side's user has           | NULL or NA
 *                      | unregistered by calling        | 
 *                      | xpc_disconnect()               |
 * ---------------------+--------------------------------+---------------------
 * xpcOtherUnregistering| the other side's user has      | NULL or NA
 *                      | unregistered by calling        |
 *                      | xpc_disconnect()               |
 * ---------------------+--------------------------------+---------------------
 * xpcNoHeartbeat       | the other side's XPC is no     | NULL or NA
 *                      | longer heartbeating            |
 *                      |                                |
 * ---------------------+--------------------------------+---------------------
 * xpcUnloading         | this side's XPC module is      | NULL or NA
 *                      | being unloaded                 |
 *                      |                                |
 * ---------------------+--------------------------------+---------------------
 * xpcOtherUnloading    | the other side's XPC module is | NULL or NA
 *                      | is being unloaded              |
 *                      |                                |
 * ---------------------+--------------------------------+---------------------
 * xpcPioReadError      | xp_nofault_PIOR() returned an  | NULL or NA
 *                      | error while sending an IPI     |
 *                      |                                |
 * ---------------------+--------------------------------+---------------------
 * xpcInvalidAddress    | the address either received or | NULL or NA
 *                      | sent by the specified partition|
 *                      | is invalid                     |
 * ---------------------+--------------------------------+---------------------
 * xpcBteNotAvailable   | attempt to pull data from the  | NULL or NA
 * xpcBtePoisonError    | specified partition over the   |
 * xpcBteWriteError     | specified channel via a        |
 * xpcBteAccessError    | bte_copy() failed              |
 * xpcBteTimeOutError   |                                |
 * xpcBteXtalkError     |                                |
 * xpcBteDirectoryError |                                |
 * xpcBteGenericError   |                                |
 * xpcBteUnmappedError  |                                |
 * ---------------------+--------------------------------+---------------------
 * xpcUnknownReason     | the specified channel to the   | NULL or NA
 *                      | specified partition was        |
 *                      | unavailable for unknown reasons|
 * =====================+================================+=====================
 */

typedef void (*xpc_channel_func_t)(xpc_t reason, partid_t partid, int ch_number,
		void *data, void *key);

typedef void (*xpc_notify_func_t)(xpc_t reason, partid_t partid, int ch_number,
		void *key);


/*
 * The following is a registration entry. There is a global array of these,
 * one per channel. It is used to record the connection registration made
 * by the users of XPC (XPMEM and XPNET). As long as a registration entry
 * exists, for any partition that comes up, XPC will attempt to establish a
 * connection on that channel. Notification that a connection has been made
 * will occur via the xpc_channel_func_t function.
 */
typedef struct {

	struct semaphore sema;

	/*
	 * Function to call when aynchronous notification is required for
	 * such events as, a connection established/lost, or an incomming
	 * message received, or an error condition encountered. A non-NULL
	 * func field indicates that there is an active registration for
	 * the channel.
	 */
	volatile xpc_channel_func_t func;
	void *key;			/* pointer to user's key */

	u16 nentries;			/* #of msg entries in local msg queue */
	u16 msg_size;			/* message queue's message size */
	u32 assigned_limit;		/* limit on #of assigned kthreads */
	u32 idle_limit;			/* limit on #of idle kthreads */
} ____cacheline_aligned xpc_registration_t;


#define XPC_CHANNEL_REGISTERED(_c)	(xpc_registrations[_c].func != NULL)


/* the following are valid xpc_allocate() flags */
#define XPC_WAIT	0		/* wait flag */
#define XPC_NOWAIT	1		/* no wait flag */


#ifndef __XPC_MAIN__


typedef struct {
	void (*connect)(int);
	void (*disconnect)(int);
	xpc_t (*allocate)(partid_t, int, u32, void **);
	xpc_t (*send)(partid_t, int, void *);
	xpc_t (*send_notify)(partid_t, int, void *, xpc_notify_func_t, void *);
	void (*received)(partid_t, int, void *);
	xpc_t (*partid_to_nasids)(partid_t, void *);
} xpc_interface_t;


extern xpc_interface_t xpc_interface;

#ifdef	SN_PROM
extern int xpc_init(void);
extern void xpc_exit(void);
#endif /* SN_PROM */

extern xpc_t xpc_connect(int, xpc_channel_func_t, void *, u16, u16, u32, u32);
extern void xpc_disconnect(int);


/*
 * extern xpc_t xpc_allocate(partid_t partid, int ch_number, u32 flags,
 *				void **payload);
 */
#define xpc_allocate(_partid, _ch_number, _flags, _payload)		\
		xpc_interface.allocate(_partid, _ch_number, _flags, _payload)


/*
 * extern xpc_t xpc_send(partid_t partid, int ch_number, void *payload);
 */
#define xpc_send(_partid, _ch_number, _payload)				\
		xpc_interface.send(_partid, _ch_number, _payload)


/*
 * extern xpc_t xpc_send_notify(partid_t partid, int ch_number, void *payload,
 *				xpc_notify_func_t func, void *key);
 */
#define xpc_send_notify(_partid, _ch_number, _payload, _func, _key)	\
		xpc_interface.send_notify(_partid, _ch_number, _payload,\
					_func, _key)


/*
 * extern void xpc_received(partid_t partid, int ch_number, void *payload);
 */
#define xpc_received(_partid, _ch_number, _payload)			\
		xpc_interface.received(_partid, _ch_number, _payload)


/*
 * extern xpc_t xpc_partid_to_nasids(partid_t partid, void * nasids);
 */
#define xpc_partid_to_nasids(_partid, _nasids)				\
		xpc_interface.partid_to_nasids(_partid, _nasids)


#else /* __XPC_MAIN__ */

extern void xpc_set_interface(void (*)(int),
		void (*)(int),
		xpc_t (*)(partid_t, int, u32, void **),
		xpc_t (*)(partid_t, int, void *),
		xpc_t (*)(partid_t, int, void *, xpc_notify_func_t, void *),
		void (*)(partid_t, int, void *),
		xpc_t (*)(partid_t, void *));
extern void xpc_clear_interface(void);

extern void xpc_initiate_connect(int);
extern void xpc_initiate_disconnect(int);
extern xpc_t xpc_allocate(partid_t, int, u32, void **);
extern xpc_t xpc_send(partid_t, int, void *);
extern xpc_t xpc_send_notify(partid_t, int, void *, xpc_notify_func_t, void *);
extern void xpc_received(partid_t, int, void *);
extern xpc_t xpc_partid_to_nasids(partid_t, void *);

#endif /* __XPC_MAIN__ */

#ifdef	SN_PROM
extern void xpc_poll(void);
#endif /* SN_PROM */

extern int xp_nofault_PIOR(void *);
extern int xp_error_PIOR(void);


#endif /* _ASM_IA64_SN_XP_H */

