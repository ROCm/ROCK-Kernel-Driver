/*
 *      This file is proprietary to Silicon Graphics, Inc. ("SGI"). It is
 *      specifically "closed source" code, confidential, and held as an SGI
 *      trade secret.  See the file "SGI_NOTICE" in the main directory of this
 *      archive for more details.
 *
 *      This work is held in copyright as an unpublished work by
 *      Silicon Graphic, Inc.  All rights reserved.
 */


/*
 * Cross Partition Communication (XPC) stubs specific to the standard version.
 */

#ifndef _IA64_SN_XPC_STUBS_H
#define _IA64_SN_XPC_STUBS_H


#define XPC_KDB_REGISTER() \
		xpc_kdb_register()
#define XPC_KDB_UNREGISTER() \
		xpc_kdb_unregister()

#define XPC_CONNECTED_CALLOUT(_ch) \
		xpc_create_kthreads(_ch, 1)
#define XPC_DISCONNECTED_CALLOUT(_ch, _ch_flags, _irq_flags)

#define XPC_INITIATE_MSG_DELIVERY(_ch, _nmsgs) \
		xpc_activate_kthreads(_ch, _nmsgs)

#define XPC_PROCESS_CHANNEL_ACTIVITY(_p) \
		xpc_wakeup_channel_mgr(_p)

#define XPC_PROCESS_PARTITION_DOWN(_p) \
		xpc_wakeup_channel_mgr(_p)

#define XPC_DISCONNECT_WAIT(_ch_number) \
		xpc_disconnect_wait(_ch_number)

#define XPC_INIT_TIMER(_timer, _function, _data, _expires) \
	{ \
		init_timer(_timer); \
		(_timer)->function = (void (*)(unsigned long)) _function; \
		(_timer)->data = (unsigned long) _data; \
		(_timer)->expires = (unsigned long) _expires; \
		add_timer(_timer); \
	}
#define XPC_DEL_TIMER(_timer) \
		del_timer_sync(_timer)


#define XPC_REQUEST_IRQ(_irq, _handler, _flags, _dev_name, _dev_id) \
		request_irq(_irq, _handler, _flags, _dev_name, _dev_id)
#define XPC_FREE_IRQ(_irq, _dev_id) \
		free_irq(_irq, _dev_id)


/* clock ticks */
#define XPC_TICKS		jiffies
#define XPC_TICKS_PER_SEC	HZ



#endif /* _IA64_SN_XPC_STUBS_H */

