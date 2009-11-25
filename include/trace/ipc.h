#ifndef _TRACE_IPC_H
#define _TRACE_IPC_H

#include <linux/tracepoint.h>

DECLARE_TRACE(ipc_msg_create,
	TP_PROTO(long id, int flags),
	TP_ARGS(id, flags));
DECLARE_TRACE(ipc_sem_create,
	TP_PROTO(long id, int flags),
	TP_ARGS(id, flags));
DECLARE_TRACE(ipc_shm_create,
	TP_PROTO(long id, int flags),
	TP_ARGS(id, flags));
DECLARE_TRACE(ipc_call,
	TP_PROTO(unsigned int call, unsigned int first),
	TP_ARGS(call, first));
#endif
