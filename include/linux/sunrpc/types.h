/*
 * linux/include/linux/sunrpc/types.h
 *
 * Generic types and misc stuff for RPC.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_TYPES_H_
#define _LINUX_SUNRPC_TYPES_H_

#include <linux/timer.h>
#include <linux/tqueue.h>
#include <linux/sunrpc/debug.h>

/*
 * These are the RPC list manipulation primitives used everywhere.
 */
struct rpc_listitem	{
	struct rpc_listitem *	prev;
	struct rpc_listitem *	next;
};

extern __inline__ void
__rpc_append_list(struct rpc_listitem **q, struct rpc_listitem *item)
{
	struct rpc_listitem	*next, *prev;

	if (!(next = *q)) {
		*q = item->next = item->prev = item;
	} else {
		prev = next->prev;
		prev->next = item;
		next->prev = item;
		item->next = next;
		item->prev = prev;
	}
}

extern __inline__ void
__rpc_insert_list(struct rpc_listitem **q, struct rpc_listitem *item)
{
	__rpc_append_list(q, item);
	*q = item;
}

extern __inline__ void
__rpc_remove_list(struct rpc_listitem **q, struct rpc_listitem *item)
{
	struct rpc_listitem	*prev = item->prev,
				*next = item->next;

	if (item != prev) {
		next->prev = prev;
		prev->next = next;
	} else {
		next = NULL;
	}
	if (*q == item)
		*q = next;
}

#define rpc_insert_list(q, i) \
      __rpc_insert_list((struct rpc_listitem **) q, (struct rpc_listitem *) i)
#define rpc_append_list(q, i) \
      __rpc_append_list((struct rpc_listitem **) q, (struct rpc_listitem *) i)
#define rpc_remove_list(q, i) \
      __rpc_remove_list((struct rpc_listitem **) q, (struct rpc_listitem *) i)

/*
 * Shorthands
 */
#define signalled()		(signal_pending(current))

#endif /* _LINUX_SUNRPC_TYPES_H_ */
