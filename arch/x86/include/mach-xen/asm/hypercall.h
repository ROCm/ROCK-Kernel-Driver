/******************************************************************************
 * hypercall.h
 *
 * Linux-specific hypervisor handling.
 *
 * Copyright (c) 2002-2004, K A Fraser
 *
 * 64-bit updates:
 *   Benjamin Liu <benjamin.liu@intel.com>
 *   Jun Nakajima <jun.nakajima@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __HYPERCALL_H__
#define __HYPERCALL_H__

#ifndef __HYPERVISOR_H__
# error "please don't include this file directly"
#endif

#if CONFIG_XEN_COMPAT <= 0x030002
# include <linux/string.h> /* memcpy() */
#endif

#ifdef CONFIG_XEN
#define HYPERCALL_ASM_OPERAND "%c"
#define HYPERCALL_LOCATION(op) (hypercall_page + (op) * 32)
#define HYPERCALL_C_OPERAND(name) "i" (HYPERCALL_LOCATION(__HYPERVISOR_##name))
#else
#define HYPERCALL_ASM_OPERAND "*%"
#define HYPERCALL_LOCATION(op) (hypercall_stubs + (op) * 32)
#define HYPERCALL_C_OPERAND(name) "g" (HYPERCALL_LOCATION(__HYPERVISOR_##name))
#endif

#define HYPERCALL_ARG(arg, n) \
	register typeof((arg)+0) __arg##n asm(HYPERCALL_arg##n) = (arg)

#define _hypercall0(type, name)					\
({								\
	type __res;						\
	asm volatile (						\
		"call " HYPERCALL_ASM_OPERAND "1"		\
		: "=a" (__res)					\
		: HYPERCALL_C_OPERAND(name)			\
		: "memory" );					\
	__res;							\
})

#define _hypercall1(type, name, arg)				\
({								\
	type __res;						\
	HYPERCALL_ARG(arg, 1);					\
	asm volatile (						\
		"call " HYPERCALL_ASM_OPERAND "2"		\
		: "=a" (__res), "+r" (__arg1)			\
		: HYPERCALL_C_OPERAND(name)			\
		: "memory" );					\
	__res;							\
})

#define _hypercall2(type, name, a1, a2)				\
({								\
	type __res;						\
	HYPERCALL_ARG(a1, 1);					\
	HYPERCALL_ARG(a2, 2);					\
	asm volatile (						\
		"call " HYPERCALL_ASM_OPERAND "3"		\
		: "=a" (__res), "+r" (__arg1), "+r" (__arg2)	\
		: HYPERCALL_C_OPERAND(name)			\
		: "memory" );					\
	__res;							\
})

#define _hypercall3(type, name, a1, a2, a3)			\
({								\
	type __res;						\
	HYPERCALL_ARG(a1, 1);					\
	HYPERCALL_ARG(a2, 2);					\
	HYPERCALL_ARG(a3, 3);					\
	asm volatile (						\
		"call " HYPERCALL_ASM_OPERAND "4"		\
		: "=a" (__res), "+r" (__arg1),			\
		  "+r" (__arg2), "+r" (__arg3)			\
		: HYPERCALL_C_OPERAND(name)			\
		: "memory" );					\
	__res;							\
})

#define _hypercall4(type, name, a1, a2, a3, a4)			\
({								\
	type __res;						\
	HYPERCALL_ARG(a1, 1);					\
	HYPERCALL_ARG(a2, 2);					\
	HYPERCALL_ARG(a3, 3);					\
	HYPERCALL_ARG(a4, 4);					\
	asm volatile (						\
		"call " HYPERCALL_ASM_OPERAND "5"		\
		: "=a" (__res), "+r" (__arg1), "+r" (__arg2),	\
		  "+r" (__arg3), "+r" (__arg4)			\
		: HYPERCALL_C_OPERAND(name)			\
		: "memory" );					\
	__res;							\
})

#define _hypercall5(type, name, a1, a2, a3, a4, a5)		\
({								\
	type __res;						\
	HYPERCALL_ARG(a1, 1);					\
	HYPERCALL_ARG(a2, 2);					\
	HYPERCALL_ARG(a3, 3);					\
	HYPERCALL_ARG(a4, 4);					\
	HYPERCALL_ARG(a5, 5);					\
	asm volatile (						\
		"call " HYPERCALL_ASM_OPERAND "6"		\
		: "=a" (__res), "+r" (__arg1), "+r" (__arg2),	\
		  "+r" (__arg3), "+r" (__arg4), "+r" (__arg5)	\
		: HYPERCALL_C_OPERAND(name)			\
		: "memory" );					\
	__res;							\
})

