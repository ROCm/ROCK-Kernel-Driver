/*
 *  vtlib32.c
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
 *	File: vtlib32.c
 *
 *	Description: functions for manipulating the hardware registers on
 *	             IA32 platforms
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>     /*malloc */

#include "vtuneshared.h"
#include "apic.h"
#include "vtdef.h"
#include "vtproto.h"
#include "vtextern.h"
#include <asm/io.h>
#ifdef KERNEL_26X
#include <linux/sched.h>
#include <asm/current.h>
#include <linux/pid.h>
#endif
#include <asm/msr.h>
#include <asm/desc.h>
#include <asm/segment.h>

#include "vtasm32.h"
#include "vtlibcommon32.h"
// Note: 07-17-00
// CJ does not want driver to init all emon regs
// at the start of a sampling session.
// See init_emon_regs varilable below.
//

//
// Data to save/restore Cpu event monitoring registers          // 05-31-00
// accross an EBS session
//


__u32 OS_base = 0xC0000000; // starting address of OS ring0 code 05-31-00

__u16 ring0cs = 0x10;       // standard ring0 code segment
__u16 ring3cs = 0x23;       // standard ring3 code segment
#define EFLAGS_V86_MASK       0x00020000L

#if defined(ALLOW_LBRS)
extern BOOLEAN	capture_lbrs ;	// should we capture LBRs on this run?
#endif

extern BOOLEAN pebs_option;
extern __u32 pebs_err;


/*++
Routine description:

  Arguments:

return value:
    TRUE... DTES buffer setup for PEBS

--*/
PDTS_BUFFER
PEBS_alloc_DTES_buf(PDTS_BUFFER p_DTES_buf)
{

    __u32 PEBS_base, lost_size = 0, record_size, buf_size;
    PDTS_BUFFER p_DTS_buffer;

    buf_size = sizeof (DTS_BUFFER) + (PEBS_RECORD_SIZE * 2);    // one PEBS record... need 2 records so that
    // threshold can be less than absolute max
    buf_size += 32;     // allow for allingment of PEBS base to cache line

    if (p_DTES_buf) {
        p_DTS_buffer = p_DTES_buf;
    } else {
        p_DTS_buffer = allocate_pool(non_paged_pool, buf_size);
    }

    if (!p_DTS_buffer) {
        return p_DTS_buffer;
    }

    record_size = PEBS_RECORD_SIZE;

    memset(p_DTS_buffer, 0, buf_size);

    // Let us move down to actual place where the data buffer
    // begins.

    PEBS_base = (__u32) p_DTS_buffer + sizeof (DTS_BUFFER); //Add of data buffer

    //Check for 32 byte  alignment here
    if ((PEBS_base & 0x000001F) != 0x0) {
        lost_size = PEBS_base % 32; //gets the records to be cut to make it aligned
        PEBS_base += lost_size;
    }
    //
    // Program the DTES buffer for precise EBS. Set PEBS buffer for one PEBS record
    //

    p_DTS_buffer->base = 0;
    p_DTS_buffer->index = 0;
    p_DTS_buffer->max = 0;
    p_DTS_buffer->threshold = 0;
    p_DTS_buffer->PEBS_base = PEBS_base;
    p_DTS_buffer->PEBS_index = p_DTS_buffer->PEBS_base;
    p_DTS_buffer->PEBS_max = p_DTS_buffer->PEBS_base + (record_size * 2);
    p_DTS_buffer->PEBS_threshold = p_DTS_buffer->PEBS_max - record_size;

    VDK_PRINT_DEBUG("DTES buf allocated for precise EBS. DTES buf %p\n", p_DTS_buffer);

    return (p_DTS_buffer);
}


/* ------------------------------------------------------------------------- */
/*!
 * @fn void samp_build_csip_sample(PINT_FRAME int_frame, P_sample_record_PC p_sample)
 * @brief       Actualy files the sample buffer with values
 *
 * @param       int_frame IN  - small interrupt frame with minimal values
 * @param       p_sample  OUT - Buffer to be filled
 *
 * @return	none
 *

 * This routine actually fills in the sample record. In the case of PEBS,
 * it can also grab the PEBS entry and use that in place of the eip. This
 * routine also samples the LBRs if requested and appends them to the
 * end of the sample record.
 *
 * <I>Special Notes:</I>
 *
 *	The MSR's to use to get the LBRs is hard coded. Works for current
 * Pentium(R) 4 processor, but likely to be a problem in the future..
 *
 * @todo Update the LBR code to use a lookup array instead of hardcoded MSRs
 *
 */
