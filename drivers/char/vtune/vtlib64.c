/*
 * vtlib64.c
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
 *	File: vtlib64.c
 *
 *	Description: functions for manipulating the hardware registers on
 *	             Itanium(R) processor family platforms
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#include <linux/vmalloc.h>
#include <linux/slab.h>     /*malloc */
#include <linux/module.h>
#include <asm/uaccess.h>

#include "vtuneshared.h"
#include "vtdef.h"
#include "vtproto.h"
#include "vtextern.h"

void samp_start_ints(BOOLEAN startall);

void samp_stop_ints(void);

#define non_paged_pool 0
#define profile_time  1

__u32 cpu_family;       // cpu family
__u32 EBS_vector;       // vector for counter overflow interrupt
__u32 init_driver_var = 1;      // driver initialized flag

__u16 ring0cs = 0x10;       // standard ring0 code segment  
__u16 ring3cs = 0x23;       // standard ring3 code segment  
__u32 valid_counters;       // bit map to indicate which counters we are using

#define EFLAGS_V86_MASK       0x00020000L

// PMC and PMD arrays for sampling session                     

#define NUMBER_OF_ITANIUM_GENERIC_COUNTERS 4    //  Need to fix!! should get PMC/PMD info from PAL       
__u32 counter_options[NUMBER_OF_ITANIUM_GENERIC_COUNTERS];  // counter overflow options          
#define COUNTER_OPTS_COLLECT_IEAR_REGS 0x1
#define COUNTER_OPTS_COLLECT_DEAR_REGS 0x2
#define COUNTER_OPTS_COLLECT_BTRACE_REGS 0x4
#define COUNTER_OPTS_EAR  (COUNTER_OPTS_COLLECT_IEAR_REGS | COUNTER_OPTS_COLLECT_DEAR_REGS | COUNTER_OPTS_COLLECT_BTRACE_REGS)

__u64 pmd17_slot_mask;
__u64 pmd17_slot_shift;
__u64 pmd17_bundle_mask;

__u64 pmc_es_mask;
__u64 pmc_es_shift;

__u64 dear_event_code;
__u64 iear_event_code;
__u64 btrace_event_code;

__u64 dear_offset;
__u64 iear_offset;
__u64 btrace_offset;

__u64 sample_options;
#define SAMPLE_OPTS_DEAR   1
#define SAMPLE_OPTS_IEAR   2
#define SAMPLE_OPTS_BTRACE 4

typedef struct PMC_SPEC_s {
    __u32 number;       // register
    __u32 reserved;     //
    ULARGE_INTEGER value;   // value
} PMC_SPEC;

typedef struct PMD_SPEC_s {
    __u32 number;       // register
    __u32 reserved;     //
    ULARGE_INTEGER value;   // value
    ULARGE_INTEGER event_inc;   // value added to event_total on counter overflow interrupt 
    ULARGE_INTEGER event_total[MAX_PROCESSORS]; // event total from start of sampling session   
} PMD_SPEC;

#define MAX_PMCS 20     // 10 entries to support setting of PMCs 4-13 Itanium(R) processor, 4-15 Itanium(R) 2 processor
#define MAX_PMDS 4      // 4  entries to support setting of PMDs 4-7 (4 counters)

PMC_SPEC pmcs[MAX_PMCS + 1];    // pmc regs for a sampling session
PMD_SPEC pmds[MAX_PMDS + 1];    // pmc regs for a sampling session

__u32 max_config_pmc = 15;  // maximum pmc which can be written for PMU configuration

#define MAX_OS_EVENTS 32
__s32 OS_events[MAX_OS_EVENTS]; //

void samp_save_set_cpu_vectors(void);

void samp_restore_cpu_vectors(void);

__u32
get_cpu_family(void)
{
    __u64 cpuid = 0;

    cpuid = itp_get_cpuid(3);  // get cpuid reg 3...contains version info
    // 63:40.... reverved
    // 39:32.... architecture revision
    // 31:24.... family
    // 23:16.... model
    // 15:8..... revision
    //  7:0..... max cpuid reg index

    return ((__u32) (cpuid & ITP_CPUID_REG3_FAMILY) >> 24);
}

void
set_pmv_mask(void)
{
    __u64 pmv;

    pmv = itp_read_reg_pmv();
    pmv |= PMV_MASK_BIT;
    itp_write_reg_pmv(pmv);

    return;
}

void
clear_PMV_mask(void)
{
    __u64 pmv;

    pmv = itp_read_reg_pmv();
    pmv &= ~PMV_MASK_BIT;
    itp_write_reg_pmv(pmv);

    return;
}

__u32
tbs_get_current_process_id(void)
{
    return (current->pid);
}

__u32
tbs_get_current_thread_id(void)
{
    return (current->pid);
}

__u32
get_current_processor_number(void)
{
    return (smp_processor_id());
}

void_ptr
allocate_pool(__u32 pool_type, __u32 i)
{
    return (kmalloc(i, GFP_ATOMIC));
}

void
free_pool(void_ptr i)
{
    kfree(i);

    return;
}

void
rtl_zero_memory(void_ptr dest, __u32 length)
{
    memset(dest, 0, length);

    return;
}

__u64
interlocked_exchange(__u32 * dest, __u32 val)
{
    return (xchg(dest, val));
}

void
add_to_counter_total_on_overflow(ULARGE_INTEGER pmc0, __u32 cpu)
{
    __u32 i, pmd_num;

    for (i = 0; i < MAX_PMDS; i++) {
        pmd_num = pmds[i].number;
        if ((pmd_num >= 4) && (pmd_num <= 7)) {
            //
            // If counter overflowed, add to event total
            //
            if (pmc0.low_part & (1 << pmd_num)) {
                pmds[i].event_total[cpu].quad_part +=
                    pmds[i].event_inc.quad_part;
            }
        }
    }

    return;
}

//
// Add the current PMC value to the counter total 
//
// This routine should be called once per cpu at the end of a sampling session.
// The counters should be stopped before calling this routine.
//
//

void
add_to_counter_total_on_stop(void)
{
    __u32 i, cpu, pmd_num;
    ULARGE_INTEGER val;

    cpu = get_current_processor_number();
    for (i = 0; i < MAX_PMDS; i++) {
        pmd_num = pmds[i].number;
        if ((pmd_num < 4) || (pmd_num > 7)) {
            continue;
        }
        val.quad_part = itp_read_reg_pmd(pmd_num);
        val.quad_part &= max_counter.quad_part;
        if (val.quad_part >= pmds[i].value.quad_part) {
            val.quad_part -= pmds[i].value.quad_part;
        }
        pmds[i].event_total[cpu].quad_part += val.quad_part;

    }

    return;
}



