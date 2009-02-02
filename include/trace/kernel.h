#ifndef _TRACE_KERNEL_H
#define _TRACE_KERNEL_H

#include <linux/tracepoint.h>

DECLARE_TRACE(kernel_module_free,
	TPPROTO(struct module *mod),
	TPARGS(mod));
DECLARE_TRACE(kernel_module_load,
	TPPROTO(struct module *mod),
	TPARGS(mod));

#endif