void
samp_build_csip_sample(PINT_FRAME int_frame, P_sample_record_PC p_sample)
{
#if defined(ALLOW_LBRS)
    ULARGE_INTEGER *nextOpenData;
#endif

    __u32 csdlo;        // low  half code seg descriptor
    __u32 csdhi;        // high half code seg descriptor
    __u32 seg_cs;       // code seg selector

#ifdef USE_NMI
    // temporarily disable if event is precise
    if (pebs_option)
      return;
#endif

    p_sample->cs = (__u16) int_frame->seg_cs;
    p_sample->eip = int_frame->eip;
    p_sample->eflags = int_frame->E_flags;
    p_sample->cpu_and_OS = 0;   // init bit fields
    p_sample->cpu_num = (__u16) smp_processor_id();
    p_sample->bit_fields2 = 0;
    p_sample->pid_rec_index_raw = 1;    // pid contains OS pid
#ifdef ENABLE_TGID
    p_sample->pid_rec_index = get_thread_group_id(current);
#else
    p_sample->pid_rec_index = current->pid;
#endif
    p_sample->tid = current->pid;

    //
    // If Pentium(R) 4 processor precise EBS, get eflags 
    // and ip from Pentium(R) 4 processor DTES buf.
    //
    // !!! NOTE !!!
    // There is a potential problem here.
    /// @todo is there still a problem here?
    //
    if (pebs_option) {
        PDTS_BUFFER p_DTES;
        PPEBS_RECORD p_rec;
        __u32 cpu;

        cpu = p_sample->cpu_num;
        if (cpu < MAX_PROCESSORS) {
            p_DTES = eachCPU[cpu].DTES_buf;
            if (p_DTES) {
                if (p_DTES->PEBS_index != p_DTES->PEBS_base) {
                    p_rec = (PPEBS_RECORD) p_DTES->PEBS_base;
                    p_sample->eflags = p_rec->E_flags;
                    p_sample->eip = p_rec->linear_IP;
                    //
                    // If stack says V86 mode but PEBS record says protected mode (32 or 16 bit),
                    // then we assume that the EBS event occured in ring 0 code with standard NT ring 0 CS
                    //
                    if (int_frame->E_flags & EFLAGS_V86_MASK) {
                        if (!(p_rec->E_flags & EFLAGS_V86_MASK)) {
                            p_sample->cs = ring0cs;
                        }
                    } else {
                        if (OS_base) {
                            if (p_sample->eip >= OS_base) {
                                p_sample->cs = ring0cs;
                            } else {
                                p_sample->cs = ring3cs;
                            }
                        }
                    }
                    p_DTES->PEBS_index = p_DTES->PEBS_base; // reset index to next PEBS record to base of buffer
                } else {
                    pebs_err++;
                }
            } else {
                pebs_err++;
            }
        } else {
            pebs_err++;
        }
    }
    //
    //  Save current code segment descriptor in cs:ip sample
    //

    ///
    ///  @todo Is the LDT pagable? If so, this could cause a problem on V86 apps
    ///

    if (p_sample->eflags & EFLAGS_V86_MASK) {   // changed to use eflags from sample record     05-31-00 Pentium(R) 4 procesor
        csdlo = 0;
        csdhi = 0;
    } else {

        seg_cs = p_sample->cs;  // changed to use CS from sample record         05-31-00 Pentium(R) 4 processor
        get_CSD(seg_cs, &csdlo, &csdhi);
    }
    p_sample->csd.low_word = csdlo;
    p_sample->csd.high_word = csdhi;

#if defined(ALLOW_LBRS)
    nextOpenData = (ULARGE_INTEGER *) (((char *) p_sample) + sizeof(sample_record_PC));


    if (sample_tsc)
    {
#if defined(DEBUG)
	if (((char *) nextOpenData) != ((char *) p_sample) + sample_tsc_offset) {
	    VDK_PRINT_ERROR("tsc offest has unexpected value (%d, %p, %p).\n", sample_tsc_offset, p_sample, nextOpenData);
	    return;
	}
	
	if (sample_tsc_offset + sizeof(ULARGE_INTEGER) > sample_rec_length) {
	    VDK_PRINT_ERROR("tsc offest (%d) larger than record size (%d)\n", sample_tsc_offset, sample_rec_length);
	    return;
	}
#endif
	rdtsc(nextOpenData->low_part, nextOpenData->high_part);
	nextOpenData++;
    }

#else
    if (sample_tsc)
    {
        u32 *tsc;

        tsc = (void *) p_sample + sample_tsc_offset;
        rdtsc(tsc[0], tsc[1]);
    }
#endif

#if defined(ALLOW_LBRS)
    if (capture_lbrs) {

#if defined(DEBUG)
	{
	    // Makes the if statments below easier...
	    long samp_start, samp_current;
            long samp_available;
	    long samp_needed;

	    samp_start = (long) p_sample;
	    samp_current = (long) nextOpenData;
            samp_available = sample_rec_length - (samp_current - samp_start);

	    // Need to make sure we have space for 4 LBRs plus a TOS indicator
	    samp_needed = LBR_SAVE_SIZE;

	    if (samp_available < samp_needed) {
	        VDK_PRINT_ERROR("sample_rec_length (%d) not big enough to hold samples.\n", 
			sample_rec_length);
	        VDK_PRINT_ERROR("p_sample: 0x%p next: 0x%p, available %ld needed %ld\n",
			p_sample,
			nextOpenData,
			samp_available,
			samp_needed);
	        return;
	    }
	}
#endif
	//
	// For now, the MSR to use is a constant, but it is not
	// architectural so the HW people could change this.
	// BEWARE!
	// Longer term, these values should be in an array so
	// you can choose what to grab (and how many) based on the
	// processor.
	//
	*nextOpenData = samp_read_msr(MSR_LASTBRANCH_TOS);
	nextOpenData++;

	*nextOpenData = samp_read_msr(MSR_LASTBRANCH_0);
	nextOpenData++;
	*nextOpenData = samp_read_msr(MSR_LASTBRANCH_1);
	nextOpenData++;
	*nextOpenData = samp_read_msr(MSR_LASTBRANCH_2);
	nextOpenData++;
	*nextOpenData = samp_read_msr(MSR_LASTBRANCH_3);
    }
#endif
    
    return;
}


