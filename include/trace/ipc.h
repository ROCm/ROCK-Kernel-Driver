#ifndef _TRACE_IPC_H
#define _TRACE_IPC_H

#include <linux/tracepoint.h>

DEFINE_TRACE(ipc_msg_create,
	TPPROTO(long id, int flags),
	TPARGS(id, flags));
DEFINE_TRACE(ipc_sem_create,
	TPPROTO(long id, int flags),
	TPARGS(id, flags));
DEFINE_TRACE(ipc_shm_create,
	TPPROTO(long id, int flags),
	TPARGS(id, flags));
#endif