//
// Read CPU Performance Counters for the current cpu
//
// Fill in the "read pmc" buffer with current counter value and 
// accumlated total for each PMC.
//
void 
read_cpu_perf_counters_for_current_cpu(
    void *info
    )
{
    __u32 i, cpu, tmp_cpu_num, cpu_mask, pmc_num;
    ULARGE_INTEGER pmc_val, start_total, pmc_mask, pmc_mask_read;
    RDPMC_BUF *pr_buf;

    pr_buf = (RDPMC_BUF *) info;

    if (!pr_buf)
    {
        return;
    }

    cpu = get_current_processor_number();

    if (cpu >= RDPMC_MAX_CPUS)
    {
      return;
    }

    cpu_mask = 1 << cpu;

    pmc_mask_read.quad_part = 0;
    //
    // Update RDPMC data for current cpu. Skip update if we already did it
    // 
    if ((pr_buf->cpu_mask_in & cpu_mask) && !(pr_buf->cpu_mask_out & cpu_mask))   
    {
        //
        // Fill in RDPMC buffer for current cpu
        //
        for (i=0; i < MAX_PMDS; i++) 
        {
            pmc_num = pmds[i].number;
            if ((pmc_num < 4) || (pmc_num > 7))
            {
                continue;
            }
            pmc_mask.quad_part = 1 << pmc_num;
            //
            // If pmc is not in regSet, skip it.
            //
            if (!(pr_buf->pmc_mask.quad_part & pmc_mask.quad_part))
            {
                continue;
            }

            //
            // Since the routine can't handle larger than RDPMC_MAX_CPUS,
            // we need to add the extra CPU data into the results
            //
	    for (tmp_cpu_num = cpu ; tmp_cpu_num < MAX_PROCESSORS; tmp_cpu_num += RDPMC_MAX_CPUS)
	    {
	      //
	      // Read current pmc and update pmc total. If the total changed accross the read,
	      // then a counteroverflow must have occured so we redo the read until we get a
	      // valid pair of values for pmc current/total.
	      //
	      do
	      {
                start_total = pmds[i].event_total[tmp_cpu_num];
                pmc_val.quad_part = itp_read_reg_pmd(pmc_num);
	      } while (start_total.quad_part != pmds[i].event_total[tmp_cpu_num].quad_part);

	      //
	      // Only update the current value once with a CPU within the normal
	      // 0->RDPMC_MAX_CPUS limit
	      //
	      if (tmp_cpu_num < RDPMC_MAX_CPUS) {
		pr_buf->pmc_values[pmc_num].pmc_val[cpu].pmc_current.quad_part = pmc_val.quad_part & max_counter.quad_part;
	      }

	      pr_buf->pmc_values[pmc_num].pmc_val[cpu].pmc_total.quad_part   += start_total.quad_part;
	    }

            pmc_mask_read.quad_part |= pmc_mask.quad_part;
        }

        //
        // Return current value for pmc's that were not in the RegSet
        //
        // Note!! 
        // For now, only support reading pmd's 4-7. Do other pmds when we have 
        // have a version of I64ReadRegPMD that uses try/except logic.
        //
        pmc_mask.quad_part = 1;
        for (i=0; i < RDPMC_MAX_PMCS; i++, pmc_mask.quad_part = pmc_mask.quad_part << 1)
        {
            if (!(pr_buf->pmc_mask.quad_part & pmc_mask.quad_part) ||
                 (pmc_mask_read.quad_part & pmc_mask.quad_part))
            {
                continue;
            }
            if ((i >= 4) && (i <= 7))
            {
                pmc_val.quad_part = itp_read_reg_pmd(i);
                pr_buf->pmc_values[i].pmc_val[cpu].pmc_current.quad_part = pmc_val.quad_part & max_counter.quad_part;
            }
            else
            {
                pr_buf->pmc_values[i].pmc_val[cpu].pmc_current.quad_part = 0;
            }
        }

        spin_lock(&rdpmc_lock);
        pr_buf->cpu_mask_out |= cpu_mask;
        spin_unlock(&rdpmc_lock);    
    }

    return;
}

void
init_emon_regs_itanium(void)
/*++

Routine description:

    Initialize emon regs to a know state at the start of an EBS session.

Arguments:

return value:

    none

--*/
{
    __u32 i;
    __u64 val;

    // Freeze all counters and clear all overflow status bits

    set_pmv_mask();
    val = itp_read_reg_pmc(0);
    val = ~(PMC0_OFLOWALL); // clear all overflow bits before setting pmc0.fr to avoid problems on Itanium(R) 2 processor
    val |= PMC0_FREEZE; // set freeze
    itp_write_reg_pmc(0, val);
    clear_PMV_mask();

    //
    // Clear overflow interrupt and privilege level in all generic PMCs (4-7). 
    // Clearing oi prevents interrupts, and setting plm=0 effectively stops counting for that pmc/pmd pair.
    //
    for (i = 4; i < 8; i++) {
        val = itp_read_reg_pmc(i);
        val &= ~(PMC4_OI | PMC4_PLM);   // turn off pmc.oi to prevent counter overflow interrupt. Clear plm to stop counting
        itp_write_reg_pmc(i, val);  // ..
        itp_write_reg_pmd(i, 0);    // clear counter
    }

    //
    // Set PMC8 and PMC9 to disable address range and opcode matching
    //
    {
        __u64 pmc8, pmc8new;
        __u64 pmc9, pmc9new;

        pmc8 = itp_read_reg_pmc(8);
        itp_write_reg_pmc(8, -1);
        pmc8new = itp_read_reg_pmc(8);

        // init pmc9                                                                    10-22-00
        pmc9 = itp_read_reg_pmc(9);
        itp_write_reg_pmc(9, -1);
        pmc9new = itp_read_reg_pmc(9);
    }

    //
    // Clear PMC10.plm to disable Instruntion EAR monitoring
    //
    i = 10;
    val = itp_read_reg_pmc(i);
    val &= ~0xf;        // set plm = 0 to disable monitoring
    itp_write_reg_pmc(i, val);

    //
    // Clear PMC11.plm to disable Data EAR monitoring
    //
    i = 11;
    val = itp_read_reg_pmc(i);
    val &= ~0xf;        // set plm = 0 to disable monitoring
    val |= 1 << 28;     // set pt = 1 to disable data address range checking    10-22-00
    itp_write_reg_pmc(i, val);

    //
    // Clear PMC12.plm to disable Branch Trace buffer collection
    //
    i = 12;
    val = itp_read_reg_pmc(i);
    val &= ~0xf;        // set plm = 0 to disable monitoring
    itp_write_reg_pmc(i, val);

    //
    // Set "Tag All" bit in pmc13 to disable address range matching
    //
    {
        __u64 pmc13, pmc13new;

        pmc13 = itp_read_reg_pmc(13);
        pmc13new = pmc13 | 1;   // set "Tag All" bit
        itp_write_reg_pmc(13, pmc13new);
        pmc13new = itp_read_reg_pmc(13);
    }

    return;
}