#define _hypercall(type, op, a1, a2, a3, a4, a5)		\
({								\
	type __res;						\
	HYPERCALL_ARG(a1, 1);					\
	HYPERCALL_ARG(a2, 2);					\
	HYPERCALL_ARG(a3, 3);					\
	HYPERCALL_ARG(a4, 4);					\
	HYPERCALL_ARG(a5, 5);					\
	asm volatile (						\
		"call *%6"					\
		: "=a" (__res), "+r" (__arg1), "+r" (__arg2),	\
		  "+r" (__arg3), "+r" (__arg4), "+r" (__arg5)	\
		: "g" (HYPERCALL_LOCATION(op))			\
		: "memory" );					\
	__res;							\
})

#ifdef CONFIG_X86_32
# include "hypercall_32.h"
#else
# include "hypercall_64.h"
#endif

static inline int __must_check
HYPERVISOR_set_trap_table(
	const trap_info_t *table)
{
	return _hypercall1(int, set_trap_table, table);
}

static inline int __must_check
HYPERVISOR_mmu_update(
	mmu_update_t *req, unsigned int count, unsigned int *success_count,
	domid_t domid)
{
	if (arch_use_lazy_mmu_mode())
		return xen_multi_mmu_update(req, count, success_count, domid);
	return _hypercall4(int, mmu_update, req, count, success_count, domid);
}

static inline int __must_check
HYPERVISOR_mmuext_op(
	struct mmuext_op *op, unsigned int count, unsigned int *success_count,
	domid_t domid)
{
	if (arch_use_lazy_mmu_mode())
		return xen_multi_mmuext_op(op, count, success_count, domid);
	return _hypercall4(int, mmuext_op, op, count, success_count, domid);
}

static inline int __must_check
HYPERVISOR_set_gdt(
	unsigned long *frame_list, unsigned int entries)
{
	return _hypercall2(int, set_gdt, frame_list, entries);
}

static inline int __must_check
HYPERVISOR_stack_switch(
	unsigned long ss, unsigned long esp)
{
	return _hypercall2(int, stack_switch, ss, esp);
}

static inline int
HYPERVISOR_fpu_taskswitch(
	int set)
{
	return _hypercall1(int, fpu_taskswitch, set);
}

#if CONFIG_XEN_COMPAT <= 0x030002
static inline int __must_check
HYPERVISOR_sched_op_compat(
	int cmd, unsigned long arg)
{
	return _hypercall2(int, sched_op_compat, cmd, arg);
}
#endif

static inline int __must_check
HYPERVISOR_sched_op(
	int cmd, void *arg)
{
	return _hypercall2(int, sched_op, cmd, arg);
}

static inline int __must_check
HYPERVISOR_platform_op(
	struct xen_platform_op *platform_op)
{
	platform_op->interface_version = XENPF_INTERFACE_VERSION;
	return _hypercall1(int, platform_op, platform_op);
}

struct xen_mc;
static inline int __must_check
HYPERVISOR_mca(
	struct xen_mc *mc_op)
{
	mc_op->interface_version = XEN_MCA_INTERFACE_VERSION;
	return _hypercall1(int, mca, mc_op);
}

static inline int __must_check
HYPERVISOR_set_debugreg(
	unsigned int reg, unsigned long value)
{
	return _hypercall2(int, set_debugreg, reg, value);
}

static inline unsigned long __must_check
HYPERVISOR_get_debugreg(
	unsigned int reg)
{
	return _hypercall1(unsigned long, get_debugreg, reg);
}

static inline int __must_check
HYPERVISOR_memory_op(
	unsigned int cmd, void *arg)
{
	if (arch_use_lazy_mmu_mode())
		xen_multicall_flush(false);
	return _hypercall2(int, memory_op, cmd, arg);
}

static inline int __must_check
HYPERVISOR_multicall(
	multicall_entry_t *call_list, unsigned int nr_calls)
{
	return _hypercall2(int, multicall, call_list, nr_calls);
}

