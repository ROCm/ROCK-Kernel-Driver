/*
 *  vtlibcommon32.h
 *
 *  Copyright (C) 2002-2004 Intel Corporation 
 *  Maintainer - Juan Villacis <juan.villacis@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
/*
 * ===========================================================================
 *
 *	File: vtlibcommon32.h 
 *
 *	Description: common IA32 performance counter programming functions
 *
 *	Author(s): Birju Shah, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#if !defined(_VTUNE_LIBCOMMON32_H)
#define _VTUNE_LIBCOMMON32_H

#define interlocked_exchange xchg

__u32
IA32_family_F_reg_type(__u32 reg);

ULARGE_INTEGER
samp_read_msr(__u32 reg);

void
samp_write_msr(__u32 reg, ULARGE_INTEGER val);

__u32
DTS_get_CPU_features(void);

BOOLEAN
known_reg(__u32 reg);

BOOLEAN
OS_safe_RDMSR(__u32 reg, PULARGE_INTEGER pui64_ret_value);

BOOLEAN
OS_safe_WRMSR_direct(__u32 reg, PULARGE_INTEGER pui64_new_value);

__u32
get_APICID(void);

void
samp_set_PEBS_this_thread(BOOLEAN enable);

void
samp_disable_PEB_sand_DTS(void);

BOOLEAN
CCCR_disable_counting_and_clear_ovf(__u32 cccr_reg);

void
reset_and_power_on_pub_counter(__u32 cccr_reg);

void
init_EBS_regs_for_package(void);

void
restore_emon_regs(void);

void
save_clear_init_emon_regs_for_package(BOOLEAN init_EBS_regs_for_package_bool);



PREG_SET
get_reg_set_table(void);

void
add_to_counter_total_on_overflow(PREG_SET p_reg_set, __u32 cpu);

void
add_to_counter_total_on_stop(void);

void
read_cpu_perf_counters_for_current_cpu(
    void *info
    );

__u32
samp_check_emon_counter_overflow_IA32_family5(void);

void
samp_stop_emon_IA32_family6(void);

void
samp_start_emon_IA32_family6(u32 do_stop);

__u32
samp_check_emon_counter_overflow_IA32_family6(void);

void
samp_stop_emon_IA32_familyF(void);

void
samp_start_emon_IA32_familyF(u32 do_stop);

__u32
samp_check_emon_counter_overflow_IA32_familyF(void);

void
samp_start_emon(void *info);

void
samp_stop_emon(void);

BOOLEAN
validate_reg_RW(__u32 reg, ULARGE_INTEGER val);

void
validate_emon_regs(void);

__u32
validate_EBS_regs(void);


void
driver_load(void);

void
driver_open(void);

void
driver_unload(void);

void
samp_init_emon_regs(void *info);

void
samp_start_profile_interrupt(void *info);

void
samp_stop_ints(void);

void
set_IA32_family_F_emon_defaults(void);

void
set_IA32_family6_emon_defaults(void);

BOOLEAN
set_event_ids(PREG_SET p_reg_set);

int
validate_samp_parm6(samp_parm6 * sp6, int sp6_len);

int
samp_configure6(samp_parm6 * sp6, int sp6_len);

void_ptr
samp_get_buf_space(__u32 length, u32 *wake_up_thread);

u32
samp_emon_interrupt(PINT_FRAME int_frame);

void
__outbyte(__u32 port, __u8 value);

__u8
__inbyte(__u32 port);

void
samp_build_csip_sample(PINT_FRAME int_frame, P_sample_record_PC p_sample);

#if defined(ALLOW_LBRS)
void
disable_lbr_capture(void);
#endif

void
samp_start_ints(void);

#endif /* _VTUNE_LIBCOMMON32_H */