void
init_emon_regs_itanium2(void)
/*++

Routine description:

    Initialize emon regs to a know state at the start of an EBS session.

Arguments:

return value:

    none

--*/
{
    __u32 i;
    __u64 val;

    //
    // Enable performance monitoring HW on Itanium(R) 2 processor (PMC4 bit 23 = Enable PM)
    //
    {
        ULARGE_INTEGER pmc4, pmc4new;

        pmc4.quad_part = itp_read_reg_pmc(4);
        if (!(pmc4.low_part & (1 << 23))) {
            pmc4new = pmc4;
            pmc4new.low_part |= (1 << 23);
            itp_write_reg_pmc(4, pmc4new.quad_part);
            pmc4new.quad_part = itp_read_reg_pmc(4);
        }
    }

    // Freeze all counters and clear all overflow status bits

    set_pmv_mask();
    val = itp_read_reg_pmc(0);
    val = ~(PMC0_OFLOWALL); // clear all overflow bits before setting pmc0.fr to avoid problems on Itanium(R) 2 processor
    val |= PMC0_FREEZE; // set freeze
    itp_write_reg_pmc(0, val);
    clear_PMV_mask();

    //
    // Clear overflow interrupt and privilege level in all generic PMCs (4-7). 
    // Clearing oi prevents interrupts, and setting plm=0 effectively stops counting for that pmc/pmd pair.
    //
    for (i = 4; i < 8; i++) {
        val = itp_read_reg_pmc(i);
        val &= ~(PMC4_OI | PMC4_PLM);   // turn off pmc.oi to prevent counter overflow interrupt. Clear plm to stop counting
        itp_write_reg_pmc(i, val);  // ..
        itp_write_reg_pmd(i, 0);    // clear counter
    }

    //
    // Reset the global pmc's to power on values
    //

    itp_write_reg_pmc(8, -1);
    itp_write_reg_pmc(9, -1);

    itp_write_reg_pmc(10, 0);
    itp_write_reg_pmc(11, 0);
    itp_write_reg_pmc(12, 0);

    itp_write_reg_pmc(13, 0x2078fefefefe);
    itp_write_reg_pmc(14, 0xdb6);
    itp_write_reg_pmc(15, 0xfffffff0);

    return;
}

void
init_emon_regs(void)
/*++

Routine description:

    Initialize emon regs to a know state at the start of an EBS session.

Arguments:

return value:

    none

--*/
{
    switch (cpu_family) {
    case ITP_CPU_FAMILY_ITANIUM:
        init_emon_regs_itanium();
        break;
    case ITP_CPU_FAMILY_ITANIUM2:
        init_emon_regs_itanium2();
        break;
    default:
        break;
    }

    return;
}

__u32
init_driver(void)
{
    init_driver_OS();

    /* Initialize system info storage */
    memset(&samp_info, 0, sizeof (sampinfo_t));
    memset(&samp_parms, 0, sizeof (samp_parm6));

    samp_info.flags = SINFO_STOP_COMPLETE;
    cpu_family = get_cpu_family();

    //
    // Set max counter value and max wr4iteable pmc.
    // Need to fix!! 
    // Counter width should be determined from Itanium(R) processor HW regs.
    //
    max_counter.quad_part = ITANIUM2_MAXCOUNTER;
    max_config_pmc = ITANIUM2_MAX_CONFIG_PMC;
    pmd17_slot_mask = ITANIUM2_PMD17_SLOT_MASK;
    pmd17_slot_shift = ITANIUM2_PMD17_SLOT_SHIFT;
    pmd17_bundle_mask = ITANIUM2_PMD17_BUNDLE_MASK;

    pmc_es_mask = ITANIUM2_PMC_ES_MASK;
    pmc_es_shift = ITANIUM2_PMC_ES_SHIFT;

    dear_event_code = ITANIUM2_DEAR_EVENT_CODE;
    iear_event_code = ITANIUM2_IEAR_EVENT_CODE;
    btrace_event_code = ITANIUM2_BTRACE_EVENT_CODE;

    //
    // Set EAR data offsets to 0 to prevent collection of 
    // ear data until we have a way to pass size of 
    // SampleRecord to user mode code. (Now done using DSA)
    //
    dear_offset = iear_offset = btrace_offset = 0;

    if (cpu_family == ITP_CPU_FAMILY_ITANIUM) {
        max_counter.quad_part = ITANIUM_MAXCOUNTER;
        max_config_pmc = ITANIUM_MAX_CONFIG_PMC;
        pmd17_slot_mask = ITANIUM_PMD17_SLOT_MASK;
        pmd17_slot_shift = ITANIUM_PMD17_SLOT_SHIFT;
        pmd17_bundle_mask = ITANIUM_PMD17_BUNDLE_MASK;
        pmc_es_mask = ITANIUM_PMC_ES_MASK;
        pmc_es_shift = ITANIUM_PMC_ES_SHIFT;
        dear_event_code = ITANIUM_DEAR_EVENT_CODE;
        iear_event_code = ITANIUM_IEAR_EVENT_CODE;
        btrace_event_code = ITANIUM_BTRACE_EVENT_CODE;
    }

    /* Initialize global sampling control flags */
    samp_info.flags = SINFO_STOP_COMPLETE;
    samp_info.sampling_active = FALSE;
    signal_thread_event = FALSE;

    /*   Initialize timers  and resources */
    g_max_samp_timer_ON = FALSE;
    g_start_delay_timer_ON = FALSE;
    init_timer(&delay_tmr);
    init_timer(&time_out_tmr);

    memset(&eachCPU[0], 0, sizeof(eachCPU)); // zero out entire array of PER_CPU entries

    //
    // Allocate Driver Shared Area (DSA) in non paged pool.
    //

#ifdef DSA_SUPPORT_MMAP
    pdsa = create_dsa();
#endif
    if(pdsa == NULL)
    {
      VDK_PRINT_ERROR("init_driver: could not create DSA\n");
      return (STATUS_INVALID_PARAMETER);
    }
    
    pdsa->num_event_counters = 4;   // four counters on Itanium(R) processor   08-06-00
    pdsa->method_EBS = TRUE;
    pdsa->module_tracking = TRUE;   // driver can do module tracking

    return (STATUS_SUCCESS);
}

/*
**********************************************************************************************************

    START/STOP EBS FUNCTIONS       

**********************************************************************************************************
*/

void
samp_start_emon_itp(BOOLEAN startall)
/*++

Routine description:

Arguments:

return value:

    none

--*/
{
    __u32 i, pmc, pmd;
    __u64 pmc0, pmc0w;

    if (sample_method & METHOD_EBS) {

        //
        // mask pmi before setting pmc0.fr=1 to workaround 
        // Itanium(R) 2 processor errata... setting pmc0.fr=1
        // on Itanium(R) 2 processor can cause a pmi.
        // While pmi is masked via the pmv, pmi requests will
        // be ignored (not latched).
        //
        set_pmv_mask();
        pmc0 = itp_read_reg_pmc(0);
        pmc0w = pmc0;
        pmc0w &= ~(PMC0_OFLOWALL);
        pmc0w |= PMC0_FREEZE;
        itp_write_reg_pmc(0, pmc0w);

        //
        // If "startall" option is set, then set all pmc/pmd regs for EBS.
        // If "startall" is not set, then we are probably restarting sampling
        // after a counter overflow interrupt, and we only want to set the
        // pmd regs for the counter(s) that overflowed.
        //
        if (startall) {
            //
            // Set counters (pmds 4-7) to 0 before setting pmcs 4-7 with pmc.fr=0.
            // This is done to avoid possible pmis but it's not really necessary
            // since we masked pmi via the pmv.
            //
            for (i = 4; i <= 7; i++) {
                itp_write_reg_pmd(i, 0);
            }

            // 
            // Program pmc's with pmc0.fr=0 to workaround Itanium(R) processor errata. 
            // pmc0.fr must be unfrozen when programing pmcs for bus events.
            //
            pmc0w &= ~PMC0_FREEZE;
            itp_write_reg_pmc(0, pmc0w);

            for (i = 0; (i < MAX_PMCS) && (pmcs[i].number != 0);
                 i++) {
                pmc = pmcs[i].number;
                if ((pmc >= 4) && (pmc <= 7)) {
                    itp_write_reg_pmc(pmc,
                               pmcs[i].value.quad_part);
                }
            }

            pmc0w |= PMC0_FREEZE;
            itp_write_reg_pmc(0, pmc0w);

            for (i = 0; (i < MAX_PMCS) && (pmcs[i].number != 0);
                 i++) {
                pmc = pmcs[i].number;
                if ((pmc < 4) || (pmc > 7)) {
                    itp_write_reg_pmc(pmc,
                               pmcs[i].value.quad_part);
                }
            }
        }
        //
        // We unmask after writing pmcs to workaround Itanium(R) 2 processor
        // errata that can caues pmi when writing pmc0.fr=1.
        //
        clear_PMV_mask();
        for (i = 0; (i < MAX_PMDS) && (pmds[i].number != 0); i++) {
            pmd = pmds[i].number;
            if (startall
                || (((pmd >= 4) && (pmd <= 7))
                && (pmc0 & (1 << pmd)))) {
                itp_write_reg_pmd(pmd, pmds[i].value.quad_part);
            }
        }
        pmc0w = itp_read_reg_pmc(0);
        pmc0w &= ~(PMC0_FREEZE | PMC0_OFLOWALL);
        itp_write_reg_pmc(0, pmc0w);
    }

    return;
}

