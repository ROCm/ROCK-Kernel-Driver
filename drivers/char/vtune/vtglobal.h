/*
 *  vtglobal.h
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
 *	File: vtglobal.h
 *
 *	Description: globally used/useful variables, constants
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#if !defined(_VTUNE_GLOBAL_H)
#define _VTUNE_GLOBAL_H

#include <asm/hw_irq.h>

#include "vtypes.h"
#include "familyf_msr.h"

int vtune_major = 0;            /* Major Device ID  stored in /dev */

unsigned int glob_sample_record_size = sizeof (sample_record_PC);

#if defined(linux32)
void *apic_local_addr;
unsigned long apic_paddr, apic_physical_addr;
unsigned long apic_timer_lvt;       /* local apic timer LVT (0x320 on P6) */
unsigned long current_apic_perf_lvt;    /* current local apic performance LVT (0x340 on P6) */
#endif

#if defined(linux32) || defined(linux32_64)
long apic_perf_lvt;         /* local apic performance LVT (0x340 on P6) */ 
#endif

#define PSTATUS_EMON_REGS_SAVED 2   // emon regs have been saved

#define LINUX32_APP_BASE_LOW   0x08000000
#define LINUX32_APP_BASE_HIGH  0x40000000

#define LINUX64_APP_BASE_LOW   0x4000000000000000
#define LINUX64_APP_BASE_HIGH  0x6000000000000000

__u32_PTR app_base_low;     // these fields are used to determine if a loaded module is an "exe" (primary binary for a process)
__u32_PTR app_base_high;        // ..

short my_cs, my_ds;
unsigned long *my_idt;

int p6_def_pesr = 0x530000;
int local_timer_icount = 0x50d4f;

char intelid[] = "Genuineintel";    /* intel processor ID */
unsigned long g_this_CPUID = 0;
unsigned long g_this_CPU_features = 0;
__u32 g_CPU_family = 0;
__u32 g_CPU_model = 0;

#ifdef USE_TYPE_WAIT_QUEUE_OLD
struct wait_queue *pc_write;        /* pc buffer full write event */
#endif

#ifdef USE_TYPE_WAIT_QUEUE_NEW
static DECLARE_WAIT_QUEUE_HEAD(pc_write);
static DECLARE_WAIT_QUEUE_HEAD(samp_delay);
#endif

/* sample file beginning size, 8 entries in sample_file_sections */
#define sfb_len  ((sizeof(sample_file_beginning)  + (7 * sizeof(sample_file_sections))) + 15) & ~15

spinlock_t sample_exec_lock = SPIN_LOCK_UNLOCKED;
spinlock_t sample_int_lock = SPIN_LOCK_UNLOCKED;
spinlock_t reg3f1_write_lock = SPIN_LOCK_UNLOCKED;
spinlock_t rdpmc_lock = SPIN_LOCK_UNLOCKED;

/* pc sample recored buf */
void *buf_start = NULL;     /* start of sample buffer */
void *buf_end = NULL;       /* end of sample buffer */
unsigned long buf_length;   /* size of sampling buffer */
void *p_sample_buf;     /* ptr to next sample in sample buf */

unsigned long current_buffer_count = 0; /* Current Sample buffer Count */
int continuous_sampling = 0;    /* flag for non-stop sampling mode */
driver_shared_area *pdsa = 0;   /* pointer to driver_shared_area */

__u32 sample_rec_length;    // length of sample record
__u32 sample_tsc;           // record cpu time stamp with sample
__u32 sample_tsc_offset;    // offset to tsc in sample record
__u32 sample_rec_length;    // length ofr sample record

unsigned long start_time = 0;   /* time sampling started */
unsigned long sample_delay = 0; /* sample start delay */
unsigned long sample_interval = 0;  /* total time to sample */

//unsigned long sample_method = 0;      /* sampling method */
unsigned long Samples_Per_Buf = 0;  /* samples per buffer */
unsigned long sample_rate_us = 0;   /* sample rate in microseconds */
unsigned long sample_rate_ms = 0;   /* sample rate in milliseconds */
unsigned long sample_rate = 0;      /* sample rate in clock ticks */

