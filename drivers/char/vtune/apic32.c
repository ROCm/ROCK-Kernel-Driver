/*
 *  apic32.c
 *
 *  Copyright (C) 2003-2004 Intel Corporation
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
 *	File: apic32.c
 *
 *	Description: handle APIC manipulations done by sampling driver
 *
 *	Author(s): Charles Spirakis, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>     /*malloc */
#include <asm/io.h>
#include <asm/desc.h>

#include "vtuneshared.h"
#include "vtdef.h"
#include "vtasm32.h"
#include "vtproto.h"
#include "vtextern.h"
#include "apic.h"

#define APIC_BASE_PHYS_ADDR         (0xfffff000)


/*!
 * @fn          void *map_apic(void)
 * @brief       Map the local apic registers into virtual memory
 *
 * @return	non-zero - virtual address of the apic, zero - apic not mapped
 *
 *
 * Local apic is only available on Pentium(R) Pro processor and beyond.
 *
 * <I>Special Notes:</I>
 *
 *	Some laptops using newer processors don't include the apic. This
 * code could have problems on laptop machines...
 *
 * @todo	Need a better test for apic existance rather than cpu family
 *
 */
void *
map_apic(void)
{
    __u32 msr_low, msr_high;

    if (!IA32_family6 && !IA32_familyF) {
        return ((void *) 0);
    }
    rdmsr(APIC_BASE_MSR, msr_low, msr_high);

    return ((void *) ioremap_nocache(msr_low & 0xFFFFF000, 4096));
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void un_map_apic(void *apic_virt_addr)
 * @brief       Unmap the local apic from virtual memory
 *
 * @param       apic_virt_addr	IN - address of be unmapped
 *
 * @return	none
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 */
void
un_map_apic(void *apic_virt_addr)
{
    if (apic_virt_addr) {
        iounmap(apic_virt_addr);
    }

    return;
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          BOOLEAN enable_apic(void) 
 * @brief       Enables the apic if it exists and is not already enabled
 *
 * @return	true if apic was enabled, false otherwise
 *
 * Calls an assembly routine to do the actual work. If the apic isn't
 * enabled, the assembly routine enables it and puts it into
 * "virtual wire mode" which bascially makes the apic emulate
 * the older 8249 interrupt structure but still allow for the processor
 * to use processor specific interrupts (like the PMU overflow).
 *
 * <I>Special Notes:</I>
 *
 *	Same apic existance problem as map_apic()
 *
 * @todo        Same apic existance problem as map_apic()
 *
 */
BOOLEAN
enable_apic(void)
{
    __u32 msr_low, msr_high;

    if (!IA32_family6 && !IA32_familyF) {
        return (FALSE);
    }

    SAMP_Set_Apic_Virtual_Wire_Mode();
    rdmsr(APIC_BASE_MSR, msr_low, msr_high);

    return ((msr_low & APIC_BASE_MSR_ENABLE) ? TRUE : FALSE);
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void DoApicInit (void)
 * @brief       Check for and initialize the APIC if necessary
 *
 * @return      none
 *
 * <I>Special Notes:</I>
 *
 *          none
 *
 */
void
DoApicInit(
        void)
{
    ULARGE_INTEGER msrValue;

    BOOLEAN apic_enabled = FALSE;
    void *apicPhysAddr;
    
    apic_local_addr = map_apic();
    apic_enabled = enable_apic();

    if (!apic_enabled) {
        un_map_apic(apic_local_addr);
        apic_local_addr = 0;
    }
    
    msrValue = samp_read_msr(IA32_APIC_BASE);
    apicPhysAddr = (void *) (msrValue.low_part & APIC_BASE_PHYS_ADDR);

    VDK_PRINT_DEBUG("Apic base (msr 0x%x) has 0x%p\n", IA32_APIC_BASE, (void *) msrValue);
    VDK_PRINT_DEBUG("Physical apic base address is 0x%p\n", apicPhysAddr);
    VDK_PRINT_DEBUG("Virtual apic base address is 0x%p\n", (void *) apic_local_addr);

    return;
}

__u32
IsApicEnabled(
        void
        )
{
    ULARGE_INTEGER  msrValue;

    msrValue = samp_read_msr(IA32_APIC_BASE);

    return (msrValue.low_part & APIC_BASE_GLOBAL_ENABLE) ? TRUE : FALSE;
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void EnablePerfVect(__u32 wantEnable)
 * @brief       Re-enable the perf interrupts
 *
 * @param       wantEnable  IN  - TRUE to enable, FALSE to disable
 *
 * @return      none
 *
 * Mask or unmask the perf interrupt through the local vector table (lvt)
 *
 * <I>Special Notes:</I>
 *
 *          none
 *
 */
void
EnablePerfVect(
        __u32 wantEnable
        )
{
    if (wantEnable) {
        samp_apic_clear_perf_lvt_int_mask();       
    }
    else {
        samp_apic_set_perf_lvt_int_mask();
    }

    return;
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          __u32 SetApicPerfLevel(__u32 newValue)
 * @brief       Set the new LVT for the perf counters
 *
 * @param       newValue - the new value to be used
 *
 * @return      the value that was there before
 *
 * Put in a new value in the APIC LVT entry for the performance counter
 *
 * <I>Special Notes:</I>
 *
 *          none
 *
 */
__u32
SetApicPerfLevel(__u32 newValue)
{
    return SAMP_Set_apic_perf_lvt(newValue);
}