void
samp_stop_emon_itp(void)
{
    __u64 pmr;

    if (sample_method & METHOD_EBS) {
        set_pmv_mask();
        pmr = itp_read_reg_pmc(0);
        pmr |= PMC0_FREEZE;
        itp_write_reg_pmc(0, pmr);
        clear_PMV_mask();
    }

    return;
}

void
samp_start_emon(void * info)
{
    samp_start_emon_itp(FALSE);

    return;
}

void
samp_stop_emon(void)
{
    samp_stop_emon_itp();

    return;
}

void
samp_start_profile_interrupt(void * info)
{
    __u32 processor;    // processor number (0 through MAX_PROCESSORS)

    processor = get_current_processor_number();

    VDK_PRINT_DEBUG("start entered cpu %d ebs_irq %d \n",
		    processor, ebs_irq);
    if ((samp_info.flags & SINFO_STARTED) && (sample_method & METHOD_EBS)
        && (processor < MAX_PROCESSORS) && ebs_irq) {

        if (!xchg(&eachCPU[processor].processor_EBS_status, 1)) {
            VDK_PRINT_DEBUG("starting profile ints cpu %d \n", processor);
            samp_save_set_cpu_vectors();
            init_emon_regs();
            samp_start_ints(TRUE);
        }
    }

    return;
}

void
samp_stop_profile_interrupt(void * info)
{
    __u32 processor;    // processor number (0 through MAX_PROCESSORS)

    processor = get_current_processor_number();

    if (xchg(&eachCPU[processor].processor_EBS_status, 0)) {
        VDK_PRINT_DEBUG("stopping profile ints cpu %d \n", processor);
        samp_stop_ints();
        add_to_counter_total_on_stop();
        init_emon_regs();
        samp_restore_cpu_vectors();
    }

    return;
}

void
driver_load(void)
{
    return;
}

void
driver_open(void)
{
    if (xchg(&init_driver_var, 0)) {
        init_driver();
    }

    return;
}

void
driver_unload(void)
{
#ifdef DSA_SUPPORT_MMAP
  int ret;
  ret = destroy_dsa();
#endif
  pdsa = NULL;

  return;
}

void
samp_start_ints(BOOLEAN startall)
/*++

Routine description:

    This routine sets up the cpu perfomance counter registers for event
    based sampling. At the start of a sampling session, this routine is called
    once on each cpu. When a counter overflow interrupt occurs, this
    routine is called to reset the cpu performance counter registers
    for the counter that overflowed.

Arguments:

return value:

    none

--*/
{
    if (sample_method & METHOD_EBS) {
        samp_start_emon_itp(startall);
    }

    return;
}

void
samp_stop_ints(void)
{
    __u64 pmr;

    if (sample_method & METHOD_EBS) {
        set_pmv_mask();
        pmr = itp_read_reg_pmc(0);
        pmr |= PMC0_FREEZE;
        itp_write_reg_pmc(0, pmr);
        clear_PMV_mask();
    }

    return;
}

void
samp_restore_vectors(void)
/*++

Routine description:

    This routine is restores the IDT and Apic perf vectors
    for each cpu.

Arguments:

return value:

--*/
{

    //
    // restore IDT and Apic perf vectors on each cpu. This
    // is done by installing a cleanup routine which runs
    //
    //

    return;
}

void
samp_save_set_cpu_vectors(void)
/*++

Routine description:

    This routine is restores the IDT and Apic perf vectors
    on the current cpu.
    This routine is called by the Samp_KeUpdatesystemTimeCleanup or
    SampKeUpdateRunTimeCleanup so it is eventually runs on each cpu.

Arguments:

return value:

--*/
{
    __u32 i;

    i = get_current_processor_number();

    //
    //  restore Apic perf vector for current cpu
    //
    if (i < MAX_PROCESSORS) {
        eachCPU[i].original_apic_perf_local_vector = itp_get_pmv();
        itp_write_reg_pmv(PMV_MASK_BIT | ebs_irq);
        itp_srlz_d();
    }

    return;
}

void
samp_restore_cpu_vectors(void)
/*++

Routine description:

    This routine is restores the IDT and Apic perf vectors
    on the current cpu.
    This routine is called by the Samp_KeUpdatesystemTimeCleanup or
    SampKeUpdateRunTimeCleanup so it is eventually runs on each cpu.

Arguments:

return value:

--*/
{
    __u32 i;

    i = get_current_processor_number();

    //
    //  restore Apic perf vector for current cpu
    //
    if (i < MAX_PROCESSORS) {
        VDK_PRINT_DEBUG("restoring pmv cpu %d pmv %p \n",
			i, eachCPU[i].original_apic_perf_local_vector);
        itp_write_reg_pmv(eachCPU[i].original_apic_perf_local_vector);
        itp_srlz_d();
    }
    return;
}

/*++

Routine description:

    get address of next free byte in Sample buffer.

Arguments:

    length = # of bytes to be allocated in sample buf.

return value:

--*/
void_ptr
samp_get_buf_space(__u32 length, u32 *wake_up_thread)
{
    void_ptr buf_ptr;

    buf_ptr = NULL;
    *wake_up_thread = FALSE;

    spin_lock(&sample_int_lock);
    if (samp_info.sampling_active) {
        if ((p_sample_buf >= buf_start) && (p_sample_buf < buf_end)) {
            buf_ptr = p_sample_buf;
            (void_ptr) p_sample_buf += length;
            if (p_sample_buf >= buf_end) {   
                if (p_sample_buf > buf_end) {   // room for sample?
                    p_sample_buf = buf_ptr;     // ..no
                    buf_ptr = NULL;
                }
                samp_info.flags |= SINFO_DO_WRITE;
                samp_info.sampling_active = FALSE;
                *wake_up_thread = TRUE;
            }
            if (buf_ptr) {
                //
                // If max samples taken, then set flag to stop sampling
                //
                if (sample_max_samples) {
                    __u32 i;
                    i = pdsa->sample_count + 1;
                    if (i >= sample_max_samples) {
                        samp_info.flags |= (SINFO_DO_WRITE | SINFO_DO_STOP);
                        samp_info.sampling_active = FALSE;
                        *wake_up_thread = TRUE;
                        if (i > sample_max_samples) {
                            p_sample_buf = buf_ptr;     
                            buf_ptr = NULL;
                        }
                    }
                }
                if (buf_ptr) {
                    pdsa->sample_count++;  
                    samp_info.sample_count = pdsa->sample_count;
                }
            }
        }
    }
    spin_unlock(&sample_int_lock);

    return (buf_ptr);
}

