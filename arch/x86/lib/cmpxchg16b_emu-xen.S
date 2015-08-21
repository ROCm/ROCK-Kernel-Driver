/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; version 2
 *	of the License.
 *
 */
#include <linux/linkage.h>
#include <linux/stringify.h>
#include <asm/irqflags.h>
#include <asm/percpu.h>
#include <xen/interface/xen.h>

.text

/*
 * Inputs:
 * %rsi : memory location to compare
 * %rax : low 64 bits of old value
 * %rdx : high 64 bits of old value
 * %rbx : low 64 bits of new value
 * %rcx : high 64 bits of new value
 * %al  : Operation successful
 */
ENTRY(this_cpu_cmpxchg16b_emu)

#
# Emulate 'cmpxchg16b %gs:(%rsi)' except we return the result in %al not
# via the ZF.  Caller will access %al to get result.
#
# Note that this is only useful for a cpuops operation.  Meaning that we
# do *not* have a fully atomic operation but just an operation that is
# *atomic* on a single cpu (as provided by the this_cpu_xx class of
# macros).
#
	.ifeqs __stringify(DISABLE_INTERRUPTS(_)), \
	       __stringify(__DISABLE_INTERRUPTS)
	.equ rbp_saved, 0
	.else
	pushq %rbp
	.equ rbp_saved, 1
#ifndef __REG_si
# error Out of sync with asm/irqflags.h!
#endif
#undef __REG_si
#define __REG_si %rbp
#define esi ebp
	GET_VCPU_INFO
	.endif
	pushq %rdi
	__SAVE_INTERRUPTS(edi)
	__DISABLE_INTERRUPTS

	cmpq PER_CPU_VAR((%rsi)), %rax
	jne .Lnot_same
	cmpq PER_CPU_VAR(8(%rsi)), %rdx
	jne .Lnot_same

	movq %rbx, PER_CPU_VAR((%rsi))
	movq %rcx, PER_CPU_VAR(8(%rsi))

.Lnot_same:
	sete %al

	test %edi, %edi
	jz .Lcheck_events
.Lexit:
	popq %rdi
	.if rbp_saved
	popq %rbp
	.endif
	ret

.Lcheck_events:
	__ENABLE_INTERRUPTS
	__TEST_PENDING
	jz .Lexit
#undef __REG_si
#undef esi
	pushq %rsi
	pushq %rax
	/* %edi is zero already */
	xor %esi, %esi
	call hypercall_page + __HYPERVISOR_xen_version * 32
	popq %rax
	popq %rsi
	jmp .Lexit

ENDPROC(this_cpu_cmpxchg16b_emu)
