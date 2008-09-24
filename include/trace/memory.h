#ifndef _TRACE_MEMORY_H
#define _TRACE_MEMORY_H

#include <linux/tracepoint.h>

DEFINE_TRACE(memory_handle_fault_entry,
	TPPROTO(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, int write_access),
	TPARGS(mm, vma, address, write_access));
DEFINE_TRACE(memory_handle_fault_exit,
	TPPROTO(int res),
	TPARGS(res));

#endif