/*
**********************************************************************************************************

                S A M P L I N G    C O N F I G U R A T I O N    F U N C T I O N S  

**********************************************************************************************************
*/

/*
 *
 *  Function: validate_samp_parm6 
 *
 *  description: 
 *  Check to make sure the size of the samp_parm6 buffer is reasonable.
 *
 *  Parms:
 *      entry:      samp_parm6
 *                  length
 *  
 *      return:     0... samp_par6 length Ok
 *
 */
int
validate_samp_parm6(samp_parm6 * sp6, int sp6_len)
{
    int status = 0, ers_num, ers_len;

    if (sp6_len < sizeof (samp_parm6)) {
        VDK_PRINT_ERROR("Invalid samp_parm6 configuration\n");
        status = -EINVAL;
        return (status);
    }

    ers_num = sp6->num_event_reg_set;
    if (ers_num) {
        ers_len = (ers_num - 1) * sizeof(event_reg_set_ex); // subtract 1 for event_reg_set_ex that is included in samp_par6 structure
        if (sp6_len < (sizeof(samp_parm6) + ers_len)) {
            status = -EINVAL;
        }
    }

    return (status);
}

void
get_options_error(__u32 num_event_reg_set,  // number of entries in event_reg_set array
        event_reg_set_ex * ers  // event_reg_set array
    )
{
    __u32 i;

    for (i = 0; i < num_event_reg_set; i++) {
        VDK_PRINT_DEBUG("eventregSet %d cmd 0x%x \n",
			num_event_reg_set, ers[i].counter_number);
    }
    return;
}

//
// Setup PMCs and PMDs for sampling session             
//

