/*
 * PowerPC helpers for hypervisors without ILE implementation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright SUSE Linux Products GmbH 2013
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#include <asm/reg.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/exception-64s.h>

/* Little Endian fixups for hosts that don't support Little Endian */

#define FAKE_ILE_HANDLER(handler, area) 			 \
								 \
/* This runs in BE mode */					 \
fake_ile_##handler:						 \
	.section __be_patch,"a"					;\
	.llong fake_ile_##handler				;\
	.previous						;\
	SET_SCRATCH0(r13)					;\
	GET_PACA(r13)						;\
	std     r9, area + EX_R9(r13)				;\
	std     r10, area + EX_R10(r13)				;\
	mfsrr0	r9						;\
	mfsrr1	r10						;\
	std	r9, area + EX_SRR0(r13)				;\
	std	r10, area + EX_R11(r13)				;\
	mflr	r9						;\
	bl	1f						;\
	1:							;\
	mflr	r10						;\
	mtlr	r9						;\
	addi	r9, r10, back_to_interrupt_##handler - 1b	;\
	mfmsr	r10						;\
	ori	r10, r10, MSR_LE				;\
	mtsrr0	r9						;\
	mtsrr1	r10						;\
	ld	r9, area + EX_SRR0(r13)				;\
	ld	r10, area + EX_R11(r13)				;\
	RFI							;\
	end_fake_ile_##handler:					;\
	.section __be_patch,"a"					;\
	.llong end_fake_ile_##handler				;\
	.previous						;\
								;\
/* This runs in LE mode */					 \
back_to_interrupt_##handler:					;\
	mtsrr0	r9						;\
	mtsrr1	r10						;\
	li	r9, area + EX_R9				;\
	li	r10, area + EX_R10				;\
	ldbrx	r9, r13, r9					;\
	ldbrx	r10, r13, r10					;\
	GET_SCRATCH0(r13)					;\
	/* This becomes the instruction we patched away */	 \
	patched_insn_##handler:					;\
	.long 0							;\
	b 	handler + 4					;\
								 \
	.section __fake_ile,"a"					;\
	.llong handler						;\
	.llong patched_insn_##handler				;\
	.llong fake_ile_##handler				;\
	.previous						;\

FAKE_ILE_HANDLER(system_reset_pSeries, PACA_EXMC)
FAKE_ILE_HANDLER(machine_check_pSeries_1, PACA_EXMC)
FAKE_ILE_HANDLER(data_access_pSeries, PACA_EXGEN)
FAKE_ILE_HANDLER(data_access_slb_pSeries, PACA_EXSLB)
FAKE_ILE_HANDLER(instruction_access_pSeries, PACA_EXGEN)
FAKE_ILE_HANDLER(instruction_access_slb_pSeries, PACA_EXSLB)
FAKE_ILE_HANDLER(hardware_interrupt_pSeries, PACA_EXGEN)
FAKE_ILE_HANDLER(alignment_pSeries, PACA_EXGEN)
FAKE_ILE_HANDLER(program_check_pSeries, PACA_EXGEN)
FAKE_ILE_HANDLER(fp_unavailable_pSeries, PACA_EXGEN)
FAKE_ILE_HANDLER(decrementer_pSeries, PACA_EXGEN)
FAKE_ILE_HANDLER(doorbell_super_pSeries, PACA_EXGEN)
FAKE_ILE_HANDLER(trap_0b_pSeries, PACA_EXGEN)
FAKE_ILE_HANDLER(system_call_pSeries, PACA_EXGEN)
FAKE_ILE_HANDLER(performance_monitor_pseries_trampoline, PACA_EXGEN)
FAKE_ILE_HANDLER(altivec_unavailable_pseries_trampoline, PACA_EXGEN)
FAKE_ILE_HANDLER(vsx_unavailable_pseries_trampoline, PACA_EXGEN)
FAKE_ILE_HANDLER(facility_unavailable_trampoline, PACA_EXGEN)
FAKE_ILE_HANDLER(instruction_breakpoint_pSeries, PACA_EXGEN)
FAKE_ILE_HANDLER(altivec_assist_pSeries, PACA_EXGEN)