static inline int __must_check
HYPERVISOR_event_channel_op(
	int cmd, void *arg)
{
	int rc = _hypercall2(int, event_channel_op, cmd, arg);

#if CONFIG_XEN_COMPAT <= 0x030002
	if (unlikely(rc == -ENOSYS)) {
		struct evtchn_op op;
		op.cmd = cmd;
		memcpy(&op.u, arg, sizeof(op.u));
		rc = _hypercall1(int, event_channel_op_compat, &op);
		memcpy(arg, &op.u, sizeof(op.u));
	}
#endif

	return rc;
}

static inline int __must_check
HYPERVISOR_xen_version(
	int cmd, void *arg)
{
	return _hypercall2(int, xen_version, cmd, arg);
}

static inline int __must_check
HYPERVISOR_console_io(
	int cmd, unsigned int count, char *str)
{
	return _hypercall3(int, console_io, cmd, count, str);
}

static inline int __must_check
HYPERVISOR_physdev_op(
	int cmd, void *arg)
{
	int rc = _hypercall2(int, physdev_op, cmd, arg);

#if CONFIG_XEN_COMPAT <= 0x030002
	if (unlikely(rc == -ENOSYS)) {
		struct physdev_op op;
		op.cmd = cmd;
		memcpy(&op.u, arg, sizeof(op.u));
		rc = _hypercall1(int, physdev_op_compat, &op);
		memcpy(arg, &op.u, sizeof(op.u));
	}
#endif

	return rc;
}

static inline int __must_check
HYPERVISOR_grant_table_op(
	unsigned int cmd, void *uop, unsigned int count)
{
	bool fixup = false;
	int rc;

	if (arch_use_lazy_mmu_mode())
		xen_multicall_flush(false);
#ifdef GNTTABOP_map_grant_ref
	if (cmd == GNTTABOP_map_grant_ref)
#endif
		fixup = gnttab_pre_map_adjust(cmd, uop, count);
	rc = _hypercall3(int, grant_table_op, cmd, uop, count);
	if (rc == 0 && fixup)
		rc = gnttab_post_map_adjust(uop, count);
	return rc;
}

static inline int __must_check
HYPERVISOR_vm_assist(
	unsigned int cmd, unsigned int type)
{
	return _hypercall2(int, vm_assist, cmd, type);
}

static inline int __must_check
HYPERVISOR_vcpu_op(
	int cmd, unsigned int vcpuid, void *extra_args)
{
	return _hypercall3(int, vcpu_op, cmd, vcpuid, extra_args);
}

static inline int __must_check
HYPERVISOR_suspend(
	unsigned long srec)
{
	struct sched_shutdown sched_shutdown = {
		.reason = SHUTDOWN_suspend
	};

	int rc = _hypercall3(int, sched_op, SCHEDOP_shutdown,
			     &sched_shutdown, srec);

#if CONFIG_XEN_COMPAT <= 0x030002
	if (rc == -ENOSYS)
		rc = _hypercall3(int, sched_op_compat, SCHEDOP_shutdown,
				 SHUTDOWN_suspend, srec);
#endif

	return rc;
}

#if CONFIG_XEN_COMPAT <= 0x030002
static inline int
HYPERVISOR_nmi_op(
	unsigned long op, void *arg)
{
	return _hypercall2(int, nmi_op, op, arg);
}
#endif

#ifndef CONFIG_XEN
static inline unsigned long __must_check
HYPERVISOR_hvm_op(
    int op, void *arg)
{
    return _hypercall2(unsigned long, hvm_op, op, arg);
}
#endif

static inline int __must_check
HYPERVISOR_callback_op(
	int cmd, const void *arg)
{
	return _hypercall2(int, callback_op, cmd, arg);
}

static inline int __must_check
HYPERVISOR_xenoprof_op(
	int op, void *arg)
{
	return _hypercall2(int, xenoprof_op, op, arg);
}

static inline int __must_check
HYPERVISOR_kexec_op(
	unsigned long op, void *args)
{
	return _hypercall2(int, kexec_op, op, args);
}

static inline int __must_check
HYPERVISOR_tmem_op(
	struct tmem_op *op)
{
	return _hypercall1(int, tmem_op, op);
}

#endif /* __HYPERCALL_H__ */