BOOLEAN
get_options_from_event_reg_set(__u32 num_event_reg_set, // number of entries in event_reg_set array
              event_reg_set_ex * ers    // event_reg_set array
    )
{
    __u32 ersx, pmcx, pmdx, osex, counter;
    ULARGE_INTEGER pmc_val, pmd_val;

    //
    //  Init PMC and PMD regs for sampling session
    //

    memset((char *) &pmcs, 0, sizeof (pmcs));
    memset((char *) &pmds, 0, sizeof (pmds));
    memset((char *) &OS_events, 0, sizeof (OS_events));
    memset((char *) &counter_options, 0, sizeof (counter_options));
    memset((char *) &event_Ids, 0, sizeof (event_Ids));
    valid_counters = 0;
    sample_options = 0;

    //
    // Treat counter_number of 0 as default counter  (PMC4/PMD4)
    //

    VDK_PRINT_DEBUG("num_event_reg_set %d  event_reg_set addr %p \n",
		    num_event_reg_set, ers);

    if ((ers[0].counter_number == 0) && (ers[0].esr_value != 0)) {
        ers[0].counter_number = 4;
        if (num_event_reg_set == 0) {
            num_event_reg_set = 1;
        }
    }
    //
    // get PMC and PMD regs from event_reg_set
    //

    for (ersx = 0, pmcx = 0, pmdx = 0, osex = 0; ersx < num_event_reg_set;
         ++ersx) {

        pmc_val.quad_part = 0;
        pmd_val.quad_part = 0;

        switch (ers[ersx].command) {
        case ERS_CMD_SET_CONFIG_AND_COUNTER_REGS:
            counter = ers[ersx].counter_number;
            pmc_val.low_part = ers[ersx].esr_value;
            pmd_val = ers[ersx].esr_count;
            break;
        case ERS_CMD_WRITE_PMC:
            counter = ers[ersx].esr_value;
            pmc_val = ers[ersx].esr_count;
            break;
        case ERS_CMD_TBS_VTD:
            VDK_PRINT_DEBUG("ERS_CMD_TBS_VTD... dsa.method_VTD %d \n",
			    (pdsa->method_VTD) ? 1 : 0);
            if (pdsa->method_VTD) { // 08-18-00
                if (osex < MAX_OS_EVENTS) {
                    VDK_PRINT_DEBUG("profileTime set\n");
                    OS_events[osex] = profile_time;
                    osex++;
                    continue;
                }
            }
            get_options_error(num_event_reg_set, ers);
            return FALSE;
        case ERS_CMD_OS_EVENT:
            /*
               if (pdsa->method_OS_events) {  // 08-18-00
               if (osex < MAX_OS_EVENTS) {
               OS_events[osex] = ers[ersx].esr_value;
               osex++;
               continue;
               }
               }
             */
            get_options_error(num_event_reg_set, ers);
            return FALSE;
        case ERS_CMD_NOP:
            continue;
        default:
            get_options_error(num_event_reg_set, ers);
            return FALSE;
        }

        //
        // get PMC/PMD combo for generic PMCs (PMC/PMD 4-7)
        //

        if ((counter >= 4) && (counter <= 7)) {
            if (pmcx == MAX_PMCS) {
                get_options_error(num_event_reg_set, ers);
                return FALSE;
            }
            if (pmdx == MAX_PMDS) {
                get_options_error(num_event_reg_set, ers);
                return FALSE;
            }
            pmcs[pmcx].number = counter;
            pmcs[pmcx].value = pmc_val;
            pmds[pmdx].number = counter;
            pmds[pmdx].event_inc = pmd_val;
            if (!pmd_val.quad_part) {
                pmds[pmdx].event_inc.quad_part =
                    max_counter.quad_part;
            }
            if (pmd_val.quad_part) {
                pmds[pmdx].value.quad_part =
                    ~pmd_val.quad_part + 1;
                pmds[pmdx].value.quad_part &=
                    max_counter.quad_part;
            }
            if ((pmcs[pmcx].number == 4)
                && (cpu_family == ITP_CPU_FAMILY_ITANIUM2)) {
                pmcs[pmcx].value.low_part |= (1 << 23); // set PMC4 bit 23 to enable performance monitoring HW on Itanium(R) 2 processor
            }
            {
                __u64 event_code;

                event_code =
                    (pmc_val.quad_part & pmc_es_mask) >> pmc_es_shift;

                if (event_code == dear_event_code) {
                    counter_options[counter - 4] |=
                        COUNTER_OPTS_COLLECT_DEAR_REGS;
                    sample_options |= SAMPLE_OPTS_DEAR;
                }

                if (event_code == iear_event_code) {
                    counter_options[counter - 4] |=
                        COUNTER_OPTS_COLLECT_IEAR_REGS;
                    sample_options |= SAMPLE_OPTS_IEAR;
                }

                if (event_code == btrace_event_code) {
                    counter_options[counter - 4] |=
                        COUNTER_OPTS_COLLECT_BTRACE_REGS;
                    sample_options |= SAMPLE_OPTS_BTRACE;
                }
            }
            event_Ids[counter - 4] = ers[ersx].event_ID;
            valid_counters |= 1 << (counter);
            pmcx++;
            pmdx++;
            continue;
        }
        //
        // Check for PMC 8-13/15 ... Opcode matching (pmc8 and 9), IEAR (pmc10), DEAR (pmc11), and Branch Trace configurtaion 
        //
        // Note: pmc 8 and 9 added 10-22-00 to support opcode matching 
        //
        if ((counter >= 8) && (counter <= max_config_pmc)) {
            if (pmcx == MAX_PMCS) {
                get_options_error(num_event_reg_set, ers);
                return FALSE;
            }

            pmcs[pmcx].number = counter;
            pmcs[pmcx].value = pmc_val;
            pmcx++;
            continue;
        }
        get_options_error(num_event_reg_set, ers);
        return FALSE;
    }

    return TRUE;
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          int samp_configure6(samp_parm6 *sp, int sp6_len)
 * @brief       Configure sampling based on samp_parm6 structure
 *
 * @param       sp      IN - configuration information
 * @param       sp6_len IN - length of config structure
 *
 * @return	success/error status
 *
 * Does a small amount of configuration. Sets some pointers
 * in the sp6 structure, sets some global variables indicating the
 * sampling method, gets the event set and sets the size of the
 * sampling record based on what events are requested.
 *
 * <I>Special Notes:</I>
 *
 *	Implies VTD works (TBS), but it isn't actually supported.
 *
 * @todo enable VTD for real...
 */
int
samp_configure6(samp_parm6 * sp6, int sp6_len)
{
    __u32 i;

    if (samp_info.flags & (SINFO_STARTED | SINFO_STOPPING | SINFO_WRITE)) {
        return (STATUS_DEVICE_BUSY);
    }

    if (!sp6 || validate_samp_parm6(sp6, sp6_len)) {
        return (STATUS_INVALID_PARAMETER);
    }


    if (sp6->ptrs_are_offsets) {
        sp6->module_info_file_name =
            (char *) ((void_ptr) sp6 +
                  (__u64) sp6->module_info_file_name);
        sp6->raw_sample_file_name =
            (char *) ((void_ptr) sp6 + (__u64) sp6->raw_sample_file_name);
        sp6->ptrs_are_offsets = FALSE;
    }

    sample_method = 0;
    for (i = 0; i < sp6->num_event_reg_set; i++) {
        switch (sp6->esr_set[i].command) {
        case ERS_CMD_TBS_VTD:
            sample_rate_us = sp6->esr_set[i].esr_count.low_part;
            sample_method |= METHOD_VTD;
            break;
        case ERS_CMD_SET_CONFIG_AND_COUNTER_REGS:
        case ERS_CMD_WRITE_MSR:
        case ERS_CMD_WRITE_PMC:
            if (!(pdsa->method_EBS)) {
                return (STATUS_INVALID_PARAMETER);
            }
            sample_method |= METHOD_EBS;
            break;
        case ERS_CMD_OS_EVENT:
            /*
               if (!(pdsa->method_OS_events)) {
               return (STATUS_INVALID_PARAMETER);
               }
               sample_method |= METHOD_OSEVENTS;
               break;
             */
            return (STATUS_INVALID_PARAMETER);
        case ERS_CMD_NOP:
            break;
        default:
            return (STATUS_INVALID_PARAMETER);
        }
    }

    //
    // get EBS regs from user parameters                            
    //

    if (!get_options_from_event_reg_set(sp6->num_event_reg_set, &sp6->esr_set[0])) {
        return (STATUS_NOT_SUPPORTED);
    }

    pdsa->sample_rec_length = sizeof(sample_record_PC);
    sample_tsc = FALSE;
    if (sp6->sample_TSC)
    {
        sample_tsc_offset = pdsa->sample_rec_length;
        sample_tsc = TRUE;
        pdsa->sample_rec_length += 8;
    }

    dear_offset = iear_offset = btrace_offset = 0;
    if (sample_options & SAMPLE_OPTS_IEAR)
    {
        iear_offset = pdsa->sample_rec_length;
        pdsa->sample_rec_length += 8*2;
        VDK_PRINT_DEBUG("Instruction EAR support enabled. PC sample record size %d \n", pdsa->sample_rec_length);
    }
    if (sample_options & SAMPLE_OPTS_DEAR)
    {
        dear_offset = pdsa->sample_rec_length;
        pdsa->sample_rec_length += 8*3;
        VDK_PRINT_DEBUG("Data EAR support enabled. PC sample record size %d \n", pdsa->sample_rec_length);
    }
    if (sample_options & SAMPLE_OPTS_BTRACE)
    {
        btrace_offset = pdsa->sample_rec_length;
        pdsa->sample_rec_length += 8*9;
        VDK_PRINT_DEBUG("Branch Event support enabled. PC sample record size %d \n", pdsa->sample_rec_length);
    }
    sample_rec_length = pdsa->sample_rec_length;

    return (STATUS_SUCCESS);
}

void
samp_build_PC_sample(PINT_FRAME int_frame, P_sample_record_PC p_sample)
/*++

Routine description:

    Build a PC sample record containing...
    cs,eip,eflags,code seg descriptor, pid, cpu#

Arguments:

return value:

--*/
{
    KXDESCRIPTOR ucsd;  // unscrambled code segment descriptor
    PKGDTENTRY pcsd;    // IA32 scrambled format code segment descriptor

    p_sample->cpu_and_OS = 0;   // init bit fields
    p_sample->bit_fields2 = 0;

    //
    //  Build PC sample record based on addressing mode at the time
    //  of the profile interrupt.
    //
    //  IPSR.is... 0=Itanium(R) processor, 1=IA32
    //
    if (!(int_frame->ipsr.quad_part & IA64_PSR_IS)) {   // check addressing mode at time of profile int (Itanium(R) instructions or IA32)
        //
        // Build PC sample record for Itanium(R) instructions ...
        //                                                  // save Itanium(R) processor cpu state at time of profile interrupt
        p_sample->iip.quad_part = int_frame->iip.quad_part; // Itanium(R) processor ip...... IIP
        p_sample->ipsr.quad_part = int_frame->ipsr.quad_part;   // Itanium(R) processor psr..... IPSR
        p_sample->itp_pc = TRUE;
    } else {
        //
        // Build IA32 PC sample
        //
        p_sample->eip = int_frame->iip.low_part;    // IA32 eip..... IIP
        p_sample->cs = (__u16) int_frame->seg_cs;   // IA32 cs...... GR17 15:0
        p_sample->eflags = int_frame->E_flags;  // IA32 eflags.. AR24
        //
        //  Store 8 byte scrambled IA32 code segment descriptor (CSD) in the PC sample record.
        //
        //  AR25 = unscrambled CSD
        //  p_sample->csd = scrambled CSD
        //
        ucsd.words.descriptor_words = int_frame->csd;   // IA32 unscrambled code seg descriptor... AR25
        pcsd = (PKGDTENTRY) & p_sample->csd;    // pcsd = address of CSD in PC sample record
        pcsd->limit_low = (__u16) ucsd.words.bits.limit;    // copy unscrambled CSD to scrambled CSD in PC sample record
        pcsd->base_low = (__u16) ucsd.words.bits.base;  // ..
        pcsd->high_word.bits.base_mid = (char) (ucsd.words.bits.base >> 16);    // ..
        pcsd->high_word.bits.type = (__u32) ucsd.words.bits.type;   // ..
        pcsd->high_word.bits.dpl = (__u32) ucsd.words.bits.dpl; // ..
        pcsd->high_word.bits.pres = (__u32) ucsd.words.bits.pres;   // ..
        pcsd->high_word.bits.sys = (__u32) ucsd.words.bits.sys; // ..
        pcsd->high_word.bits.limit_hi = (__u32) (ucsd.words.bits.limit >> 16);  // ..
        pcsd->high_word.bits.reserved_0 = (__u32) ucsd.words.bits.reserved_0;   // ..
        pcsd->high_word.bits.default_big = (__u32) ucsd.words.bits.default_big; // ..
        pcsd->high_word.bits.granularity = (__u32) ucsd.words.bits.granularity; // ..
        pcsd->high_word.bits.base_hi = (char) (ucsd.words.bits.base >> 28); // ..
    }

    p_sample->cpu_num = (__u16) get_current_processor_number();
    p_sample->pid_rec_index_raw = 1;    // pid contains OS pid
#ifdef ENABLE_TGID
    p_sample->pid_rec_index = get_thread_group_id(current);
#else
    p_sample->pid_rec_index = tbs_get_current_process_id();
#endif
    p_sample->tid = tbs_get_current_thread_id();

    return;
}


/* ------------------------------------------------------------------------- */
/*!
 * @fn          void samp_add_ear_data(P_sample_record_PC p_sample, __u32 counter)
 * @brief       Add ear data (data, instruction, branch) to end of sample record
 *
 * @param       p_sample	IN - address sample record
 * @param       counter	    IN - index of counter that caused interrupt
 *
 * @return	none
 *
 * <I>Special Notes:</I>
 *
 *	Plays with the actual IIP and IPSR to make those returned values
 *  have particular meaning for EAR events. However, we don't
 *  always do it in a uniform manner...
 *
 */
void
samp_add_ear_data(P_sample_record_PC p_sample, __u32 counter)
{
    __u64 *p_data, pmd;
    __u32 i;

    //
    // KLUDGE WARNING:
    //
    // The IIP and IPSR are already captured at this point based
    // on the trap frame. However, for EAR data, the trap frame
    // doesn't have interesting information since the EAR event
    // happened earlier.
    //
    // Alas, the downstram tools are dependant on the IIP and IPSR
    // having information for the particular event. So... This routine
    // modifies the IIP and IPSR to make the downstream code happy. A
    // side effect of this is an inconsistant view from the downstream code...
    // For most events, the IIP does not include the slot information (since
    // the slot information is in the ipsr, not the iip). But, for the data
    // ear event, the code puts the slot information into both the ipsr
    // AND the iip... 
    // 
    // Thus, downstram tools should be looking at the iip and ipsr to get
    // the correct information, but any tool that blindly prints out the iip
    // and uses the ipsr for the slot will show the slot data twice... Tools
    // like sfdump, for example.
    //
    // The reason for not always putting the slot in the iip is so that
    // the driver is providing the actual trap frame iip under normal
    // circumstances.
    //
    // The reason for never putting the slot in the iip is because some
    // tools only look at the iip and to avoid rewriting those tools, the
    // slot is put in the iip for the data ears (which is where it tends
    // to be the most useful due to "skid" for normal events).
    //
    // So, we have the inconsistant situation... A strong argument can be made that
    // the downstram tools should change so that if they care about the slot they
    // should look for it in the right place...
    //

    if (counter_options[counter] & COUNTER_OPTS_COLLECT_IEAR_REGS) {    
        //
        //  Treat Instruction cache line address as IIP
        //  Copy cache line address
        //
        pmd = itp_read_reg_pmd(0);
        p_sample->iip.quad_part = pmd & (~0x1f);    // clear low order bits (bits 4-2 ignored, 1 tlb, 0 v)  05-23-00
        p_sample->ipsr.quad_part &= ~((__u64) 3 << 41); // Itanium(R) processor psr..... IPSR. Clear ri bits in ipsr
        // this is a kludge since the trapframe ipsr is not related
        // to the cache line address (pmd0)
        p_sample->itp_pc = TRUE;

        if (iear_offset) {
            p_data =
                (__u64 *) ((__u64) p_sample + iear_offset);
            p_data[0] = pmd;
            p_data[1] = itp_read_reg_pmd(1);
        }
    }

    if (counter_options[counter] & COUNTER_OPTS_COLLECT_DEAR_REGS) {    
        //
        //  Copy Data EAR Instruction address to IIP field and
        //  treat it as a 64 bit sample
        //
        pmd = itp_read_reg_pmd(17);
        
        //
        // See kludge warning above. This is setting the iip and ipsr
        // to make downstream tools happy...
        //

        //
        // set instruction address but clear slot and v bits
        //
        p_sample->iip.quad_part = pmd & (~0xf);

        //
        // set slot number in instruction address
        // (pmd17 bits 3-2 Itanium(R) processor, bits 1-0 Itanium(R) 2 processor)
        //
        // KLUDGE WARNING:
        // This is DIFFERENT than all other IIP!
        // All other IIP have just bundle information and
        // no slot information, but for data
        // EARs we are also adding the slot into the IIP.
        //
        p_sample->iip.quad_part |= (pmd & (pmd17_slot_mask)) >> pmd17_slot_shift;

        //
        // If the bundle bit is set, we need to bump the
        // instruction address by one bundle (16 bytes)
        //
        if (pmd & pmd17_bundle_mask) {
            p_sample->iip.quad_part += 16;
        }

        //
        // Per the kludge warning above,  the data ear does have an exact
        // bundle and slot but the tools are expecting the bundle to be
        // in iip and the slot to be in psr. The code above put the bundle
        // (plus more) into the iip, the code below is updating the
        // psr that was grabbed in the trapframe to have the correct slot
        // information for the data ear event.
        // NOTE: the rest of the information in the PSR may not be accurate
        // since it was captured at the time of the interrupt, not at
        // the time of the data ear event...
        //

        //
        // Clear the RI bits (slot information)
        //
        p_sample->ipsr.quad_part &= ~ITP_IPSR_RI_MASK;

        //
        // Then put in the slot information we have from
        // the actual data ear event
        //
        p_sample->ipsr.quad_part |= ((pmd & (pmd17_slot_mask)) >>
                                            pmd17_slot_shift) <<
                                            ITP_IPSR_RI_SHIFT;

        p_sample->itp_pc = TRUE;    // treat as 64 bit

        if (dear_offset) {
            p_data =
                (__u64 *) ((__u64) p_sample + dear_offset);
            p_data[0] = itp_read_reg_pmd(2);
            p_data[1] = itp_read_reg_pmd(3);
            p_data[2] = pmd;
        }
    }

    if (counter_options[counter] & COUNTER_OPTS_COLLECT_BTRACE_REGS) {  
        if (btrace_offset) {
            p_data =
                (__u64 *) ((__u64) p_sample + btrace_offset);
            p_data[0] = itp_read_reg_pmd(8);
            p_data[1] = itp_read_reg_pmd(9);
            p_data[2] = itp_read_reg_pmd(10);
            p_data[3] = itp_read_reg_pmd(11);
            p_data[4] = itp_read_reg_pmd(12);
            p_data[5] = itp_read_reg_pmd(13);
            p_data[6] = itp_read_reg_pmd(14);
            p_data[7] = itp_read_reg_pmd(15);
            p_data[8] = itp_read_reg_pmd(16);

            //
            // To make the IIP from the BRANCH_EVENT have more
            // meaning, we are substituting the IIP
            // under certain specific circumstances...
            //
            // 1) Must be on a Itanium(R) 2 processor (or other system with the DS bit)
            // 2) the buffer must have overflowed (this is done to prevent
            //      using bad/stale information and is likely to happen
            //      all the time unless the sampleafter value is unusually
            //      small...
            // 3) The pmc12.ds bit is zero.
            //
            // When all the conditions are met, we search for a valid
            // target address (mp == 1, b ==0) starting with PMD8. For
            // histogram type information, any PMD is as valid as any
            // other (statistically speaking) so we can start with PMD8
            // all the time and not worry about parsing the pmd16 first/last
            // information.
            //
            // Finally, when we copy the PMD on top of the IIP, we do
            // no need to worry about the PMD16.ext bits since we are only
            // working with valid target address. This also means
            // the slot information will always be zero since you can't
            // branch into the middle of a bundle, so we can
            // avoid doing slot fixups and just zero the slot areas...
            //
            // TODO: Revisit this when we have a follow on processor
            // to the Itanium(R) 2 processor ...
            //
            // TODO: Possible changes
            //          use global variable instead of reading
            //          pmc12 (help with PMU latency)
            //
            //          Zero the BTB PMD's (8-15) and skip the
            //          full check to get all data... Probably
            //          not worth the cycles for the rare case this
            //          algorithm would miss...
            //

            if ((ITP_CPU_FAMILY_ITANIUM2 == cpu_family) &&
                        (ITANIUM2_PMD16_FULL_MASK & p_data[8])) {
                pmd = itp_read_reg_pmc(12);
                if (0 == (pmd & ITANIUM2_PMC12_DS_MASK)) {
                    //
                    // See if we have a better choice for the iip
                    // than the current one by looking at the
                    // pmd registers we just saved
                    //
                    for (i = 0; i < ITANIUM2_NUM_BTBREGS; i++) {
                        //
                        // Need branch bit (bit 0) == 0 and Mispredict Bit
                        // (bit 1) == 1 to have a valid target address
                        //
                        if ((p_data[i] & (ITANIUM2_BTB_MP_MASK | ITANIUM2_BTB_B_MASK)) == 0x2) {
                            //
                            // Start with the target address
                            //
                            p_sample->iip.quad_part = p_data[i];

                            //
                            // Strip out the crud at the bottom (slot, mp, b)
                            //
                            p_sample->iip.quad_part &= ~((__u64) ITANIUM2_BTB_SLOT_MASK |
                                    ITANIUM2_BTB_MP_MASK |
                                    ITANIUM2_BTB_B_MASK);

#if defined(DEBUG)
                            //
                            // As noted before, because we are using
                            // only valid target address, the slot bits
                            // should be zero
                            //
                            if (p_data[i] & ITANIUM2_BTB_SLOT_MASK) {
                                VDK_PRINT_WARNING("valid target address had a non-zero slot (0x%p)\n", p_data[i]);
                            }
#endif

                            //
                            // And make sure we update the ipsr to have the
                            // correct slot info as well in case someone
                            // is looking there for information (which
                            // is actually where it is supposed to be)
                            //
                            p_sample->ipsr.quad_part &= ~ITP_IPSR_RI_MASK;

                            break;
                        }
                    }
                }

                //
                // Now that we've looked through the BTB for something
                // interesting, reset PMD16 since the full bit is sticky...
                //
                // TODO: Should we be resetting this all the time
                // after the BRANCH_EVENT interrupt? My initial reaction
                // is yes, but need to think about it further...
                //
                pmd = p_data[8] & ~((__u64) ITANIUM2_PMD16_BBI_MASK | ITANIUM2_PMD16_FULL_MASK);
                itp_write_reg_pmd(16, pmd);
            }
        }
    }

    return;
}

void
samp_build_PC_sample_ex(PINT_FRAME int_frame, P_sample_record_PC p_sample)
/*++

Routine description:

    This routine is called to build a PC sample and save
    it in the sample buffer. A spinlock is used to serialize
    between multiple profile interrupt handlers.

Arguments:

return value:

    none

--*/
{
    __u64 *p_ITC;

    if (p_sample) {
        p_ITC = 0;  
        samp_build_PC_sample(int_frame, p_sample);
        if (sample_tsc) {
            p_ITC = (__u64 *) ((__u32_PTR) p_sample + sample_tsc_offset);  
            *p_ITC = itp_get_itc();   
        }
    }

    return;
}

u32
samp_emon_interrupt(PINT_FRAME int_frame)
/*++

Routine description:

    Handle cpu counter overflow interrupt. This routine is called
    with the SamplLock held and cpu emon counters stopped.

Arguments:

return value:

    none

--*/
{
    __u32 i, events = 0, wake_up_thread = FALSE;
    P_sample_record_PC p_sample, p_sample2;
    ULARGE_INTEGER pmc0;
    __u32 my_cpu = get_current_processor_number();

    if (my_cpu >= MAX_PROCESSORS)  // ignore if sample is on a CPU larger than MAX_PROCESSORS
    {
      return FALSE;
    }

    pmc0.quad_part = itp_read_reg_pmc(0);   // read counter overflow status register
    events = ((pmc0.low_part & valid_counters) >> 4) & 0xf;

    add_to_counter_total_on_overflow(pmc0, my_cpu);

    // samp_get_sample_lock();

    if (check_pause_mode()) {
        sample_skipped();
        events = 0;
    }

    p_sample = 0;
    if (events) {
        p_sample = samp_get_buf_space(sample_rec_length, &wake_up_thread);
    }
    if (p_sample) {
        samp_build_PC_sample_ex(int_frame, p_sample);
        for (i = 0; i < MAX_ACTIVE_EVENTS; i++, events = events >> 1) {
            if (events & 1) {
                break;
            }
        }
        p_sample->event_index = event_Ids[i];
        if (sample_options && (counter_options[i] & COUNTER_OPTS_EAR)) {
            samp_add_ear_data(p_sample, i);
        }
        events = events >> 1;
        i++;

        for (; events && (i < MAX_ACTIVE_EVENTS);
             i++, events = events >> 1) {
            if (events & 1) {
                p_sample2 = samp_get_buf_space(sample_rec_length, &wake_up_thread);
                if (p_sample2) {
                    memcpy(p_sample2, p_sample,
                           sample_rec_length);
                    p_sample2->event_index = event_Ids[i];
                    if (counter_options[i] &
                        COUNTER_OPTS_EAR) {
                        samp_add_ear_data(p_sample2, i);
                    }
                } else {
                    break;
                }
            }
        }

    }

    return(wake_up_thread);
}
