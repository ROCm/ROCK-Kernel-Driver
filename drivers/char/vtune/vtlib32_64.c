/*
 *  vtlib32_64.c
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
 *	File: vtlib32_64.c
 *
 *	Description: functions for manipulating hardware registers on
 *	             Pentium(R) 4 processors with 
 *                   Intel(R) Extended Memory 64 Technology
 *
 *	Author(s): George Artz, Intel Corp.
 *	           Charles Spirakis, Intel Corp.
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
#include <asm/msr.h>
#include <asm/desc.h>
#include <asm/segment.h>

#include "vtasm32_64.h"

#include "vtlibcommon32.h"

//
// Seems like these should be in OS include files somewhere, but
// didn't see them...
//
#define IS_LDT_BIT      0x4
#define SEGMENT_SHIFT   3

#define EFLAGS_V86_MASK       0x00020000L


#if defined(ALLOW_LBRS)
extern BOOLEAN	capture_lbrs;	// should we capture LBRs on this run?
#endif

extern BOOLEAN pebs_option; // precise event sampling active    05-31-00
extern __u32 pebs_err;      // error during precise EBS ISR     05-31-00


/*++
Routine description:

  Arguments:

return value:
    TRUE... DTES buffer setup for PEBS

--*/
PIA32EM64T_DTS_BUFFER
PEBS_alloc_DTES_buf(PIA32EM64T_DTS_BUFFER p_DTES_buf)
{
    __u32 record_size;
    __u32 buf_size;
    __u64 PEBS_base;
    PIA32EM64T_DTS_BUFFER p_DTS_buffer;

    //
    // event though we only want one PEBS record... need 2 records so that
    // threshold can be less than absolute max
    //
    buf_size = sizeof (IA32EM64T_DTS_BUFFER) + (sizeof(IA32EM64T_PEBS_RECORD)* 2);

    buf_size += 32;     // allow for allingment of PEBS base to cache line

    if (p_DTES_buf) {
        p_DTS_buffer = p_DTES_buf;
    } else {
        p_DTS_buffer = allocate_pool(non_paged_pool, buf_size);
    }

    if (!p_DTS_buffer) {
        return p_DTS_buffer;
    }

    memset(p_DTS_buffer, 0, buf_size);

    //
    // The PEBS will start after the DTS area
    //
    PEBS_base = ((__u64) p_DTS_buffer) + sizeof(IA32EM64T_DTS_BUFFER);

    //
    // Force 32 byte alignment here
    //
    PEBS_base = (PEBS_base + 31) & ~((__u64) 0x1f);


    record_size = sizeof(IA32EM64T_PEBS_RECORD);

    //
    // Program the DTES buffer for precise EBS.
    // Set PEBS buffer for one PEBS record...
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
    ULARGE_INTEGER *nextOpenData;
    __u32 is_64bit_addr;
    __u8 tmp;

    p_sample->cpu_and_OS = 0;   // init bit fields
    p_sample->cpu_num = (__u16) smp_processor_id();
    p_sample->bit_fields2 = 0;
    p_sample->pid_rec_index_raw = 1;    // pid contains OS pid
    p_sample->cs = (__u16) int_frame->seg_cs;

    get_CSD(p_sample->cs, &p_sample->csd.low_word, &p_sample->csd.high_word);

    //save off the dpl
    tmp =p_sample->csd.dpl;
    //
    // The reserved_0 bit == 1 means we have a 64-bit descriptor
    // based on the cs of the trap frame we just received...
    //
    is_64bit_addr = (1 == p_sample->csd.reserved_0);

    if (is_64bit_addr) {
        p_sample->iip = int_frame->iip;
        p_sample->ipsr = int_frame->ipsr;
	//BSBS this is a complete hack.  we are sticking the cpl in bits
	//32 and 33 of the ipsr because the user mode code is expecting it
	//there and currently RFLAGS(extended EFLAGS) is only 32 bits (upper
	//32 bits all set to 0)
	p_sample->ipsr.high_part =  tmp;
        p_sample->itp_pc = TRUE;
    }
    else {
        p_sample->eip = (__u32) (int_frame->iip.quad_part & 0xffffffff);
        p_sample->eflags = (__u32) (int_frame->ipsr.quad_part & 0xffffffff);
        p_sample->itp_pc = FALSE;

        //
        // CSS: TODO: Do we care about V86 mode? If so, do we need to do
        // more here?  My initial reading of the manual says V86 mode isn't
        // supported under "long" os'es... Is that true? Need to verify...
        //
        if (p_sample->eflags & EFLAGS_V86_MASK) {
            VDK_PRINT_ERROR("Unexpected V86 mode sample captured!\n");
        }
    }

#ifdef ENABLE_TGID
    p_sample->pid_rec_index = get_thread_group_id(current);
#else
    p_sample->pid_rec_index = current->pid;
#endif

    p_sample->tid = current->pid;

    //
    // If precise EBS, overwrite the eflags and ip usig the ones from DTES buf.
    //
    // NOTE:
    // There is no indication in the PEBS buffer of the CS used
    // at the time of the sample. So, we need to assume that the PEBS
    // sample and the current interrupt frame are "close" in time -
    // close enough, in fact, to use the CS from the current interrupt
    // to determine things like 32-bit vs. 64-bit mode...
    //
    // If that assumption is false, the data can be misattributed (at
    // least mode wise)
    //
    //
    if (pebs_option) {
        PIA32EM64T_DTS_BUFFER p_DTES;
        PIA32EM64T_PEBS_RECORD p_rec;
        __u32 cpu;

        cpu = p_sample->cpu_num;
        if (cpu < MAX_PROCESSORS) {
            p_DTES = eachCPU[cpu].DTES_buf;
            if (p_DTES) {
                if (p_DTES->PEBS_index != p_DTES->PEBS_base) {
                    p_rec = (PIA32EM64T_PEBS_RECORD) p_DTES->PEBS_base;
                    //
                    // Rest of 64/32 bit information should have
                    // been done above when we were assuming we weren't
                    // pebs...
                    //
                    if (is_64bit_addr) {
                        p_sample->iip.quad_part = p_rec->linear_IP;
                        p_sample->ipsr.quad_part = p_rec->E_flags;
                    }
                    else {
                        p_sample->eip = (__u32) (p_rec->linear_IP & 0xffffffff);
                        p_sample->eflags = (__u32) (p_rec->E_flags & 0xffffffff);
                    }

                    //
                    // reset index to next PEBS record to base of buffer
                    //
                    p_DTES->PEBS_index = p_DTES->PEBS_base;
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

    nextOpenData = (ULARGE_INTEGER *) (((char *) p_sample) + sizeof(sample_record_PC));

    if (sample_tsc) {
#if defined(DEBUG)
        if (((char *) nextOpenData) != ((char *) p_sample) + sample_tsc_offset) {
            VDK_PRINT_ERROR("TSC offest has unexpected value (%d, %p, %p).\n", sample_tsc_offset, p_sample, nextOpenData);
            return;
        }
	
        if (sample_tsc_offset + sizeof(ULARGE_INTEGER) > sample_rec_length) {
            VDK_PRINT_ERROR("TSC offest (%d) larger than record size (%d)\n", sample_tsc_offset, sample_rec_length);
            return;
        }
#endif
        rdtsc(nextOpenData->low_part, nextOpenData->high_part);
        nextOpenData++;
    }

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

    VDK_PRINT_DEBUG("returning sample iip: %p, eflags %p, itp_pc: %d, cs: %x\n",
                (void *) p_sample->iip.quad_part,
                (void *) p_sample->ipsr.quad_part,
                p_sample->itp_pc,
                p_sample->cs);

    return;
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void get_idt_func(__u32 vect, void **func)
 * @brief       Get the function currently associated with an idt vector
 *
 * @param       vect - the idt vector of interest
 * @param       func - where the resulting function address goes
 *
 * @return	none
 *
 * Done this way in case we want to capture the full vector (16-bytes) instead
 * of just the function at some point in the future...
 *
 * <I>Special Notes:</I>
 *
 *      Since we are only reading the idt info, don't bother
 *      blocking interrupts
 *
 */

void
get_idt_func(
    __u32 vect,
    void **func
    )
{
    IDTGDT_DESC idtDesc;
    struct gate_struct oldGate;
    struct gate_struct *idt;

    GetIDTDesc(&idtDesc);

    idt = idtDesc.idtgdt_base;

    copy_16byte(&oldGate, &idt[vect]);

    *func = (void *) ((((__u64) oldGate.offset_high) << 32) | (((__u64) oldGate.offset_middle) << 16) | ((__u64) oldGate.offset_low));

    VDK_PRINT_DEBUG("idt base %p, vector %x has address %p\n", idt, vect, *func);

    return;
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void set_idt_func(__u32 vect, void *func)
 * @brief       Set the function currently associated with an idt vector
 *
 * @param       vect - the idt vector of interest
 * @param       func - function to be called when the interrupt occurs
 *
 * @return	none
 *
 *
 * <I>Special Notes:</I>
 *      None
 *
 */

void
set_idt_func(
    __u32 vect,
    void *func
    )
{
    IDTGDT_DESC idtDesc;
    struct gate_struct *idt;

    GetIDTDesc(&idtDesc);

    idt = idtDesc.idtgdt_base;

    VDK_PRINT_DEBUG("Setting idt %p, vector %x with address %p\n", idt, vect, func);

    _set_gate(&idt[vect], GATE_INTERRUPT, (unsigned long) func, 3, 0);

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
    PIA32EM64T_DTS_BUFFER our_DTES_buf;

    if (sample_method & METHOD_EBS) {
        cpu = smp_processor_id();
        if (cpu < MAX_PROCESSORS) {
            // InterlockedIncrement(&processor_EBS_status[cpu]);
            interlocked_exchange(&eachCPU[cpu].processor_EBS_status, 1);
            if (eachCPU[cpu].original_EBS_idt_entry == 0) {
                //
                // CSS: TODO: When we have a routine that can handle
                // the interrupt, then we write this routine to insert it...
                //
                // CSS: Think this is DONE now unless we want to make it
                // a single routine instead of three calls...
                //
                get_idt_func(PERF_MON_VECTOR,&eachCPU[cpu].original_EBS_idt_entry);
                set_idt_func(PERF_MON_VECTOR, eachCPU[cpu].samp_EBS_idt_routine);
                eachCPU[cpu].original_apic_perf_local_vector = SetApicPerfLevel(apic_perf_lvt);

                //
                // If precise EBS active, then allocate a DTES buffer and
                // install it for current logical processor (HW thread)
                //
                if (pebs_option) {
                    eachCPU[cpu].DTES_buf = PEBS_alloc_DTES_buf(eachCPU[cpu].DTES_buf);
                    our_DTES_buf = eachCPU[cpu].DTES_buf;
                    //
                    // If we could not get a DTES buffer, return without
                    // starting...
                    //
                    // EBS ints for this cpu.
                    // NEED TO FIX!! if this happens, we should stop
                    // sampling and report an error.
                    //
                    if (!our_DTES_buf) {
                        VDK_PRINT_ERROR("unable to allocate DTES buffer for cpu %d\n", cpu);
                        return;
                    }

                    //
                    // Save original DTES and DBG_CTL regs if we have not
                    // already saved them
                    //
                    val = samp_read_msr(WMT_CR_DTES_AREA);
                    if (val.quad_part != (__u64) our_DTES_buf) {
                        eachCPU[cpu].org_DTES_area_msr_val = val;
                        eachCPU[cpu].org_dbg_ctl_msr_val = samp_read_msr(DEBUG_CTL_MSR);
                    }

                    //
                    // Disable PEBS and DTS for current thread
                    // and install our DTES buf for PEBS
                    //
                    samp_disable_PEB_sand_DTS();
                    val.quad_part = (__u64) our_DTES_buf;
                    samp_write_msr(WMT_CR_DTES_AREA, val);
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
    //  restore Apic perf vector for current cpu. We leave perf masked off
    //
    if (eachCPU[i].original_apic_perf_local_vector) {
        EnablePerfVect(FALSE);

        SetApicPerfLevel(eachCPU[i].original_apic_perf_local_vector);

        eachCPU[i].original_apic_perf_local_vector = 0;
    }

    //
    //  restore IDT vector for current cpu
    //
    if (eachCPU[i].original_EBS_idt_entry) {
        //
        // CSS: TODO: When we have a routine that can handle the interrupt
        // and we write the routine to insert that handler in the IDT
        // then we can worry about writing this routine to clean up...
        //
        set_idt_func(PERF_MON_VECTOR, eachCPU[i].original_EBS_idt_entry);
        eachCPU[i].original_EBS_idt_entry = 0;
    }

    return;
}

/*
**********************************************************************************************************

                S A M P L I N G    C O N F I G U R A T I O N    F U N C T I O N S  

**********************************************************************************************************
*/


/* ------------------------------------------------------------------------- */
/*!
 * @fn __u32 get_CSD(__u32 seg, __u32 *low, __u32 *high)
 * @brief       Get the csd for a corresponding segment if available
 *
 * @param       seg IN - the segment
 * @param       low OUT - the low part of the csd
 * @param       high OUT - the high part of the csd
 *
 * @return	TRUE if the seg has a correspond CSD, FALSE for any errors
 *
 * Get the CSD entry for the corresponding segment number. In theory
 * each processor can have its own GDT so, in theory, if we cached this
 * info, we would need to do it per CPU. In reality for linux, it looks
 * like they all point to the same spot...
 *
 * <I>Special Notes:</I>
 *      Executed at interrupt time...
 *
 *
 * @todo Really should save these since they are the same all the time...
 *
 */
__u32
get_CSD(
    __u32 seg,
    __u32 *low,
    __u32 *high
    )
{
    IDTGDT_DESC gdt_desc;
    void *gdt_max_addr;
    struct desc_struct *gdt;
    code_descriptor *csd;

    //
    // These could be pre-init'ed values
    //
    GetGDTDesc(&gdt_desc);
    gdt_max_addr = (void *) (((__u64) gdt_desc.idtgdt_base) + gdt_desc.idtgdt_limit);
    gdt = gdt_desc.idtgdt_base;

    //
    // end pre-init potential...
    //

    //
    // We don't do ldt's
    //
    if (seg & IS_LDT_BIT) {
        *low = 0;
        *high =0;
        return FALSE;
    }

    //
    // segment offset is based on dropping the bottom 3 bits...
    //
    csd = (code_descriptor *) &(gdt[seg >> SEGMENT_SHIFT]);

    if (((void *) csd) >= gdt_max_addr) {
        VDK_PRINT_ERROR("segment too big in get_CSD(%x)\n", seg);
        return FALSE;
    }

    *low = csd->low_word;
    *high = csd->high_word;

    VDK_PRINT_DEBUG("get_CSD - seg %x, low %08x, high %08x, reserved_0: %d\n",
            seg,
            *low,
            *high,
            csd->reserved_0);

    return TRUE;
}


/* ------------------------------------------------------------------------- */
/*!
 * @fn          void ReadTsc(__u64 *pTsc)
 * @brief       Read the tsc store the result
 *
 * @return	none
 *
 * Use the inline assembly from msr.h to do the actual tsc read
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 * @todo Could inline this too...
 *
 */
void
ReadTsc(__u64 *pTsc)
{
    rdtscll(*pTsc);

    return;
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void ReadMsr(__u32 msr, __u64 *pResult)
 * @brief       Read a value from the msr
 *
 * @return	none
 *
 * Use the inline assembly from msr.h to do the actual msr read
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 * @todo Could inline this too...
 *
 */
void
ReadMsr(__u32 msr, __u64 *pResult)
{
    rdmsrl(msr, *pResult);

    return;
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void WriteMsr(__u32 msr, __u64 value)
 * @brief       Write a value to the msr
 *
 * @return	none
 *
 * Use the inline assembly from msr.h to do the actual msr write
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 * @todo Could inline this too...
 *
 */
void
WriteMsr(__u32 msr, __u64 value)
{
    wrmsrl(msr, value);

    return;
}

