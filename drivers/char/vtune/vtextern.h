/*
 *  vtextern.h
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
 *	File: vtextern.h
 *
 *	Description: extern declarations
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#if !defined(_VTUNE_EXTERN_H)
#define _VTUNE_EXTERN_H

extern void *apic_local_addr;
extern BOOLEAN DTES_supported;
extern sys_samp_info vtune_sys_info;
extern samp_parm6 samp_parms;
extern sampinfo_t samp_info;
extern __u32 apic_perf_lvt;

extern driver_shared_area *pdsa;    /* DSA ptr */
#ifdef DSA_SUPPORT_MMAP
extern driver_shared_area *create_dsa(void);
extern int destroy_dsa(void);
#endif

/* PC Sample record buf */
extern void *buf_start;         /* start of sample buffer */
extern void *buf_end;           /* end of sample buffer */
extern __u32 buf_length;        /* size of sampling buffer */
extern void *p_sample_buf;      /* ptr to next sample in sample buf */

extern int g_max_samp_timer_ON;     /* Global max Timer status */
extern int g_start_delay_timer_ON;  /* Global Start delay Timer status */
extern BOOLEAN signal_thread_event;   /* flag to signal sampler thread */
extern int num_mod_rec;
extern __u32 ebs_irq;

extern struct timer_list delay_tmr; /* user specifies delay time */
extern struct timer_list time_out_tmr;  /* max sampling time */
extern spinlock_t sample_int_lock;
extern spinlock_t sample_exec_lock;
extern spinlock_t reg3f1_write_lock;
extern spinlock_t rdpmc_lock;

extern __u32 sample_max_samples;
extern __u32 sample_version;
extern __u32 sample_method;
extern __u32 sample_tsc;
extern __u32 sample_tsc_offset;
extern __u32 sample_rec_length;

extern unsigned long start_time;
extern unsigned long sample_delay;
extern unsigned long sample_interval;

extern unsigned long Samples_Per_Buf;
extern unsigned long sample_rate_us;
extern unsigned long sample_rate_ms;
extern unsigned long sample_rate;

extern RDPMC_BUF rdpmc_buf;
extern int rdpmc_msr_base;
extern BOOLEAN pebs_option;
extern __u32 reset_and_power_on_pubs;   
extern ULARGE_INTEGER max_counter;
extern __u32 g_CPU_family;
extern __u32 g_CPU_model;

extern REG_SET reg_set[MAX_REG_SET_ENTRIES];
extern REG_SET reg_set0[MAX_REG_SET_ENTRIES];
extern REG_SET reg_set1[MAX_REG_SET_ENTRIES];
extern REG_SET reg_set_init[MAX_REG_SET_ENTRIES];
extern void *samp_EBS_idt_routine[MAX_PROCESSORS];

extern int event_count;
extern char event_Ids[MAX_ACTIVE_EVENTS];

extern BOOLEAN IA32_family5;
extern BOOLEAN IA32_family6;
extern BOOLEAN IA32_familyF;

#define PSTATUS_EMON_REGS_SAVED 2   

#endif /* _VTUNE_EXTERN_H */
