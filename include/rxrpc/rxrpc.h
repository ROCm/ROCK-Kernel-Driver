/* rx.h: Rx RPC interface
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RXRPC_RXRPC_H
#define _LINUX_RXRPC_RXRPC_H

#ifdef __KERNEL__

extern u32 rxrpc_epoch;

extern int rxrpc_ktrace;
extern int rxrpc_kdebug;
extern int rxrpc_kproto;
extern int rxrpc_knet;

extern int rxrpc_sysctl_init(void);
extern void rxrpc_sysctl_cleanup(void);

#endif /* __KERNEL__ */

#endif /* _LINUX_RXRPC_RXRPC_H */
