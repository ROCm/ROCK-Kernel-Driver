/*
 *  apic32_64.c
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
 *	File: apic32_64.c
 *
 *	Description: handle APIC manipulations done by sampling driver
 *	             on Pentium(R) 4 processors
 *	             with Intel(R) Extended Memory 64 Technology
 *
 *	Author(s): Charles Spirakis, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <asm/processor.h>
#include <asm/fixmap.h>
#include <asm/apic.h>

#include "vtdef.h"
#include "vtypes.h"
#include "vtasm32_64.h"
#include "apic.h"

//
// Linux* already defines the apic virtual address for us at boot time
// and it is the same one each time...
//
#define  apicVirtAddr   (APIC_BASE)

#define APIC_BASE_PHYS_ADDR         (0xfffffffffffff000)

#if defined(DEBUG)
IDTGDT_DESC gIdtDesc;
#endif


/* ------------------------------------------------------------------------- */
/*!
 * @fn          void ShortDelay (IN __u32 wantSerialize)
 * @brief       A small delay loop with optional serialization
 *
 * @param       wantSerialize  IN  - True if you want to serialize at the end
 *
 * @return      none
 *
 * A small delay loop that can also serialize the processor at the end
 *
 * <I>Special Notes:</I>
 * 
 *      Uses the cpuid instruction to do the serialization
 *
 * @todo    This routine is currently untested!
 *
 */