int num_mod_rec = 0;
int num_pid_create_rec = 0;
unsigned long current_sample_loop_count;

struct timer_list delay_tmr;        /* user specifies delay time */
struct timer_list time_out_tmr;     /* max sampling time */

int g_max_samp_timer_ON = B_FALSE;  /* Global max Timer status */
int g_start_delay_timer_ON = B_FALSE;   /* Global Start delay Timer status */

BOOLEAN signal_thread_event;  /* flag to signal sampler thread */
BOOLEAN terminate_thread;

sys_samp_info vtune_sys_info;   /* system level info */
sampinfo_t samp_info;       /* sample information area */
samp_parm6 samp_parms;      /* sample parameters */

module_record *mrlist;
module_record *last = (module_record *) 0;

static int file_count = 0;

#ifdef USE_TYPE_FILE_OPS_NEW
static struct file_operations vtune_fops = {
    read:vtune_read,
    write:vtune_write,
    open:vtune_open,
    ioctl:vtune_ioctl,
    release:vtune_release,
#ifdef DSA_SUPPORT_MMAP
    mmap:vtune_mmap,
#endif // DSA_SUPPORT_MMAP
};
#endif

#ifdef USE_TYPE_FILE_OPS_OLD
struct file_operations vtune_fops = {
    NULL,           /* lseek */
    vtune_read,     /* read */
    vtune_write,    /* write */
    NULL,           /* readdir */
    NULL,           /* poll */
    vtune_ioctl,    /* ioctl */
#ifdef DSA_SUPPORT_MMAP
    vtune_mmap,     /* mmap */
#else
    NULL,           /* mmap */
#endif // DSA_SUPPORT_MMAP
    vtune_open,     /* open */
    NULL,           /* flush */
    vtune_release,  /* release */
    NULL,           /* fsync */
    NULL,           /* sfsync */
    NULL,           /* check_media_change */
    NULL,           /* revalidate */
    NULL,           /* lock */
};
#endif

__u32 ebs_irq = 0;

/*
==========================================================================================
    5-04-01 - TVK (New Model Port ) 
==========================================================================================
*/

samp_user_config_stat_t samp_user_config_stat;

//
// Sampling method. Multiple methods can be active
// for a sampling session
//
__u32 sample_method = 0;        // sample method

void *sample_ptr;       // sample buf pointer

__u32 sample_max_samples;       // maximum samples to collect
int sample_version;     // sample parms version
                // ..for backward compatibility
ULARGE_INTEGER max_counter;

/* Note: These registers are written once on each OS cpu at the
         start of sampling. They are not written at during the
         EBS counter overflow ISR 
*/

REG_SET reg_set[MAX_REG_SET_ENTRIES];   // registers for configure
REG_SET reg_set0[MAX_REG_SET_ENTRIES];  // registers for Pentium(R) 4 processor thread 0
REG_SET reg_set1[MAX_REG_SET_ENTRIES];  // registers for Pentium(R) 4 processor thread 1
REG_SET reg_set_init[MAX_REG_SET_ENTRIES];  // registers for all cpu's

int event_count;            // number of events in event_Ids array
char event_Ids[MAX_ACTIVE_EVENTS];  // event IDs for active for sampling session

BOOLEAN IA32_family5 = B_FALSE;   // 
BOOLEAN IA32_family6 = B_FALSE;   // 
BOOLEAN IA32_familyF = B_FALSE;   // 

__u32 rdpmc_msr_base;               // msr of 1st cpu perf counter (IA32)

BOOLEAN DTES_supported = FALSE;     // CPU supports DTES feature
BOOLEAN HT_supported = FALSE;       // CPU supports Hyper-Threading Technology
BOOLEAN pebs_option = B_FALSE;    // precise event sampling active
__u32 pebs_err;                     // error during precise EBS ISR
__u32 logical_processors_per_package;   // Pentium(R) 4 processor: logical processors per phycal processor
__u32 logical_processors_shift;     

#endif /* _VTUNE_GLOBAL_H */
