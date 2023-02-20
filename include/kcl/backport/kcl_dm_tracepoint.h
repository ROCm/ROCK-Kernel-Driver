/* SPDX-License-Identifier: GPL-2.0 */
#ifndef KCL_BACKPORT_KCL_DM_TRACEPOINT_H
#define KCL_BACKPORT_KCL_DM_TRACEPOINT_H

#ifndef DECLARE_EVENT_NOP
#define DECLARE_EVENT_NOP(name, proto, args)				\
	static inline void trace_##name(proto)				\
	{ }								\
	static inline bool trace_##name##_enabled(void)			\
	{								\
		return false;						\
	}

#define TRACE_EVENT_NOP(name, proto, args, struct, assign, print)	\
	DECLARE_EVENT_NOP(name, PARAMS(proto), PARAMS(args))

#define DEFINE_EVENT_NOP(template, name, proto, args)			\
	DECLARE_EVENT_NOP(name, PARAMS(proto), PARAMS(args))
#endif

#endif /* KCL_BACKPORT_KCL_DM_TRACEPOINT_H */
