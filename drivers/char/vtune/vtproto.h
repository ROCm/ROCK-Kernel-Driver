/*
 *  vtproto.h
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
 *	File: vtproto.h
 *
 *	Description: function prototypes
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#if !defined(_VTUNE_PROTO_H)
#define _VTUNE_PROTO_H

#ifdef KERNEL_26X
#include <linux/fs.h>
#endif

#include "vtoshooks.h"

#ifdef KERNEL_26X
#ifndef smp_num_cpus
#define smp_num_cpus num_online_cpus()
#endif
#endif

#ifdef DSA_SUPPORT_MMAP
int vtune_mmap(struct file *, struct vm_area_struct *);
#endif // DSA_SUPPORT_MMAP

driver_shared_area *create_dsa(void);
int destroy_dsa(void);

#if defined(linux32) || defined(linux32_64)
void samp_apic_set_perf_lvt_int_mask(void);
void samp_apic_clear_perf_lvt_int_mask(void);
__u32 SAMP_Set_apic_perf_lvt(long apic_perf_lvt);
void SAMP_Set_Apic_Virtual_Wire_Mode(void);
#elif !defined(linux64)
#error Compiling for unsupported architecture
#endif
int ebs_intr(int irq, void *arg, struct pt_regs *regs);

ssize_t samp_write_module_file(struct file *, char *, size_t, loff_t *);
void samp_write_sample_file(void);

ssize_t vtune_read(struct file *, char *, size_t, loff_t *);
ssize_t vtune_write(struct file *, const char *, size_t, loff_t *);
int vtune_open(struct inode *, struct file *);
int vtune_release(struct inode *inode, struct file *filp);
int vtune_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
void vtune_sampreset(void);
int vtune_sampuserstop(void);

void vdrv_init_emon_regs(void);
int vdrv_start_EBS(void);
void vdrv_stop_EBS(void);

u32  samp_emon_interrupt(PINT_FRAME int_frame);
void samp_init_emon_regs(void *info);
void samp_start_profile_interrupt(void *info);
void samp_stop_profile_interrupt(void *info);
void samp_start_emon(void *info);
void samp_stop_emon(void);
void samp_restore_cpu_vectors(void);

int start_sampling6(samp_parm6 * sp6, int sp6_len);

PREG_SET get_reg_set_table(void);

void samp_start_delay(unsigned long);
void samp_max_sample_time_expired(unsigned long);
int samp_get_stats(sampinfo_t *);
int samp_get_parm(samp_parm3 *);

void free_mrlist(void);
int vdrvgetsysinfo(void);

ULARGE_INTEGER samp_read_msr(__u32 reg);

void samp_write_msr(__u32 reg, ULARGE_INTEGER val);

//
// CSS: TODO: Update the linux32 to use the newer C code instead...
//
#if defined(linux32)
void get_CSD(__u32, __u32 *, __u32 *);
#elif defined(linux32_64)
__u32 get_CSD(__u32, __u32 *, __u32 *);
#endif

/* ASM types (vtxsys*.S) */
__u32 get_APICID(void);
void save_clear_init_emon_regs_for_package(BOOLEAN);
void validate_emon_regs(void);
void driver_load(void);
void driver_open(void);
void driver_unload(void);
int init_driver_OS(void);
extern void t_ebs(void);
extern void samp_get_set_idt_entry(unsigned long, unsigned long, __u64 *);
extern void samp_restore_idt_entry(unsigned long, long long *);

/* vtlib*.c */
void set_IA32_family6_emon_defaults(void);
int samp_configure6(samp_parm6 * sp6, int sp6_len);
BOOLEAN set_event_I_ds(PREG_SET p_reg_set);
void read_cpu_perf_counters_for_current_cpu(void *info);

/* OS Services */
void_ptr allocate_pool(__u32 pool_type, __u32 i);
void free_pool(void_ptr i);

/* vtune.c */
void sample_skipped(void);
BOOLEAN check_pause_mode(void);

#ifdef ENABLE_TGID
/* Thread Group ID support */
int get_thread_group_id(struct task_struct *proc_task);
int find_thread_id(thread_info *p_thread_id);
#endif

#endif /* _VTUNE_PROTO_H */
