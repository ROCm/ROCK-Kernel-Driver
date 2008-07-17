#ifndef __XEN_HYPERCALL_H__
#define __XEN_HYPERCALL_H__

#include <asm/hypercall.h>

static inline int __must_check
HYPERVISOR_multicall_check(
	multicall_entry_t *call_list, unsigned int nr_calls,
	const unsigned long *rc_list)
{
	int rc = HYPERVISOR_multicall(call_list, nr_calls);

	if (unlikely(rc < 0))
		return rc;
	BUG_ON(rc);
	BUG_ON((int)nr_calls < 0);

	for ( ; nr_calls > 0; --nr_calls, ++call_list)
		if (unlikely(call_list->result != (rc_list ? *rc_list++ : 0)))
			return nr_calls;

	return 0;
}

/* A construct to ignore the return value of hypercall wrappers in a few
 * exceptional cases (simply casting the function result to void doesn't
 * avoid the compiler warning): */
#define VOID(expr) ((void)((expr)?:0))

#endif /* __XEN_HYPERCALL_H__ */