void
ShortDelay(
        __u32 wantSerialize)
{
    volatile char t;
    int i;

    VDK_PRINT_WARNING("You are running UNTESTED CODE: %s line %d\n", __FILE__, __LINE__);

    for (i = 0; i < 10; i++) {
        t = 0;
    }
    if (wantSerialize) {
        // CPUID is a serializing instruction
        cpuid_eax(1);
    }
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void SetVirtualWireMode(void)
 * @brief       Put the APIC in virtual wire mode
 *
 * @return      none
 *
 * This routine assumes the APIC exists.
 *
 * <I>Special Notes:</I>
 * 
 *      none
 *
 * @todo    This routine is currently untested!
 *
 */
void
SetVirtualWireMode(
        void)
{
    unsigned long oldFlags;
    unsigned char pic1Mask;
    unsigned char pic2Mask;
    __u64 apicBaseValue;

    VDK_PRINT_WARNING("You are running UNTESTED CODE: %s line %d\n", __FILE__, __LINE__);

    VDK_PRINT("Setting virtual wire mode\n");

    //
    // Interrupts are DISABLED HERE
    //
    local_irq_save(oldFlags);

    //
    // Mask the 8259's to prevent interrupts from being trapped
    // while switching to virtual wire mode
    //
    pic2Mask = __inbyte(0xa1);      // save old mask
    ShortDelay(FALSE);

    __outbyte(0xa1, 0xff);          // mask off everything
    ShortDelay(FALSE);

    pic1Mask = __inbyte(0x21);      // save old mask
    ShortDelay(FALSE);

    __outbyte(0x21, 0xff);          // mask off everything
    ShortDelay(FALSE);

    //
    // About to RE-ENABLE interrupts HERE
    //
    local_irq_restore(oldFlags);

    //
    // Delay to let any pending interrupts get handled...
    //
    // Shouldn't get many interrupts - only the pending ones since the 
    // 8259's are masked off
    //
    ShortDelay(TRUE);
    ShortDelay(TRUE);

    //
    // Interrupts are DISABLED HERE
    //
    local_irq_save(oldFlags);

    //
    // Turn on the global enable
    //
    ReadMsr(IA32_APIC_BASE, &apicBaseValue);
    apicBaseValue |= APIC_BASE_GLOBAL_ENABLE;
    WriteMsr(IA32_APIC_BASE, apicBaseValue);

    // Enable virtual wire mode
    APIC_WRITE(apicVirtAddr, APIC_SPIV, 0x1ff);

    // Block interrupts (should be done already above, but...)
    APIC_WRITE(apicVirtAddr, APIC_TASKPRI, APIC_VALUE_TPR_HI);

    // local id is 0
    APIC_WRITE(apicVirtAddr, APIC_ID, 0);

    // logical unit id is 0
    APIC_WRITE(apicVirtAddr, APIC_LDR, 0);

    // destination format
    APIC_WRITE(apicVirtAddr, APIC_DFR, -1);

    // mask local timer
    APIC_WRITE(apicVirtAddr, APIC_LVTT, APIC_LVT_MASKED);

    // 8259->lint0 - edge sensitive
    APIC_WRITE(apicVirtAddr, APIC_LVT0, SET_APIC_DELIVERY_MODE(0, APIC_MODE_EXINT));

    // nmi->lint1 - level sensitive
    APIC_WRITE(apicVirtAddr, APIC_LVT1, SET_APIC_DELIVERY_MODE(APIC_LVT_LEVEL_TRIGGER, APIC_MODE_NMI));

    // Set PMU interrupt priority
    APIC_WRITE(apicVirtAddr, APIC_LVTPC, PERF_MON_VECTOR);

    // Allow interrupts through apic
    APIC_WRITE(apicVirtAddr, APIC_TASKPRI, APIC_VALUE_TPR_LO);

    
    __outbyte(0xa1, pic2Mask);          // put back pic2 mask
    ShortDelay(FALSE);

    __outbyte(0x21, pic1Mask);          // put back pic1 mask
    ShortDelay(TRUE);

    //
    // About to RE-ENABLE interrupts HERE
    //
    local_irq_restore(oldFlags);

    VDK_PRINT("Setting virtual wire mode - DONE\n");
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
    __u64 msrValue;

    void *apicPhysAddr;

#if defined(DEBUG)
    //
    // Find out where the IDT is hiding...
    //
    GetIDTDesc(&gIdtDesc);
#endif

    ReadMsr(IA32_APIC_BASE, &msrValue);

    apicPhysAddr = (void *) (msrValue & APIC_BASE_PHYS_ADDR);

    VDK_PRINT_DEBUG("Apic base (msr 0x%x) has 0x%p\n", IA32_APIC_BASE, (void *) msrValue);
    VDK_PRINT_DEBUG("Physical apic base address is 0x%p\n", apicPhysAddr);
    VDK_PRINT_DEBUG("Virtual apic base address is 0x%p\n", (void *) apicVirtAddr);

    if (msrValue & APIC_BASE_GLOBAL_ENABLE) {
        VDK_PRINT_DEBUG("Apic is already enabled\n");
    }
    else {
        VDK_PRINT_DEBUG("Putting APIC in virtual wire mode\n");
        SetVirtualWireMode();
    }

#if defined(DEBUG)
    {
        __u32 idtPerfOffset;
        LONG_IDT_ENTRY *pIdtEntry;
        void *targetOffset;

        VDK_PRINT("APIC id 0x%x\n", APIC_READ(apicVirtAddr, APIC_ID));
        VDK_PRINT("APIC version 0x%x\n", APIC_READ(apicVirtAddr, APIC_LVR));
        VDK_PRINT("APIC lvl timer 0x%x\n", APIC_READ(apicVirtAddr, APIC_LVTT));
        VDK_PRINT("APIC lvl perf 0x%x\n", APIC_READ(apicVirtAddr, APIC_LVTPC));
        VDK_PRINT("APIC lvl lint0 %x\n", APIC_READ(apicVirtAddr, APIC_LVT0));
        VDK_PRINT("APIC lvl lint1 %x\n", APIC_READ(apicVirtAddr, APIC_LVT1));

        idtPerfOffset = APIC_READ(apicVirtAddr, APIC_LVTT) & 0xff;

        pIdtEntry = ((LONG_IDT_ENTRY *) gIdtDesc.idtgdt_base) + idtPerfOffset;

        VDK_PRINT("timer - idt base: 0x%p, idtPerfOffset: 0x%x, pIdtEntry: 0x%p, *pIdtEntry: 0x%p, (*p+1): 0x%p\n",
                (void *)gIdtDesc.idtgdt_base,
                idtPerfOffset,
                (void *)pIdtEntry,
                (void *)pIdtEntry->lowQuad,
                (void *)pIdtEntry->highQuad);

        targetOffset = (void *) (pIdtEntry->offsetLow + (pIdtEntry->offsetMid << 16) + (((__u64) pIdtEntry->offsetHigh) << 32));

        VDK_PRINT("Idt targetOffset: 0x%p, sel: 0x%x, P: 0x%x, dpl: 0x%x, S: 0x%x type: 0x%x, ist: 0x%x\n",
                    (void *)targetOffset,
                    pIdtEntry->selector,
                    pIdtEntry->pres,
                    pIdtEntry->dpl,
                    pIdtEntry->sysUsr,
                    pIdtEntry->type,
                    pIdtEntry->ist);

        idtPerfOffset = APIC_READ(apicVirtAddr, APIC_LVTPC) & 0xff;

        pIdtEntry = ((LONG_IDT_ENTRY *) gIdtDesc.idtgdt_base) + idtPerfOffset;

        VDK_PRINT("perf - idt base: 0x%p, idtPerfOffset: 0x%x, pIdtEntry: 0x%p, *pIdtEntry: 0x%p, (*p+1): 0x%p\n",
                (void *)gIdtDesc.idtgdt_base,
                idtPerfOffset,
                (void *)pIdtEntry,
                (void *)pIdtEntry->lowQuad,
                (void *)pIdtEntry->highQuad);

        targetOffset = (void *) (pIdtEntry->offsetLow + (pIdtEntry->offsetMid << 16) + (((__u64) pIdtEntry->offsetHigh) << 32));

        VDK_PRINT("Idt targetOffset: 0x%p, sel: 0x%x, P: 0x%x, dpl: 0x%x, S: 0x%x type: 0x%x, ist: 0x%x\n",
                    (void *)targetOffset,
                    pIdtEntry->selector,
                    pIdtEntry->pres,
                    pIdtEntry->dpl,
                    pIdtEntry->sysUsr,
                    pIdtEntry->type,
                    pIdtEntry->ist);
    }
#endif
}

__u32
IsApicEnabled(
        void
        )
{
    __u64 msrValue;

    ReadMsr(IA32_APIC_BASE, &msrValue);

    return (msrValue & APIC_BASE_GLOBAL_ENABLE) ? TRUE : FALSE;
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
    __u32 perfLevel;

    perfLevel = APIC_READ(apicVirtAddr, APIC_LVTPC);

    VDK_PRINT_DEBUG("APIC PERF called. old perfValue is %x\n", perfLevel);

    if (wantEnable) {
        perfLevel &= ~APIC_LVT_MASKED;      // zero means enabled
    }
    else {
        perfLevel |= APIC_LVT_MASKED;       // one means disabled
    }

    VDK_PRINT_DEBUG("APIC PERF called. new perfValue is %x\n", perfLevel);

    APIC_WRITE(apicVirtAddr, APIC_LVTPC, perfLevel);
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
    __u32 perfLevel;

    perfLevel = APIC_READ(apicVirtAddr, APIC_LVTPC);

    VDK_PRINT_DEBUG("SetApicPerfLevel(%x), old value %x\n", newValue, perfLevel);

    APIC_WRITE(apicVirtAddr, APIC_LVTPC, newValue);

    return (perfLevel);
}