/*
**********************************************************************************************************

    START/STOP EBS FUNCTIONS

**********************************************************************************************************
*/

void
samp_start_ints(void)
{
    __u32 cpu;
    ULARGE_INTEGER val;
    PDTS_BUFFER our_DTES_buf;

#ifdef USE_NMI
    // temporarily disable if event is precise
    if (pebs_option)
      return;
#endif

    if (sample_method & METHOD_EBS) {
        cpu = smp_processor_id();
        if (cpu < MAX_PROCESSORS) {
            // InterlockedIncrement(&processor_EBS_status[cpu]);
            interlocked_exchange(&eachCPU[cpu].processor_EBS_status, 1);
            if (eachCPU[cpu].original_EBS_idt_entry == 0) {
#ifndef USE_NMI
                samp_get_set_idt_entry(PERF_MON_VECTOR, (__u32)
                               eachCPU[cpu].samp_EBS_idt_routine, &eachCPU[cpu].original_EBS_idt_entry);
#endif
                eachCPU[cpu].original_apic_perf_local_vector = SetApicPerfLevel(apic_perf_lvt); 

                //
                // If Pentium(R) 4 processor precise EBS is active, then allocate a DTES buffer
                // and install it for current logical processor (HW thread)
                //
                if (pebs_option) {  // 05-31-00 Pentium(R) 4 processor
                    eachCPU[cpu].DTES_buf = PEBS_alloc_DTES_buf(eachCPU[cpu].DTES_buf);
                    our_DTES_buf = eachCPU[cpu].DTES_buf;
                    //
                    // If we could not get a DTES buffer, return without starting
                    // EBS ints for this cpu.
                    // NEED TO FIX!! if this happens, we should stop sampling and report
                    // an error.
                    //
                    if (!our_DTES_buf) {
                        VDK_PRINT("unable to allocate DTES buffer for cpu %d\n", cpu);
                        return;
                    }
                    //
                    // Save original DTES and DBG_CTL regs if we have not
                    // already saved them
                    //
                    val = samp_read_msr(WMT_CR_DTES_AREA);
                    if (val.low_part != (__u32) our_DTES_buf) {
                        eachCPU[cpu].org_DTES_area_msr_val = val;
                        eachCPU[cpu].org_dbg_ctl_msr_val = samp_read_msr(DEBUG_CTL_MSR);
                    }
                    //
                    // Disable PEBS and DTS for current thread and install our DTES buf for PEBS
                    //
                    samp_disable_PEB_sand_DTS();
                    val.high_part = 0;
                    val.low_part = (__u32) our_DTES_buf;
                    samp_write_msr(WMT_CR_DTES_AREA, val);  // install our DTES bufer on current thread
                }
            }
            eachCPU[cpu].start_all = TRUE;
        }
        samp_start_emon(NULL);  // start EBS on current cpu
    }

    return;
}


/*++

Routine description:

    This routine is restores the IDT and Apic perf vectors
    on the current cpu.
    This routine is called by the Samp_KeUpdatesystemTimeCleanup or
    SampKeUpdateRunTimeCleanup so it is eventually runs on each cpu.

Arguments:

return value:

--*/
void
samp_restore_cpu_vectors(void)
{
    __u32 i;

    i = smp_processor_id();

    //
    //  restore Apic perf vector for current cpu
    //
    if (eachCPU[i].original_apic_perf_local_vector) {
        EnablePerfVect(FALSE);
        eachCPU[i].original_apic_perf_local_vector = 0;
    }
    //
    //  restore IDT vector for current cpu
    //
    if (eachCPU[i].original_EBS_idt_entry) {
        samp_restore_idt_entry(PERF_MON_VECTOR, &eachCPU[i].original_EBS_idt_entry);
        eachCPU[i].original_EBS_idt_entry = 0;
    }

    return;
}
