/*
 *  vtlibcommon32.c
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
 *	File: vtlibcommon32.c
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

#include <linux/vmalloc.h>
#include <linux/slab.h>   
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/msr.h>
#include <asm/desc.h>
#include <asm/segment.h>

#include "vtuneshared.h"
#include "apic.h"
#include "vtdef.h"
#include "vtproto.h"
#include "vtextern.h"
#if defined(linux32)
#include "vtasm32.h"
#elif defined(linux32_64)
#include "vtasm32_64.h"
#endif
#include "vtlibcommon32.h"

#define _INIT_EMON_REGS_AT_START_OF_SAMPLING_SESSION 1

#define PACKAGE_INIT_COMPLETE -1

//
// Data to save/restore Cpu event monitoring registers          // 05-31-00
// accross an EBS session
//
__u32 num_emon_regs = 0;    // number of Emon regs
__u32 *p_emon_regs = 0;     // emon regs to save/restore

#if defined(ALLOW_LBRS)
__u32 quick_freeze_msr = 0; // non-zero is msr used to freeze LBRs. Want
			    //  this aligned on a 4-byte boundary if possible
BOOLEAN	capture_lbrs = FALSE;	// should we capture LBRs on this run?
#endif

__u32 cccr_index = 0;       // index of first cccr in Emonregs array
__u32 cccr_index_last = 0;  // index of last cccr in Emonregs array
__u32 cccr_count = 0;       // number of cccrs in Emonregs array
__u32 init_driver_var = 1;  // driver initialized flag

char emon_options[] = "INITEMON";   // eyecatcher for patching
__u32 init_emon_regs = 1;   // init all Emon regs at start of a sampling session
__u32 reset_and_power_on_pubs = 1;



extern BOOLEAN DTES_supported;  // cpu supports DTES feature        05-31-00
extern BOOLEAN pebs_option; // precise event sampling active    05-31-00

extern __u32 logical_processors_shift;

extern BOOLEAN HT_supported;    // cpu supports Hyper-Threading Technology  05-31-00
extern __u32 logical_processors_per_package;    // Pentium(R) 4 processor: logical processors per phycal processor
extern __u32 pebs_err;      // error during precise EBS ISR     05-31-00

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

/* ------------------------------------------------------------------------- */
/*!
 * @fn          __u32 IA32_family_F_reg_type(__u32 reg)
 * @brief       Checks if the msr provided is a legitmate PMU msr
 *
 * @param       reg	IN  - msr of interest
 *
 * @return	type of register or UNKNOWN_REG if not PMU msr
 *
 *
 * <I>Special Notes:</I>
 *
 *	Tied to current Pentium(R) 4 processor. Will likely need to be
 *      changed on future processors.
 *
 * @todo        Convert to use ptr to array of legit msr's chosen at boot time
 *
 */
__u32
IA32_family_F_reg_type(__u32 reg)
{
    if ((reg >= ESCR_FIRST) && (reg <= ESCR_LAST)) {
        //
        // On Linux*, skip regs that are in the ESCR range but are not implemented
        // on Pentium(R) 4 processors
        if ((reg == 0x3BF) || (reg == 0x3C6) || (reg == 0x3C7)
            || (reg == 0x3CE) || (reg == 0x3CF) || ((reg >= 0x3D0)
                                && (reg <= 0x3DF))) {
            return (UNKNOWN_REG);
        }
        return ESCR_REG;
    }

    if ((reg >= CCCR_FIRST) && (reg <= CCCR_LAST)) {
        return CCCR_REG;
    }

    if ((reg >= COUNT_FIRST) && (reg <= COUNT_LAST)) {
        return COUNT_REG;
    }

    if (reg == CRU_CR_PEBS_MATRIX_HORIZ) {
        return PEBS_REG;
    }

    if (reg == CRU_CR_PEBS_MATRIX_VERT) {
        return PEBS_REG;
    }

    return UNKNOWN_REG;
}

inline
ULARGE_INTEGER
samp_read_msr(__u32 reg)
{
    ULARGE_INTEGER val;

    rdmsr(reg, val.low_part, val.high_part);

    return (val);
}

inline
void
samp_write_msr(__u32 reg, ULARGE_INTEGER val)
{
    wrmsr(reg, val.low_part, val.high_part);

    return;
}

__u32
DTS_get_CPU_features(void)
/*++
Routine description:
    This routine is called by the DTSValidateprocessor()
    Calls the cpuid and returns the processor features

  Arguments:
    none

return value:
    The feature bits

--*/
{
    return (cpuid_edx(1));
}


BOOLEAN
known_reg(__u32 reg)
{
    if (IA32_family6) {
        return ((reg == 0xC1) || (reg == 0xC2)) ? TRUE : FALSE;
    }

    if (IA32_familyF) {
        return ((IA32_family_F_reg_type(reg) == UNKNOWN_REG) ? FALSE : TRUE);
    }

    return (FALSE);
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          BOOLEAN OS_safe_RDMSR(__u32 reg, PULARGE_INTEGER pui64_ret_value)
 * @brief       See if the msr is a PMU msr before doing an msr read
 *
 * @param       reg		IN  - msr of interest
 * @param       pui64_ret_value OUT - stores the result of the msr read
 *
 * @return	true if the msr is allowed, false otherwise
 *
 * Routine doesn't touch pui64_ret_value if the msr is not valid
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 */
BOOLEAN
OS_safe_RDMSR(__u32 reg, PULARGE_INTEGER pui64_ret_value)
{
    if (!known_reg(reg)) {
        return (FALSE);
    }

    *pui64_ret_value = samp_read_msr(reg);

    return (TRUE);
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          BOOLEAN OS_safe_WRMSR_direct(__u32 reg, PULARGE_INTEGER pui64_ret_value)
 * @brief       See if the msr is a PMU msr before doing an msr read
 *
 * @param       reg		IN - msr of interest
 * @param       pui64_ret_value IN - the value to write
 *
 * @return	true if the msr is allowed, false otherwise
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 */
BOOLEAN
OS_safe_WRMSR_direct(__u32 reg, PULARGE_INTEGER pui64_new_value)
{
    if (!known_reg(reg)) {
        return (FALSE);
    }

    samp_write_msr(reg, *pui64_new_value);

    return (TRUE);
}


/*++
Routine description:
    This routine is called by the Mapprocessors function.
    It calls the CPUID and returns the information in
    the EBX register which includes the number of logical
    processors per physical package and initial APIC ID

  Arguments:
    none

return value:
    The feature bits

--*/
__u32
get_APICID(void)
{
    __u32 features_ebx;

    features_ebx = cpuid_ebx(1);

    return features_ebx;
}

void
samp_set_PEBS_this_thread(BOOLEAN enable)
{
    ULARGE_INTEGER val;

    // 
    // CRU_CR_PEBS_MATRIX_HORIZ is per package and 
    // writes need to be serialized by spinlock. 
    // It would be more efficient to have a seperate 
    // spinlock per package, but this will do for now. 
    //
    // Note:
    // We don't want to change the PEBS state for the 
    // other thread. Bit 26 is controls and reflects
    // PEBS state of the other thread.
    //
    spin_lock(&reg3f1_write_lock);

    val = samp_read_msr(CRU_CR_PEBS_MATRIX_HORIZ);
    if (enable) {
        val.low_part |= ENABLE_EBS_MY_THR;
    } else {
        val.low_part &= ~ENABLE_EBS_MY_THR;
    }

    samp_write_msr(CRU_CR_PEBS_MATRIX_HORIZ, val);
    spin_unlock(&reg3f1_write_lock);

    return;
}

void
samp_disable_PEB_sand_DTS(void)
{
    ULARGE_INTEGER val;

    samp_set_PEBS_this_thread(FALSE);

    val = samp_read_msr(DEBUG_CTL_MSR);
    val.low_part &= ~(debug_ctl_msr_DTS);
    samp_write_msr(DEBUG_CTL_MSR, val);

    return;
}

/*++
Routine description:
    This routine clears the enable and overflow bits in the CCCR.
    It handles both methods of clearing the overflow bit on Pentium(R) 4 processor.
    Write of CCCR.OVF=0 clears OVF on Pentium(R) 4 processors with stepping C0 and newer.
    Write of CCCR.OVF=1 clears OVF on Pentium(R) 4 processors with pre C0 stepping.

  Arguments:
    cccr_reg

return value:
    TRUE... if overflow bit was set before counting disabled

--*/
BOOLEAN
CCCR_disable_counting_and_clear_ovf(__u32 cccr_reg)
{
    BOOLEAN ovf;
    ULARGE_INTEGER cccr, cccr2;

    ovf = FALSE;

    //
    // Disable counting and clear overflow interrupt status bit
    // The method of clearing the CCCR OVF bit changed between 
    // Pentium(R) 4 processor steppings...
    // steppings up to C0 - OVF is cleared by writing a 1 to OVF bit
    // steppings C0 and newer - OVF is cleared by writing a 0 to OVF bit
    //
    // On C0 and newer parts, write of the CCCR OVF bit is latached.
    // On pre C0 parts, write of a 0 to CCCR OVF is a don't care and does not 
    // change the CCCR OVF bit if it is 1
    //
    cccr = samp_read_msr(cccr_reg);
    cccr2 = cccr;
    cccr2.low_part &= ~(CCCR_Overflow_MASK | CCCR_Enable_MASK); // disable counting on all Pentium(R) 4 processors.  Clear OVF on all Pentium(R) 4 processors except those with pre C0 stepping
    samp_write_msr(cccr_reg, cccr2);
    if (cccr.low_part & CCCR_Overflow_MASK) {
        ovf = TRUE;
    }
    //
    // Clear CCCR OVF bit on pre C0 parts by writing CCCR OVF as a 1
    //
    // Note: GA Feb 10, 2002
    // Just discovered that the CCCR.Enable bit must be on to clear 
    // OVF bit on Pentium(R) 4 processor pre C0 parts. To handle this counting is disabled by
    // setting CCCR.Complement=0, CCCR.Compare=1 and CCCR.threshold=F. This effectively stops
    // counting but leaves the CCCR/PUB enabled so that CCCR.OVF can be cleared.
    //
    cccr = samp_read_msr(cccr_reg);
    if (cccr.low_part & CCCR_Overflow_MASK) {
        cccr2 = cccr;
        cccr2.low_part &= ~CCCR_Complement_MASK;    // set compare=1, complement=0, and threashold=F to stop counting 
        cccr2.low_part |= (CCCR_threshold_MASK | CCCR_Compare_MASK | CCCR_Enable_MASK); // ..(complement=0 means use > comparison with threshold which an't happen)
        samp_write_msr(cccr_reg, cccr2);    // ..write OVF=1 to clear OVF on Pentium(R) 4 processor pre C0
        cccr.low_part &= ~(CCCR_Overflow_MASK | CCCR_Enable_MASK);  // disable counting and write OVF=0 to make sure OVF is clear.
        samp_write_msr(cccr_reg, cccr); // ..for Pentium(R) 4 processor with C0 stepping or newer, there is a very small window where could have come on during initial write to disable (above this if statement)
        ovf = TRUE;
    }

    return (ovf);
}

void
reset_and_power_on_pub_counter(__u32 cccr_reg)
{
    ULARGE_INTEGER cccr;

    if (reset_and_power_on_pubs) {
        CCCR_disable_counting_and_clear_ovf(cccr_reg);  // disable counting and clear overflow bit
        cccr = samp_read_msr(cccr_reg); // enable CCCR but disable counting by setting
        cccr.low_part &= ~CCCR_Complement_MASK; // ..set compare=1, complement=0, and threashold=F 
        cccr.low_part |= (CCCR_threshold_MASK | CCCR_Compare_MASK | CCCR_Enable_MASK);  // ..write OVF=1 to clear OVF on Pentium(R) 4 processor pre C0
        samp_write_msr(cccr_reg, cccr); // ..
    }

    return;
}

void
init_EBS_regs_for_package(void)
{
    __u32 i;

    //
    // write EBS regs that are written once per package
    //
    for (i = 0; (i < MAX_REG_SET_ENTRIES) && (reg_set_init[i].reg_num); i++) {
        VDK_PRINT_DEBUG("init_EBS_regs_for_package: reg=%x  val=( 0x%x 0x%0x ) \n",
			reg_set_init[i].reg_num,
			reg_set_init[i].reg_val.high_part, reg_set_init[i].reg_val.low_part);
        samp_write_msr(reg_set_init[i].reg_num, reg_set_init[i].reg_val);
    }

    return;
}

/*++

Routine description:

    Save Pentium(R) 4 processor emon regs that are shared by logical processors within a package.

Arguments:

return value:

    none

--*/
void
save_clear_init_emon_regs_for_package(BOOLEAN init_EBS_regs_for_package_bool)
{
    __u32 i, cpu, initial_apic_ID, package;
    ULARGE_INTEGER val;
    ULARGE_INTEGER *p_reg_SA;

    if (!IA32_familyF) {
        return;
    }
    if ((num_emon_regs == 0) || (p_emon_regs == 0)) {
        return;
    }
    //
    // get physical package ID. A package consists of 1 or more logical cpus
    //
    // On return from CPUID eax=1, ebx bits 31-24 contain the initial apic ID for the
    // logical processor. The initial apic id is physical package id relative to 0
    // with the low order bits containing the logical processor number within the package.
    //
    cpu = smp_processor_id();
    if (cpu >= MAX_PROCESSORS) {
        return;
    }
    initial_apic_ID = (get_APICID() >> 24) & 1; // NEED TO FIX!! assuming max of 2 logical processors for now
    package = ((get_APICID() >> 24) >> logical_processors_shift) + 1;   // shift locigal proceoor number and add
    // 1 to get package ID relative to 1

    //
    // We only want save and clear ESCR/CCCR regs once for a package, since these
    // regs are shared between all logical processors in a package.
    //
    // The cpu number is saved in the package_status array to indicate
    // that the save has been done for this package.
    //
    i = interlocked_exchange(&package_status[package], cpu + 1);    // add 1 to cpu to make sure it is > 0
    if (i) {
        while (TRUE) {
            if (i == PACKAGE_INIT_COMPLETE) {
                interlocked_exchange(&package_status[package], PACKAGE_INIT_COMPLETE);
                return;
            }
            i = package_status[package];
        }
    }
    EnablePerfVect(FALSE);

    //
    // Save Emon regs for this package
    //

    i = interlocked_exchange(&eachCPU[cpu].processor_status, PSTATUS_EMON_REGS_SAVED);
    if (i == 0) {
        if (!eachCPU[cpu].p_emon_reg_save_area) {
            i = num_emon_regs * 8;
            eachCPU[cpu].p_emon_reg_save_area = allocate_pool(non_paged_pool, i);
            if (eachCPU[cpu].p_emon_reg_save_area) {
                memset(eachCPU[cpu].p_emon_reg_save_area, 0, i);
            }
        }
        if (eachCPU[cpu].p_emon_reg_save_area) {
            p_reg_SA = eachCPU[cpu].p_emon_reg_save_area;
            for (i = 0; i < num_emon_regs; i++) {
                p_reg_SA[i] = samp_read_msr(p_emon_regs[i]);
            }
            VDK_PRINT_DEBUG("%d emon regs saved for cpu %d package %d \n", i, cpu, package);
        } else {
            VDK_PRINT_DEBUG("unable to allocate emon regSaveArea for cpu %d package %d\n", cpu, package);
        }
    } else {
        VDK_PRINT_DEBUG("emon regs already saved for cpu %d package %d \n", cpu, package);
    }

#ifdef _INIT_EMON_REGS_AT_START_OF_SAMPLING_SESSION
    //
    // Clear EMON regs
    //

    VDK_PRINT_DEBUG("clearing Emon regs for cpu %d package %d \n", cpu, package);

    //
    // Make sure all pubs are powered on before clearing escr's.
    //
    // Note: 07/12/00
    // Michael Cranford (MD6) says pubs can power off which can cause units/subunits to power off.
    // If a pub is powered off when an escr is written, then the write may not take effect.
    // For example, if an escr was set for tagging and the pub was powered off when the escr is cleared,
    // then the tagging may continue after the clear of the escr. Sounds wierd.
    //
    if (init_emon_regs) {
        //
        // Make sure PUBS and Units are powered on when writing ESCRs
        //
        if (cccr_count) {
            for (i = cccr_index; i <= cccr_index_last; i++) {
                reset_and_power_on_pub_counter(p_emon_regs[i]);
            }
        }
        //
        // Zero all emon regs except CCCRs
        //
        val.quad_part = 0;
        for (i = 0; i < num_emon_regs && p_emon_regs[i]; i++) {
            //
            // Skip CCCRs
            //
            if (cccr_count && (i >= cccr_index)
                && (i <= cccr_index_last)) {
                continue;
            }
            samp_write_msr(p_emon_regs[i], val);
        }

        //
        // Init ESCRs while CCCR's are powered on
        //
        if (init_EBS_regs_for_package_bool) {
            init_EBS_regs_for_package();
            init_EBS_regs_for_package_bool = FALSE;
        }
        //
        // Zero CCCRs that are not set by init_EBS_regs_for_package()
        //
        if (cccr_count) {
            __u32 j;
            BOOLEAN ebs_CCCR;

            val.quad_part = 0;
            for (i = cccr_index; i <= cccr_index_last; i++) {
                ebs_CCCR = FALSE;
                for (j = 0; (j < MAX_REG_SET_ENTRIES)
                     && (reg_set_init[j].reg_num); j++) {
                    if (reg_set_init[j].reg_num == p_emon_regs[i]) {
                        ebs_CCCR = TRUE;
                        break;
                    }
                }
                if (ebs_CCCR) {
                    continue;
                }
                samp_write_msr(p_emon_regs[i], val);
            }
        }

    }
#endif              // _INIT_EMON_REGS_AT_START_OF_SAMPLING_SESSION

    if (init_EBS_regs_for_package_bool) {
        init_EBS_regs_for_package();
    }

    interlocked_exchange(&package_status[package], PACKAGE_INIT_COMPLETE);

    return;
}

__u32
init_driver(void)
{
    __u32 i;
    int ret_val;

    ret_val = init_driver_OS();
    if (ret_val != STATUS_SUCCESS) {
        return ret_val;
    }

    /* Initialize system info storage */
    memset(&samp_parms, 0, sizeof (samp_parm6));

    max_counter.quad_part = IA32_FAMILY6_MAX_COUNTER;
    switch (g_CPU_family) {
    case 5:
        IA32_family5 = TRUE;
        break;
    case 6:
        IA32_family6 = TRUE;
        break;
    case 0xf:
        IA32_familyF = TRUE;
        break;
    default:
        break;
    }
    if (IA32_familyF) {
        __u32 features;

        //
        // get CPU features
        //
        features = DTS_get_CPU_features();
        if (features & CPUID_DTS_MASK) {    //21st Bit should be set
            DTES_supported = TRUE;
        } else {
            DTES_supported = FALSE;
        }
        //
        // Check if Hyper-Threading Technology is supported
        //
        if (features & CPUID_HT_MASK) { // 28th Bit should be set
            HT_supported = TRUE;
        } else {
            HT_supported = FALSE;
        }
        logical_processors_per_package = 1;
        logical_processors_shift = 1;
        if (HT_supported) {
            __u32 i;
            logical_processors_per_package = (get_APICID() & 0xff0000) >> 16;
            if (logical_processors_per_package == 0) {
                logical_processors_per_package = 1;
            }
            i = logical_processors_per_package;
            logical_processors_shift = 0;
            i--;
            while (1) {
                logical_processors_shift++;
                i >>= 1;
                if (!i) {
                    break;
                }
            }
        }
        VDK_PRINT_DEBUG("cpu features...  DTES=%d HT=%d logical_processors_per_package %d  Shift %d \n",
			(DTES_supported) ? 1 : 0, (HT_supported) ? 1 : 0,
			logical_processors_per_package, logical_processors_shift);
        validate_emon_regs();
    }

    /* Initialize register Sets */
    memset(reg_set, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set0, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set1, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set_init, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);

    /* Initialize CPU Specific Sampling Defaults */
    if (IA32_family6) {
        rdpmc_msr_base = 0xC1;
        set_IA32_family6_emon_defaults();
    }
    if (IA32_familyF) {
        rdpmc_msr_base = 0x300; // 03-31-01
        // set_IA32_family_F_emon_defaults();
        // 03-31-00 Pentium(R) 4 processor
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


    for (i = 0; i < MAX_PROCESSORS; i++) {
        eachCPU[i].DTES_buf = 0;    // 05-31-00 Pentium(R) 4 processor
        eachCPU[i].org_dbg_ctl_msr_val.quad_part = 0;
        eachCPU[i].org_DTES_area_msr_val.quad_part = 0;
        eachCPU[i].p_emon_reg_save_area = 0;
    }

    for (i = 0; i < MAX_PROCESSORS; i++) {  //                             12-21-97
        eachCPU[i].samp_EBS_idt_routine = (void *) t_ebs;
        eachCPU[i].original_EBS_idt_entry = 0;
        eachCPU[i].original_apic_perf_local_vector = 0;
    }

    //
    // Allocate Driver Shared Area (DSA) in non paged pool.
    //

    pdsa = create_dsa();
    if(pdsa == NULL)
    {
      VDK_PRINT_ERROR("init_driver: could not create DSA\n");
      return (STATUS_INVALID_PARAMETER);
    }
    pdsa->method_EBS = TRUE;    // NEED TO FIX!! EBS should be set based on cpu features and EBS test
    pdsa->module_tracking = TRUE;   // driver can do module tracking

#ifdef USE_NMI
    //
    // Use nmi for counter overflow interrupt
    //
    //  apic perf lvt... PMI delivered as NMI
    //
    apic_perf_lvt = 0x400; // mode 100 : see page 8-16 in Intel(R) Pentium 4 Processor Manual Vol 3
    VDK_PRINT("using NMI with apic_perf_lvt=0x%x\n",apic_perf_lvt);
#else
    apic_perf_lvt = APIC_PERFLVT_FIXED_ENA | PERF_MON_VECTOR;
#endif

    DoApicInit ();

    return (STATUS_SUCCESS);
}

/*++

Routine description:

    restore Pentium(R) 4 processor emon regs for the current cpu.

Arguments:

return value:

    none

--*/
void
restore_emon_regs(void)
{
    __u32 i, j, cpu;
    PULARGE_INTEGER p_reg_SA;

    if (!IA32_familyF) {
        return;
    }
    if ((num_emon_regs == 0) || (p_emon_regs == 0)) {
        return;
    }

    cpu = smp_processor_id();
    if (cpu >= MAX_PROCESSORS) {
        return;
    }

    i = interlocked_exchange(&eachCPU[cpu].processor_status, 0);
    if (i != PSTATUS_EMON_REGS_SAVED) {
        VDK_PRINT_DEBUG("restore_emon_regs... no restore, regs not saved  cpu %d \n", cpu);
        return;
    }

    p_reg_SA = eachCPU[cpu].p_emon_reg_save_area;
    if (!p_reg_SA) {
        VDK_PRINT_DEBUG("restore_emon_regs... no restore, no save area cpu %d \n", cpu);
        return;
    }
    EnablePerfVect(FALSE);

#ifdef _INIT_EMON_REGS_AT_START_OF_SAMPLING_SESSION
    //
    // Make sure all pubs are powered on before clearing escr's.
    //
    if (init_emon_regs) {
        if (cccr_count) {
            for (i = cccr_index; i <= cccr_index_last; i++) {
                reset_and_power_on_pub_counter(p_emon_regs[i]);
            }
        }
    }
#endif              // _INIT_EMON_REGS_AT_START_OF_SAMPLING_SESSION

    //
    // restore all regs except CCCR's... restore escrs and other msrs
    //
    j = 0;
    for (i = 0; i < num_emon_regs && p_emon_regs[i]; i++) {
        if (cccr_count && (i >= cccr_index) && (i <= cccr_index_last)) {
            continue;
        }
        samp_write_msr(p_emon_regs[i], p_reg_SA[i]);
        j++;
    }

    //
    // restore CCCRs last
    //
    if (cccr_count) {
        for (i = cccr_index; i <= cccr_index_last; i++) {
            samp_write_msr(p_emon_regs[i], p_reg_SA[i]);
            j++;
        }
    }

    {
        __u32 initial_apic_ID, package;

        initial_apic_ID = (get_APICID() >> 24) & 1; // NEED TO FIX!! assuming max of 2 logical processors for now
        package = ((get_APICID() >> 24) >> logical_processors_shift) + 1;   // shift locigal proceoor number and add
        VDK_PRINT_DEBUG("%d emon regs restored for package %d cpu %d \n", j, package, cpu);
    }

    return;
}

PREG_SET
get_reg_set_table(void)
{
    REG_SET *p_reg_set;

    p_reg_set = reg_set;
    if (IA32_familyF) {
        if (get_APICID() & (1 << 24)) {
            p_reg_set = reg_set1;
        } else {
            p_reg_set = reg_set0;
        }
    }

    return (p_reg_set);
}

void
add_to_counter_total_on_overflow(PREG_SET p_reg_set, __u32 cpu)
{
    p_reg_set->event_total[cpu].quad_part += p_reg_set->event_inc.quad_part;

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
    __u32 i, cpu;
    REG_SET *p_reg_set;
    ULARGE_INTEGER val;

    p_reg_set = get_reg_set_table();

    cpu = smp_processor_id();
    for (i = 0; i < MAX_REG_SET_ENTRIES && p_reg_set[i].reg_num; i++) {
        if (!(p_reg_set[i].options & REG_SET_COUNTER)) {
            continue;
        }
        val = samp_read_msr(p_reg_set[i].reg_num);
        val.quad_part &= max_counter.quad_part;
        if (val.quad_part >= p_reg_set[i].reg_val.quad_part) {
            val.quad_part -= p_reg_set[i].reg_val.quad_part;
        }
        p_reg_set[i].event_total[cpu].quad_part += val.quad_part;
        VDK_PRINT_DEBUG("event id %d pmc_msr %x inc %d\n",
			p_reg_set[i].event_ID, p_reg_set[i].reg_num, p_reg_set[i].event_inc);
        VDK_PRINT_DEBUG("event total 0x%x %08x\n",
			p_reg_set[i].event_total[cpu].high_part, p_reg_set[i].event_total[cpu].low_part);

    }

    return;
}

/*
 *
 *  Function: read_cpu_perf_counters_for_current_cpu
 *
 *  Description: 
 *  Put cpu perf counter data in caller's buffer.
 *  Save current counter value and event total the session.
 *
 *  Parms:
 *      Entry:      address of counter buffer
 *  
 *      Return:    
 *
 */
void
read_cpu_perf_counters_for_current_cpu(
    void *info
    )
{
    __u32 i, cpu, tmp_cpu_num, pmc_msr, cpu_mask, pmc_num;
    PREG_SET preg_set;
    ULARGE_INTEGER pmc_val, start_total, pmc_mask, pmc_mask_read;
    RDPMC_BUF *pr_buf;

    pr_buf = (RDPMC_BUF *)info;

    if (!pr_buf)
    {
        return;
    }

    cpu = smp_processor_id();

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
        pmc_msr = rdpmc_msr_base;
        preg_set = get_reg_set_table();
        for (i=0; i < MAX_REG_SET_ENTRIES; i++, preg_set++) 
        {
            if (!(preg_set->options & REG_SET_COUNTER)) 
            {
                continue;
            }
            pmc_num = preg_set->reg_num - rdpmc_msr_base;
            pmc_mask.quad_part = 1 << pmc_num;
            //
            // If pmc is not in regSet, skip it.
            //
            pmc_msr = preg_set->reg_num;
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
                start_total = preg_set->event_total[tmp_cpu_num];
                pmc_val = samp_read_msr(pmc_msr);
	      } while (start_total.quad_part != preg_set->event_total[tmp_cpu_num].quad_part);
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
        // On JT, the caller needs to be aware that the cpu perf counters are
        // shared within a package. This will lead to pmc being read twice
        // once for each logical cpu. The caller needs to take care not to double 
        // count pmc values for a JT enabled package.
        //
        pmc_msr = rdpmc_msr_base;
        pmc_mask.quad_part = 1;
        for (i=0; i < RDPMC_MAX_PMCS; i++, pmc_msr++, pmc_mask.quad_part = pmc_mask.quad_part << 1)
        {
            if (!(pr_buf->pmc_mask.quad_part & pmc_mask.quad_part) ||
                 (pmc_mask_read.quad_part & pmc_mask.quad_part))
            {
                continue;
            }
            if (OS_safe_RDMSR(pmc_msr, &pmc_val))
            {
                pr_buf->pmc_values[i].pmc_val[cpu].pmc_current.quad_part = pmc_val.quad_part & max_counter.quad_part;
            }
            else
            {
                pr_buf->pmc_values[i].pmc_val[cpu].pmc_current.quad_part = 0;
            }
	    pr_buf->pmc_values[i].pmc_val[cpu].pmc_total.quad_part = 0;
        }

        spin_lock(&rdpmc_lock);
        pr_buf->cpu_mask_out |= cpu_mask;
        spin_unlock(&rdpmc_lock);    
    }

    return;
}


/*
**********************************************************************************************************

    START/STOP EBS FUNCTIONS

**********************************************************************************************************
*/

__u32
samp_check_emon_counter_overflow_IA32_family5(void)
{
    return (0);     // Pentium(R) processor EBS not supported
}

void
samp_stop_emon_IA32_family6(void)
{
    ULARGE_INTEGER esr0;

    //
    //  Disable counting for both counters
    //
    esr0 = samp_read_msr(EM_MSR_PESR0);
    esr0.low_part &= ~EM_EN_MASK;
    samp_write_msr(EM_MSR_PESR0, esr0);

    return;
}

void
samp_start_emon_IA32_family6(u32 do_stop)
{
    int i, cpu;
    REG_SET *p_reg_set;
    BOOLEAN enable_counters = FALSE, start_all_counters = FALSE;
    ULARGE_INTEGER val;

    if (do_stop) {
        samp_stop_emon_IA32_family6();
    }

    cpu = smp_processor_id();
    if (eachCPU[cpu].start_all) {
        eachCPU[cpu].start_all = FALSE;
        start_all_counters = TRUE;
    }
    //
    //  Disable counting for both counters
    //
    val = samp_read_msr(EM_MSR_PESR0);
    val.low_part &= ~EM_EN_MASK;
    samp_write_msr(EM_MSR_PESR0, val);

    //
    // Set msrs for sampling
    //
    p_reg_set = reg_set;
    for (i = 0; (i < MAX_REG_SET_ENTRIES) && (p_reg_set->reg_num); i++, p_reg_set++) {
        val = p_reg_set->reg_val;
        if (p_reg_set->reg_num == EM_MSR_PESR1) {
            enable_counters = TRUE;
        }
        if (p_reg_set->reg_num == EM_MSR_PESR0) {
            enable_counters = TRUE;
            val.low_part &= ~EM_EN_MASK;
        }
        if (p_reg_set->options & REG_SET_COUNTER) {
            if ((p_reg_set->c_ovf[cpu]) || start_all_counters) {
                p_reg_set->c_ovf[cpu] = 0;
            } else {
                continue;
            }
        }
        samp_write_msr(p_reg_set->reg_num, val);
    }

    //
    // Fix EBS on new mobile processors
    //
    EnablePerfVect(TRUE);

    //
    // If either PESR0 or PESR1 was written, enable both counters
    //
    if (enable_counters) {
        val = samp_read_msr(EM_MSR_PESR0);
        val.low_part |= EM_EN_MASK;
        samp_write_msr(EM_MSR_PESR0, val);
    }

    return;
}

__u32
samp_check_emon_counter_overflow_IA32_family6(void)
{
    int i, cpu;
    BOOLEAN ovf;
    __u32 events = 0;
    REG_SET *p_reg_set;
    ULARGE_INTEGER val, esr0;

    //
    //  Disable counting for both counters
    //
    esr0 = samp_read_msr(EM_MSR_PESR0);
    esr0.low_part &= ~EM_EN_MASK;
    samp_write_msr(EM_MSR_PESR0, esr0);

    cpu = smp_processor_id();
    p_reg_set = reg_set;
    for (i = 0; (i < MAX_REG_SET_ENTRIES) && (p_reg_set->reg_num); i++, p_reg_set++) {
        ovf = FALSE;
        if (p_reg_set->reg_num == EM_MSR_PCTR0) {
            if (esr0.low_part & EM_INT_MASK) {
                val = samp_read_msr(EM_MSR_PCTR0);
                val.high_part &= max_counter.high_part;
                if (val.quad_part < p_reg_set->reg_val.quad_part) {
                    ovf = TRUE;
                }
            }
        } else {
            if (p_reg_set->reg_num == EM_MSR_PCTR1) {
                val = samp_read_msr(EM_MSR_PESR1);
                if (val.low_part & EM_INT_MASK) {
                    val = samp_read_msr(EM_MSR_PCTR1);
                    val.high_part &= max_counter.high_part;
                    if (val.quad_part < p_reg_set->reg_val.quad_part) {
                        ovf = TRUE;
                    }
                }
            }

        }
        if (ovf) {
            if (interlocked_exchange(&p_reg_set->c_ovf[cpu], 1) == 0) {
                add_to_counter_total_on_overflow(p_reg_set, cpu);
            }
            events |= 1 << p_reg_set->event_I_dindex;
        }
    }

    return (events);
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void samp_stop_emon_IA32_familyF(void)
 * @brief       Stop the PMU for the Pentium(R) 4 processor family
 *
 * @return	none
 *
 * Heavily hard coded for early Pentium(R) 4 processors. Likely to change 
 * on later Pentium(R) 4 processors...
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 */
void
samp_stop_emon_IA32_familyF(void)
{
    int i, cccr_reg, count_reg = 0, cpu;
    BOOLEAN cindex_valid, ovf;
    ULARGE_INTEGER cccr, cccr2, count, count2;
    REG_SET *p_reg_set, *p_reg_set_tbl;
#if defined(linux32)
    PDTS_BUFFER p_DTES;
#elif defined(linux32_64)
    PIA32EM64T_DTS_BUFFER p_DTES;
#endif

#if defined(ALLOW_LBRS)
    ULARGE_INTEGER val;
#endif

#ifdef USE_NMI
    // temporarily disable if event is precise
    if (pebs_option)
      return;
#endif

    if (get_APICID() & (1 << 24)) {
        p_reg_set = reg_set1;
    } else {
        p_reg_set = reg_set0;
    }

    p_reg_set_tbl = p_reg_set;
    cpu = smp_processor_id();
    for (i = 0; (i < MAX_REG_SET_ENTRIES) && (p_reg_set->reg_num); i++, p_reg_set++) {
        if (p_reg_set->options & REG_SET_CCCR) {
            ovf = FALSE;
            cccr_reg = p_reg_set->reg_num;
            if (p_reg_set->options & REG_SET_CINDEX_VALID) {
                cindex_valid = TRUE;
                count_reg = p_reg_set_tbl[p_reg_set->c_index].reg_num;
                count = samp_read_msr(count_reg);
            } else {
                cindex_valid = FALSE;
            }

            //
            // Disable counting and clear overflow interrupt status bit
            // The method of clearing the CCCR OVF bit changed...
            // steppings up to C0 - OVF is cleared by writing a 1 to OVF
            // steppings C0 and newer - OVF is cleared by writing a 0 to OVF bit
            //
            // On C0 and newer parts, write of the CCCR OVF bit is latached.
            // On pre C0 parts, write of a 0 to CCCR OVF is a don't care and does not 
            // change the CCCR OVF bit if it is 1
            //
            // Note: GA Feb 10, 2002
            // Just discovered that the CCCR.Enable bit must be on to clear 
            // OVF bit on Pentium(R) 4 processor pre C0 parts. To handle this counting 
            // is disabled by setting CCCR.Complement=0 and CCCR.threshold=F. 
            // This effectively stops counting but leaves the CCCR/PUB enabled.
            //
            cccr = samp_read_msr(cccr_reg);
            if (!(cccr.low_part & CCCR_Enable_MASK)) {
                continue;
            }

            ovf = CCCR_disable_counting_and_clear_ovf(cccr_reg);

            //
            // At this point counting is stopped and the CCCR OVF bit has 
            // been cleared. 
            // 
            // The logical overflow flag ("ovf") has been based on the CCCR OVF bit, but we need
            // to treat the following two cases as overflow
            //
            // 1) C0 or newer cpu and counter overflowed between CCCR read and CCCR write that we just did to disable counting.
            //    When this happens, the write to the CCCR to disable counting clears the CCCR OVF bit. To detect overflow in this 
            //    case, we compare the counter before and after the CCCR write.
            //
            // 2) CCCR FOVF bit is set and the counter is different than the initial counter value
            //
            // Another special case:
            //
            //    If the CCCR overflow bit was set but counter is 0, then it's not time to take a sample.
            //    We want to take a sample on the next occurence of the event. To do this we 
            //    do not treat as overflow, but instead set the force overflow flag to get an interrupt 
            //    on the next occurence of the event.
            // 
            //    Note:
            //    On Pentium(R) 4 processor, the CCCR OVF bit is set when the counter increments
	    //    from the max value to 0.
            //    This does not cause an interrupt to be generated. A counter overflow interrupt is generated
            //    when the event counter increments and either the CCCR OVF or FOVF bits are set.
            //
            //    
            // 
            if (cindex_valid) {
                count2 = samp_read_msr(count_reg);
                count2.high_part &= max_counter.high_part;
                if (!ovf) {
                    count.high_part &= max_counter.high_part;
                    //
                    // We need to handle the case where the counter may have overflowed between
                    // the CCCR read and the CCCR write that we just did to disable counting.
                    // 
                    // To detect the overflow in this small window, we compare the event count reg
                    // before and after disabling counting. If the current count is less than the count 
                    // before sampling was stopped, then assume counter has overflowed.
                    //
                    if (count2.quad_part < count.quad_part) {
                        ovf = TRUE;
                    }
                }
                //
                // If CCCR force overflag is set and the current value of the counter is not
                // equal to the initial counter value, then treat it as if the counter overflowed.
                //
                if (cccr.low_part & CCCR_ForceOverflow_MASK) {
                    if (count2.quad_part != p_reg_set_tbl[p_reg_set->c_index].reg_val.quad_part) {
                        ovf = TRUE;
                        //
                        // Clear force overflow bit, if force overflow is set in the CCCR register
                        // and the force overflow bit is not set in the user's original CCCR value.
                        //
                        if (cccr.low_part & CCCR_ForceOverflow_MASK) {
                            if (!(p_reg_set->reg_val.low_part & CCCR_Overflow_MASK)) {
                                cccr2 = samp_read_msr(cccr_reg);
                                cccr2.low_part &= ~CCCR_ForceOverflow_MASK;
                                samp_write_msr(cccr_reg, cccr2);
                            }
                        }
                    }
                } else {
                    //
                    // If the counter has overflowed but the counter is 0 and FOVF not set,
                    // then it is not time to take a sample. Instead we set the 
                    // force overflow bit in the CCCR and we will take a sample
                    // next time the counter is incremented.
                    //
                    // Note: 
                    // For PEBS, we should never hit this case, because 
                    // we only support one event when doing PEBS.
                    //
                    if (ovf) {
                        if (count2.quad_part == 0) {
                            ovf = FALSE;
                            cccr2 = samp_read_msr(cccr_reg);
                            cccr2.low_part |= CCCR_ForceOverflow_MASK;
                            samp_write_msr(cccr_reg, cccr2);
                        }
                    }
                }
            }
            if (pebs_option) {
                p_DTES = eachCPU[cpu].DTES_buf;
                if (p_DTES) {
                    if (p_DTES->PEBS_index != p_DTES->PEBS_base) {
                        ovf = TRUE;
                    }
                }
            }
            if (ovf && cindex_valid) {
                if (interlocked_exchange(&p_reg_set_tbl[p_reg_set->c_index].c_ovf[cpu], 1) == 0) {
                    add_to_counter_total_on_overflow(&p_reg_set_tbl[p_reg_set->c_index], cpu);
                }
            }
        }
    }

    if (pebs_option) {
        samp_disable_PEB_sand_DTS();
    }
#if defined(ALLOW_LBRS)
    // LBRs are per logical processor so need to clear per cpu
    if (capture_lbrs) {
	val = samp_read_msr(DEBUG_CTL_MSR);
	val.quad_part &= ~debug_ctl_msr_LBR;
	samp_write_msr(DEBUG_CTL_MSR, val);
    }
#endif

    return;
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void samp_start_emon_IA32_familyF(u32 do_stop)
 * @brief       Stop the PMU for the Pentium(R) 4 processor family
 *
 * @return	none
 *
 * Heavily hard coded for early Pentium(R) 4 processors.  Likely to change
 * on later Pentium(R) 4 processors...
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 * @todo        Get rid of some of the useless types (like u32 vs. __u32).
 *
 */
void
samp_start_emon_IA32_familyF(u32 do_stop)
{
    int i, cpu;
#if defined(linux32)
    PDTS_BUFFER p_DTES;
#elif defined(linux32_64)
    PIA32EM64T_DTS_BUFFER p_DTES;
#endif
    REG_SET *p_reg_set, *p_regs;
    BOOLEAN start_all_counters = FALSE;

#ifdef USE_NMI
    // temporarily disable if event is precise
    if (pebs_option)
      return;
#endif

    if (do_stop) {
        samp_stop_emon_IA32_familyF();
    }
    EnablePerfVect(FALSE);

    cpu = smp_processor_id();
    if (eachCPU[cpu].start_all) {
        eachCPU[cpu].start_all = FALSE;
        start_all_counters = TRUE;
    }

    if (pebs_option) {
        if (cpu < MAX_PROCESSORS) {
            p_DTES = eachCPU[cpu].DTES_buf;
            if (p_DTES) {
                p_DTES->PEBS_index = p_DTES->PEBS_base; // reset index to next PEBS record to base of buffer
            }
            samp_set_PEBS_this_thread(TRUE);
        }
    }

    if (get_APICID() & (1 << 24)) {
        p_reg_set = reg_set1;
    } else {
        p_reg_set = reg_set0;
    }

    p_regs = p_reg_set;
    for (i = 0; (i < MAX_REG_SET_ENTRIES) && (p_reg_set->reg_num); i++, p_reg_set++) {
        if (p_reg_set->options & REG_SET_CCCR) {
            continue;
        }
        if (p_reg_set->options & REG_SET_COUNTER) {
            if ((p_reg_set->c_ovf[cpu]) || start_all_counters) {
                p_reg_set->c_ovf[cpu] = 0;
            } else {
                continue;
            }
        }
        samp_write_msr(p_reg_set->reg_num, p_reg_set->reg_val);
    }

    p_reg_set = p_regs;
    for (i = 0; (i < MAX_REG_SET_ENTRIES) && (p_reg_set->reg_num); i++, p_reg_set++) {
        if (p_reg_set->options & REG_SET_CCCR) {
            __u32 cccr_reg;
            ULARGE_INTEGER cccr_val, cccr;

            cccr_reg = p_reg_set->reg_num;
            cccr_val = p_reg_set->reg_val;
            if (!start_all_counters) {
                cccr = samp_read_msr(cccr_reg);
                if (cccr.low_part & CCCR_ForceOverflow_MASK) {
                    cccr_val.low_part |= CCCR_ForceOverflow_MASK;
                }
            }
            samp_write_msr(cccr_reg, cccr_val);
        }
    }
#if defined(ALLOW_LBRS)
    // LBRs are per logical processor so need to set per cpu
    if (capture_lbrs) {
	ULARGE_INTEGER val;

	val = samp_read_msr(DEBUG_CTL_MSR);
	val.quad_part |= debug_ctl_msr_LBR;
	samp_write_msr(DEBUG_CTL_MSR, val);
    }
#endif
    EnablePerfVect(TRUE);

    return;
}

/*++
Routine description:
    This routine is returns

  Arguments:
    none

return value:
    A __u32 representing events for which a counter overflow
    has occured. Each bit of the __u32 corresponds to an 
    entry in the event_Ids[] table. 
    Example: if bit0 of the returned __u32 is 1, then a counter
    overflow has occured for the 1st event in the event_Ids[] table.

--*/
__u32
samp_check_emon_counter_overflow_IA32_familyF(void)
{
    int i, cpu;
    __u32 events = 0;
    REG_SET *p_reg_set;

    if (get_APICID() & (1 << 24)) {
        p_reg_set = reg_set1;
    } else {
        p_reg_set = reg_set0;
    }

    cpu = smp_processor_id();
    for (i = 0; (i < MAX_REG_SET_ENTRIES) && (p_reg_set->reg_num); i++, p_reg_set++) {
        if (p_reg_set->options & REG_SET_COUNTER) {
            if (p_reg_set->c_ovf[cpu]) {
                events |= 1 << p_reg_set->event_I_dindex;
            }
        }
    }

    return (events);
}

void
samp_start_emon(void *info)
{
    u32 do_stop;

    do_stop = (info) ? TRUE : FALSE;

    if (IA32_familyF) {
        samp_start_emon_IA32_familyF(do_stop);
    } else {
        if (IA32_family6) {
            samp_start_emon_IA32_family6(do_stop);
        }
    }

    return;
}

void
samp_stop_emon(void)
{
    if (IA32_familyF) {
        samp_stop_emon_IA32_familyF();
    } else {
        if (IA32_family6) {
            samp_stop_emon_IA32_family6();
        }
    }
    return;
}

BOOLEAN
validate_reg_RW(__u32 reg, ULARGE_INTEGER val)
{
    BOOLEAN ret = TRUE;
    ULARGE_INTEGER org_val;

    if (!OS_safe_RDMSR(reg, &org_val)) {
        return (FALSE);
    }
    if (!OS_safe_WRMSR_direct(reg, &val)) {
        ret = FALSE;
    }
    if (!OS_safe_WRMSR_direct(reg, &org_val)) {
        ret = FALSE;
    }

    return (ret);
}

/*++

Routine description:

    Build array of Pentium(R) 4 processor emon registers that will be saved/restored
    across an EBS sampling session. These are registers which are shared between
    logical processors within a package. They are only saved once per package
    when sampling is started.

Arguments:

return value:

    none

--*/
void
validate_emon_regs(void)
{
    __u32 reg, i, max_regs;
    ULARGE_INTEGER val;

    if (!IA32_familyF) {
        return;
    }

    max_regs = NUM_ESCRS + NUM_CCCRS + 2 + 1;   // max emon regs to save/restore per package
    // add two for CRU_CR_PEBS_MATRIX_HORIZ and CRU_CR_PEBS_MARTRIX_VERT
    // add 1 for array termination
    num_emon_regs = 0;
    i = max_regs * 4;
    p_emon_regs = allocate_pool(non_paged_pool, i);
    if (!p_emon_regs) {
        return;
    }
    memset(p_emon_regs, 0, i);

    //
    // Add valid ESCRs to emon reg array
    //
    val.quad_part = 0;
    for (i = 0, reg = ESCR_FIRST; reg <= ESCR_LAST; reg++) {
    //MSRs 0x3BA and 0x3BB have been removed in model 3
        if ((g_CPU_model == 3) && ((reg == 0x3BA) || (reg == 0x3BB))){
                continue;
        }
        if (validate_reg_RW(reg, val)) {
            p_emon_regs[i] = reg;
            i++;
        } else {
            VDK_PRINT_DEBUG("invalid ESCR %x \n", reg);
        }
    }

    //
    // Add valid CCCRs to emon reg array
    //
    VDK_PRINT_DEBUG("validate_emon_regs: validating CCCR's \n");
    cccr_index = i;
    cccr_count = 0;
    for (reg = CCCR_FIRST; reg <= CCCR_LAST; reg++) {
        if (validate_reg_RW(reg, val)) {
            p_emon_regs[i] = reg;
            i++;
            cccr_count++;
        } else {
            VDK_PRINT_DEBUG("invalid CCCR %x \n", reg);
        }
    }
    cccr_index_last = (cccr_count) ? (cccr_index + cccr_count - 1) : 0;

    //
    // Add PEBS_MATRIX regs to emon reg array
    //

    reg = CRU_CR_PEBS_MATRIX_HORIZ;
    if (validate_reg_RW(reg, val)) {
        p_emon_regs[i] = reg;
        i++;
    } else {
        VDK_PRINT_DEBUG("invalid CRU_CR_PEBS_MATRIX_HORIZ %x \n", reg);
    }
    reg = CRU_CR_PEBS_MATRIX_VERT;
    if (validate_reg_RW(reg, val)) {
        p_emon_regs[i] = reg;
        i++;
    } else {
        VDK_PRINT_DEBUG("invalid CRU_CR_PEBS_MATRIX_VERT %x \n", reg);
    }
    num_emon_regs = i;

    return;
}

/*++

Routine description:

    This ensures that each entry in the regSet array is a vaild register.

Arguments:

return value:

    0.......... all registers in the regSet array are valid
    non-zero... index relative to one of the first invalid register in the regSet array.
                (i.e. 1 means first entry in the regSet array)

--*/
__u32
validate_EBS_regs(void)
{
    __u32 i, j;
    REG_SET *p_reg_set = NULL;

    j = 0;
    while (j < 3) {
        //
        // Validate EBS regs
        //
        if (j == 0) {
            //
            // On IA32_familyF (Pentium(R) 4 processor), regset0, reg_set1 and regsetInit are
            // used to hold reg for EBS.
            // On other processors, regset and regsetInit are used and regset0 and regset1 will
            // be empty and initialized to zeros.
            //
            if (IA32_familyF) {
                p_reg_set = reg_set0;
            } else {
                p_reg_set = reg_set;    // regset is used on non-Pentium(R) 4 processors
            }
        }
        if (j == 1) {
            p_reg_set = reg_set1;
        }
        if (j == 2) {
            p_reg_set = reg_set_init;
        }
        j++;

        for (i = 0; (i < MAX_REG_SET_ENTRIES) && (p_reg_set->reg_num); i++, p_reg_set++) {
            if (!validate_reg_RW(p_reg_set->reg_num, p_reg_set->reg_val)) {
                VDK_PRINT_DEBUG("invalid reg for EBS... reg %x %p \n",
				p_reg_set->reg_num, p_reg_set->reg_val);
                return (++i);
            }
        }
    }

    return 0;       // all regs valid
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
    __u32 i;
    void_ptr p;
    int ret;
//BSBS this is only for linux32 because older kernels (not x86-64) 
//dont expose the virtual address of the APIC so we have to map it
//in DoApicInit
#if defined(linux_32) 
    if ((p = xchg(&apic_local_addr, 0))) {
        un_map_apic(p);
    }
#endif
    
    if ((p = xchg(&p_emon_regs, 0))) {
        free_pool(p);
    }

    for (i = 0; i < MAX_PROCESSORS; i++) {
        if ((p = xchg(&eachCPU[i].p_emon_reg_save_area, 0))) {
            free_pool(p);
        }
    }

    ret = destroy_dsa();
    pdsa = NULL;
    
    return;
}

void
samp_init_emon_regs(void *info)
{
    if (IA32_familyF) {
        //
        // Save and clear emon regs for this package. We also PASS "TRUE"
        // to have EBS regs written that only get written once per package
        //
        save_clear_init_emon_regs_for_package(TRUE);    // 05-31-00
    }

    return;
}

void
samp_start_profile_interrupt(void *info)
{
    __u32 processor;    // processor number (0-32)

    processor = smp_processor_id();

    //
    // Turn on PCE in Cr4 on this processor
    //
    /*
       if (IA32_family5 || IA32_family6 || IA32_familyF ) {
       _asm {
       pushfd
       cli
       push    eax
       _emit   0x0f   ;get current cr4 to eax
       _emit   0x20
       _emit   0x0e0
       or      eax, CR4_PCE
       _emit   0x0f   ;move eax to current cr4
       _emit   0x22
       _emit   0x0e0
       pop     eax
       popfd
       }
       }
     */

    if ((samp_info.flags & SINFO_STARTED) && (sample_method & METHOD_EBS)) {
        if (processor < MAX_PROCESSORS) {
            // On Linux*, emon regs are initialized before calling the start routine.
            // This avoids a timing problem in SaveClearIni
            samp_start_ints();
        }
        return;
    }
    // HaldStartProfileinterrupt (Source);

    return;
}

void
samp_stop_profile_interrupt(void *info)
{
    __u32 cpu;
    ULARGE_INTEGER val;

#ifdef USE_NMI
    // temporarily disable if event is precise
    if (pebs_option)
      return;
#endif

    cpu = smp_processor_id();

    if (cpu < MAX_PROCESSORS) {
        if (interlocked_exchange(&eachCPU[cpu].processor_EBS_status, 0)) {
            samp_stop_ints();
            add_to_counter_total_on_stop();
            if (pebs_option) {
                //
                // Disable PEBS and DTS for current thread
                //
                samp_disable_PEB_sand_DTS();

                //
                // Clear buffer address that we wrote to DTES reg
                //
                val = samp_read_msr(WMT_CR_DTES_AREA);
#if defined(linux32) 
                if (val.low_part == (__u32) eachCPU[cpu].DTES_buf) {
#elif defined(linux32_64)
                if (val.quad_part == (__u64) eachCPU[cpu].DTES_buf) {
#endif
                    samp_write_msr(WMT_CR_DTES_AREA, eachCPU[cpu].org_DTES_area_msr_val);
                    samp_write_msr(DEBUG_CTL_MSR, eachCPU[cpu].org_dbg_ctl_msr_val);
                    eachCPU[cpu].org_DTES_area_msr_val.quad_part = 0;
                    eachCPU[cpu].org_dbg_ctl_msr_val.quad_part = 0;
                }
            }
            return;
        }
    }
    // HaldstopProfileinterrupt (Source);

    return;
}

void
samp_stop_ints(void)
{
    samp_stop_emon();

    if (sample_method & METHOD_EBS) {
        samp_restore_cpu_vectors(); // 10-02-96
        restore_emon_regs();    // 05-31-00
    }

    return;
}

/*
 *
 *  Function: set_IA32_family_F_emon_defaults 
 *
 *  description: 
 *  Set Emon Defaults for IA32 Family F
 *
 *  Parms:
 *      entry:      None
 *  
 *      return:     None
 *
 */
void
set_IA32_family_F_emon_defaults(void)
{
    ULARGE_INTEGER i64;
 
    memset(reg_set, 0,  sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set0, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set1, 0,  sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set_init, 0,  sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    max_counter.quad_part = IA32_FAMILYF_MAX_COUNTER;

    //
    // regs to count Clockticks on T0 and
    // get counter overflow interrupt on T0
    //

    reg_set0[0].reg_num = ITLB_CR_ESCR0;    // ITLB_CR_ESCR0 0x3b6
    reg_set0[0].reg_val.low_part = 0x3000040c;  //

    reg_set0[1].options = REG_SET_COUNTER;
    reg_set0[1].reg_num = BPU_CR_PUB_COUNTER0;  // BPU_CR_PUB_COUNTER0 0x300
    i64.quad_part = 10000;  // arbitrary rate
    reg_set0[1].reg_val.quad_part = ~i64.quad_part;

    reg_set0[2].options = REG_SET_CCCR | REG_SET_CINDEX_VALID;
    reg_set0[2].c_index = 1;    // index of associated counter
    reg_set0[2].reg_num = BPU_CR_PUB_CCCR0; // BPU_CR_PUB_CCCR0 0x360
    reg_set0[2].reg_val.low_part = 0x4ff7000;   // OVF_PMI_T0

    //
    // regs to count Clockticks on T1 and
    // get counter overflow interrupt on T1
    //
    reg_set1[0].reg_num = ITLB_CR_ESCR1;    // ITLB_CR_ESCR1 0x3b7
    reg_set1[0].reg_val.low_part = 0x30000403;  // T1-OS, T1-USER

    reg_set1[1].options = REG_SET_COUNTER;
    reg_set1[1].reg_num = BPU_CR_PUB_COUNTER2;  // BPU_CR_PUB_COUNTER2 0x302
    i64.quad_part = 10000;  // arbitrary rate
    reg_set1[1].reg_val.quad_part = ~i64.quad_part;

    reg_set1[2].options = REG_SET_CCCR | REG_SET_CINDEX_VALID;
    reg_set1[2].c_index = 1;    // index of associated counter
    reg_set1[2].reg_num = BPU_CR_PUB_CCCR2; // BPU_CR_PUB_CCCR2 0x362
    reg_set1[2].reg_val.low_part = 0x8ff7000;   // OVF_PMI_T0

    return;
}

/*
 *
 *  Function: set_IA32_family6_emon_defaults 
 *
 *  description: 
 *  Set Emon Defaults for IA32 Family 6.
 *
 *  Parms:
 *      entry:      None
 *  
 *      return:     None
 *
 */
void
set_IA32_family6_emon_defaults(void)
{
    memset(reg_set, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set0, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set1, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set_init, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);

    /*
     * regs to count Clockticks
     */

    max_counter.quad_part = IA32_FAMILY6_MAX_COUNTER;   /* !!!! BUG_FIX_NEW !!!! */

    reg_set[0].reg_num = EM_MSR_PESR0;  /* EventSel0 */
    reg_set[0].reg_val.low_part = 0x530079; /* cpu cycles unhalted */

    reg_set[1].reg_num = EM_MSR_PCTR0;  /* interval = 60000... arbitrary */
    reg_set[1].reg_val.low_part = 60000;
    reg_set[1].reg_val.low_part = ~reg_set[1].reg_val.low_part;
    reg_set[1].reg_val.quad_part = ~reg_set[1].reg_val.quad_part;

    return;
}

BOOLEAN
set_event_ids(PREG_SET p_reg_set)
{
    __u32 i, j;

    //
    // Build table of unique event ID's for the sampling session. Save
    // the index to the event_ID table in the counter regset entry to
    // speed up build of event bit map when a profile interrupt occurs.
    //
    for (i = 0; i < MAX_REG_SET_ENTRIES; i++) {
        if (p_reg_set[i].options & REG_SET_COUNTER) {
            for (j = 0; j < event_count; j++) {
                if (p_reg_set[i].event_ID == event_Ids[j]) {
                    p_reg_set[i].event_I_dindex = (char) j;
                    break;
                }
            }
            //
            // Add new event_index to event_Ids table
            //
            if (j >= event_count) {
                if (event_count == MAX_ACTIVE_EVENTS) {
                    return (FALSE);
                }
                event_Ids[j] = p_reg_set[i].event_ID;
                p_reg_set[i].event_I_dindex = (char) event_count;
                event_count++;
            }
        }
    }

    return (TRUE);
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
        sp6->module_info_file_name = (char *) ((__u32_PTR) sp6 + (__u32_PTR) sp6->module_info_file_name);
        sp6->raw_sample_file_name = (char *) ((__u32_PTR) sp6 + (__u32_PTR) sp6->raw_sample_file_name);
        sp6->ptrs_are_offsets = FALSE;
    }

    sample_method = 0;
    for (i = 0; i < sp6->num_event_reg_set; i++) {
        switch (sp6->esr_set[i].command) {
        case ERS_CMD_TBS_VTD:
            if (!(pdsa->method_VTD)) {
                return (STATUS_INVALID_PARAMETER);
            }
            sample_rate_us = sp6->esr_set[i].esr_count.low_part;
            sample_method |= METHOD_VTD;
            break;
        case ERS_CMD_SET_CONFIG_AND_COUNTER_REGS:
        case ERS_CMD_WRITE_MSR:
            if (!(pdsa->method_EBS)) {
                return (STATUS_INVALID_PARAMETER);
            }
            // sample_method = SM_EBS;
            sample_method |= METHOD_EBS;
            break;
        case ERS_CMD_NOP:
            break;
        default:
            return (STATUS_INVALID_PARAMETER);
        }
    }

    pdsa->sample_rec_length = sizeof(sample_record_PC);
    sample_tsc = FALSE;
    if (sp6->sample_TSC)
    {
      sample_tsc_offset = pdsa->sample_rec_length;
      sample_tsc = TRUE;
      pdsa->sample_rec_length += 8;
    }

#if defined(ALLOW_LBRS)

    //
    // Only thing left now is figuring out how the user code
    // will pass the request to make capture_lbrs become true...
    //
    if (capture_lbrs) {
	pdsa->sample_rec_length += LBR_SAVE_SIZE;
	quick_freeze_msr = DEBUG_CTL_MSR;
    }
#endif

    sample_rec_length = pdsa->sample_rec_length;

    //
    //  Zero the reg_set array
    //
    memset(reg_set, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set0, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set1, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    memset(reg_set_init, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
    pebs_option = FALSE;

    if (sample_method & METHOD_EBS) {
        __u32 i, j, tot, t0, t1, init, msr;
        ULARGE_INTEGER msr_val; // value to be written to msr register

        if (IA32_familyF) {
            if (sp6->num_event_reg_set) {

                //
                // Copy registers from samp_parms to local regSet array
                //

                for (i = 0, j = 0, t0 = 0, t1 = 0, init = 0, tot = 0; (i < sp6->num_event_reg_set)
                     && (tot < MAX_REG_SET_ENTRIES); ++i) {
                    switch (sp6->esr_set[i].command) {
                    case ERS_CMD_WRITE_MSR:
                        VDK_PRINT_DEBUG("esr_set[%d]  msr 0x%x  val 0x%x \n", i, sp6->esr_set[i].esr_value, sp6->esr_set[i].esr_count);
                        tot++;
                        msr = sp6->esr_set[i].esr_value;
                        msr_val = sp6->esr_set[i].esr_count;
                        switch (IA32_family_F_reg_type(msr)) {
                        case ESCR_REG:
                            break;  // write ESCR's only once per package
                        case CCCR_REG:
                            // the below if - else if statement ensures that the IP collected 
                            //  relates to the h/w thread that caused the interrupt
                            // other models could be supported in the future
                            if (msr_val.low_part & CCCR_OVF_PMI_T0_MASK) {
                                reg_set0[t0].reg_num = msr;
                                reg_set0[t0].reg_val = msr_val;
                                reg_set0[t0].options = REG_SET_CCCR;
                                t0++;
                            } else {
                                if (msr_val.low_part & CCCR_OVF_PMI_T1_MASK) {
                                    reg_set1[t1].reg_num = msr;
                                    reg_set1[t1].reg_val = msr_val;
                                    reg_set1[t1].options = REG_SET_CCCR;
                                    t1++;
                                } else {
                                    //
                                    // If neither OVF_PMI_T0 or OVF_PMI_T1 is on then
                                    // it might be a precise event.
                                    // IQ_CR_PUB_CCCR4 is for T0 precise events and
                                    // IQ_CR_PUB_CCCR5 is for T1 precise events.
                                    //
                                    // If other CCCR reg is specified without either OVF T0/T1 set,
                                    // then add it to the "Init" regs which will only be written once
                                    // on each cpu when sampling is started.
                                    //
                                    if (msr == IQ_CR_PUB_CCCR4) {
                                        reg_set0[t0].reg_num = msr;
                                        reg_set0[t0].reg_val = msr_val;
                                        reg_set0[t0].options = REG_SET_CCCR;
                                        t0++;
                                    } else {
                                        if (msr == IQ_CR_PUB_CCCR5) {
                                            reg_set1[t1].reg_num = msr;
                                            reg_set1[t1].reg_val = msr_val;
                                            reg_set1
                                                [t1].options = REG_SET_CCCR;
                                            t1++;
                                        } else {
                                            reg_set_init
                                                [init].reg_num = msr;
                                            reg_set_init
                                                [init].reg_val = msr_val;
                                            init++;
                                        }
                                    }
                                }
                            }
                            continue;
                            break;
                        case COUNT_REG:
                            reg_set[j].reg_num = msr;
                            reg_set[j].event_ID = (char) sp6->esr_set[i].event_ID;
                            reg_set[j].event_inc = msr_val;
                            if (!msr_val.quad_part) {
                                reg_set[j].
                                    event_inc.quad_part = IA32_FAMILYF_MAX_COUNTER;
                            }
                            //
                            // Negate the initial count value so that we get a counter
                            // overflow interrupt (or PEBS record) after the requested
                            // number of occurences of the event.
                            //
                            // On Pentium(R) 4 processor, the counter overflow interrupt
			    // takes place on the next occurrence of the event following
                            // the counter overflow.
                            // To account for this we add 2 to the negated value.
                            //
                            // To sample one each event, we should set the FORCE_OVF bit
                            // in the associated CCCR. This will cause an onverflow
                            // in on each increment of the counter.
                            // We'll do this in the future... 07-21-00 (already done now?)
                            //

                            if (msr_val.quad_part) {
                                if (msr_val.quad_part == 1) {
                                    msr_val.quad_part = (~msr_val.quad_part)
                                        + 1;
                                } else {
                                    msr_val.quad_part = (~msr_val.quad_part)
                                        + 2;
                                }
                            }
                            msr_val.quad_part &= IA32_FAMILYF_MAX_COUNTER;
                            reg_set[j].reg_val = msr_val;
                            j++;
                            continue;
                            break;
                        case PEBS_REG:
                            if (msr == CRU_CR_PEBS_MATRIX_HORIZ) {
                                if (msr_val.low_part & ENABLE_EBS_MY_THR) {
                                    if (DTES_supported) {
                                        pebs_option = TRUE;
                                    } else {
                                        return (STATUS_NOT_SUPPORTED);
                                    }
                                }
				//BSBS this is not a bug
				break;
                                reg_set0[t0].reg_num = msr;
                                reg_set0[t0].reg_val = msr_val;
                                t0++;
                                reg_set1[t1].reg_num = msr;
                                reg_set1[t1].reg_val = msr_val;
                                t1++;
                                continue;
                            }
                            break;
                        default:
                            break;
                        }
                        //
                        // If we could not determine that an msr was thread specific,
                        // then put the msr in the reSetInit array which contains
                        // registers that will only be written once per OS cpu at the
                        // start of sampling
                        //
                        reg_set_init[init].reg_num = msr;
                        reg_set_init[init].reg_val = msr_val;
                        init++;
                        break;
                    case ERS_CMD_NOP:
                    default:
                        break;
                    }
                }
                if ((i < sp6->num_event_reg_set)
                    && (tot == MAX_REG_SET_ENTRIES)) {
                    return (STATUS_NOT_SUPPORTED);
                }
                //
                // At this point the reg_set array has only counter type registers
                // and we don't know whether they belong in the T0 array or T1 array.
                //
                // Look for the corresponding CCCR for the counter and put the counter in
                // same array (T0 or T1) as the CCCR.
                //

                for (i = 0; (i < j) && reg_set[i].reg_num; i++) {
                    __u32 k;
                    BOOLEAN CCCR_FOUND;

                    CCCR_FOUND = FALSE;
                    msr = reg_set[i].reg_num;   // msr = count reg
                    msr += 0x60;    // msr = CCCR reg that corresponds to count reg
                    for (k = 0; k < MAX_REG_SET_ENTRIES; k++) {
                        if (msr == reg_set0[k].reg_num) {
                            reg_set0[t0].reg_num = reg_set[i].reg_num;
                            reg_set0[t0].reg_val = reg_set[i].reg_val;
                            reg_set0[t0].event_ID = reg_set[i].event_ID;
                            reg_set0[t0].event_inc = reg_set[i].event_inc;
                            reg_set0[t0].options = REG_SET_COUNTER;
                            reg_set0[k].c_index = (char) t0;
                            reg_set0[k].options |= REG_SET_CINDEX_VALID;
                            t0++;
                            CCCR_FOUND = TRUE;
                            break;
                        }
                    }
                    for (k = 0; k < MAX_REG_SET_ENTRIES; k++) {
                        if (msr == reg_set1[k].reg_num) {
                            reg_set1[t1].reg_num = reg_set[i].reg_num;
                            reg_set1[t1].reg_val = reg_set[i].reg_val;
                            reg_set1[t1].event_ID = reg_set[i].event_ID;
                            reg_set1[t1].event_inc = reg_set[i].event_inc;
                            reg_set1[t1].options = REG_SET_COUNTER;
                            reg_set1[k].c_index = (char) t1;
                            reg_set1[k].options |= REG_SET_CINDEX_VALID;
                            t1++;
                            CCCR_FOUND = TRUE;
                            break;
                        }
                    }
                    for (k = 0; k < MAX_REG_SET_ENTRIES; k++) {
                        if (msr == reg_set_init[k].reg_num) {
                            reg_set_init[init].reg_num = reg_set[i].reg_num;
                            reg_set_init[init].reg_val = reg_set[i].reg_val;
                            reg_set_init[init].event_ID = reg_set[i].event_ID;
                            reg_set_init[init].event_inc = reg_set[i].event_inc;
                            reg_set_init[init].options = REG_SET_COUNTER;
                            reg_set_init[k].c_index = (char) init;
                            reg_set_init[k].options |= REG_SET_CINDEX_VALID;
                            init++;
                            CCCR_FOUND = TRUE;
                            break;
                        }
                    }
                    if (!CCCR_FOUND) {
                        return (STATUS_NOT_SUPPORTED);
                    }
                }
                //
                // If we put the PEBS enable reg in a thread array with
                // no other registers, then it is probably a mistake and
                // the caller only intended to enable PEBS on the other thread.
                //
                if (t0 == 1) {
                    if (reg_set0[0].reg_num == CRU_CR_PEBS_MATRIX_HORIZ) {
			memset(reg_set0, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
                    }
                }
                if (t1 == 1) {
                    if (reg_set1[0].reg_num == CRU_CR_PEBS_MATRIX_HORIZ) {
		        memset(reg_set1, 0, sizeof (REG_SET) * MAX_REG_SET_ENTRIES);
                    }
                }
            }
        } else {
            __u32 event_sel_reg, perf_ctr_reg;
            // emon_cesr = sp6->esr_set[0].esr_value;
            // emon_rate = sp6->esr_set[0].esr_count.low_part;
            if (IA32_family6) {

                if (sp6->num_event_reg_set) {
                    for (i = 0, j = 0, tot = 0; (i < sp6->num_event_reg_set)
                         && (tot < (MAX_REG_SET_ENTRIES - 1)); ++i) {
                        switch (sp6->esr_set[i].command) {
                        case ERS_CMD_WRITE_MSR:
                            reg_set[j].reg_num = sp6->esr_set[i].esr_value;
                            reg_set[j].reg_val = sp6->esr_set[i].esr_count;
                            reg_set[j].event_ID = (char) sp6->esr_set[i].event_ID;
                            if ((reg_set[j].reg_num == EM_MSR_PCTR0)
                                || (reg_set[j].reg_num == EM_MSR_PCTR1)) {
                                reg_set[j].event_inc = reg_set[j].reg_val;
                                if (reg_set[j].reg_val.quad_part) {
                                    reg_set
                                        [j].
                                        reg_val.
                                        quad_part
                                        =
                                        ((~reg_set[j].reg_val.quad_part) +
                                         1) & IA32_FAMILY6_MAX_COUNTER;
                                } else {
                                    reg_set
                                        [j].
                                        event_inc.
                                        quad_part = IA32_FAMILY6_MAX_COUNTER;
                                }
                                reg_set[j].options = REG_SET_COUNTER;
                            }
                            j++;
                            tot++;
                            break;
                        case ERS_CMD_SET_CONFIG_AND_COUNTER_REGS:
                            if (sp6->esr_set[i].data == 0) {
                                event_sel_reg = EM_MSR_PESR0;
                                perf_ctr_reg = EM_MSR_PCTR0;
                            } else {
                                if (sp6->esr_set[i].data == 1) {
                                    event_sel_reg = EM_MSR_PESR1;
                                    perf_ctr_reg = EM_MSR_PCTR1;
                                } else {
                                    return (STATUS_NOT_SUPPORTED);
                                }
                            }
                            reg_set[j].reg_num = event_sel_reg;
                            reg_set[j].reg_val.low_part = sp6->esr_set[i].esr_value;
                            j++;
                            tot++;
                            reg_set[j].reg_num = perf_ctr_reg;
                            reg_set[j].reg_val = sp6->esr_set[i].esr_count;
                            reg_set[j].event_ID = (char) sp6->esr_set[i].event_ID;
                            reg_set[j].event_inc = reg_set[j].reg_val;
                            if (reg_set[j].reg_val.quad_part) {
                                reg_set[j].
                                    reg_val.quad_part = ((~reg_set[j].reg_val.quad_part)
                                             +
                                             1) & IA32_FAMILY6_MAX_COUNTER;
                            } else {
                                reg_set[j].
                                    event_inc.quad_part = IA32_FAMILY6_MAX_COUNTER;
                            }
                            reg_set[j].options = REG_SET_COUNTER;
                            j++;
                            tot++;
                            break;
                        case ERS_CMD_NOP:
                            break;
                        default:
                            return (STATUS_NOT_SUPPORTED);
                        }
                    }
                    if ((i < sp6->num_event_reg_set)
                        && (tot == MAX_REG_SET_ENTRIES)) {
                        return (STATUS_NOT_SUPPORTED);
                    }
                }
            } else {
                if (IA32_family5) {
                    if (sp6->num_event_reg_set) {
                        for (i = 0, j = 0, tot = 0; (i < sp6->num_event_reg_set)
                             && (tot < (MAX_REG_SET_ENTRIES - 1)); ++i) {
                            switch (sp6->esr_set[i].command) {
                            case ERS_CMD_WRITE_MSR:
                                reg_set[j].reg_num = sp6->esr_set[i].esr_value;
                                reg_set[j].reg_val = sp6->esr_set[i].esr_count;
                                reg_set[j].event_ID = (char) sp6->esr_set[i].event_ID;
                                if ((reg_set[j].reg_num == EM_MSR_CTR0)
                                    || (reg_set[j].reg_num == EM_MSR_CTR1)) {
                                    reg_set[j].event_inc = reg_set[j].reg_val;
                                    if (reg_set[j].reg_val.quad_part) {
                                        reg_set
                                            [j].
                                            reg_val.
                                            quad_part
                                            =
                                            ((~reg_set[j].reg_val.quad_part) +
                                             1) & IA32_FAMILY5_MAX_COUNTER;
                                    } else {
                                        reg_set
                                            [j].
                                            event_inc.
                                            quad_part
                                            = IA32_FAMILY5_MAX_COUNTER;
                                    }
                                    reg_set[j].options = REG_SET_COUNTER;
                                }
                                j++;
                                tot++;
                                break;
                            case ERS_CMD_SET_CONFIG_AND_COUNTER_REGS:
                                if (sp6->esr_set[i].data == 0) {
                                    event_sel_reg = EM_MSR_CESR;
                                    perf_ctr_reg = EM_MSR_CTR0;
                                } else {
                                    if (sp6->esr_set[i].data == 1) {
                                        event_sel_reg = EM_MSR_CESR;
                                        perf_ctr_reg = EM_MSR_CTR1;
                                    } else {
                                        return (STATUS_NOT_SUPPORTED);
                                    }
                                }
                                reg_set[j].reg_num = event_sel_reg;
                                reg_set[j].reg_val.low_part = sp6->esr_set[i].esr_value;
                                j++;
                                tot++;
                                reg_set[j].reg_num = perf_ctr_reg;
                                reg_set[j].reg_val = sp6->esr_set[i].esr_count;
                                reg_set[j].event_ID = (char) sp6->esr_set[i].event_ID;
                                reg_set[j].event_inc = reg_set[j].reg_val;
                                if (reg_set[j].reg_val.quad_part) {
                                    reg_set
                                        [j].
                                        reg_val.
                                        quad_part
                                        =
                                        ((~reg_set[j].reg_val.quad_part) +
                                         1) & IA32_FAMILY5_MAX_COUNTER;
                                } else {
                                    reg_set
                                        [j].
                                        event_inc.
                                        quad_part = IA32_FAMILY5_MAX_COUNTER;
                                }
                                reg_set[j].options = REG_SET_COUNTER;
                                j++;
                                tot++;
                                break;
                            case ERS_CMD_NOP:
                                break;
                            default:
                                return (STATUS_NOT_SUPPORTED);
                            }
                        }
                        if ((i < sp6->num_event_reg_set)
                            && (tot == MAX_REG_SET_ENTRIES)) {
                            return (STATUS_NOT_SUPPORTED);
                        }
                    }
                } else {
                    return (STATUS_NOT_SUPPORTED);
                }
            }
        }
    }
    //
    // Build table of unique event IDs that are active for this sampling session
    //
    memset(event_Ids, 0, sizeof (event_Ids));
    event_count = 0;
    if (!set_event_ids(reg_set_init)) {
        return (STATUS_INVALID_PARAMETER);
    }
    if (!set_event_ids(reg_set)) {
        return (STATUS_INVALID_PARAMETER);
    }
    if (!set_event_ids(reg_set0)) {
        return (STATUS_INVALID_PARAMETER);
    }
    if (!set_event_ids(reg_set1)) {
        return (STATUS_INVALID_PARAMETER);
    }

    return (STATUS_SUCCESS);
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





/*++

Routine description:

    Handle cpu counter overflow interrupt. This routine is called
    with the SamplLock held and cpu emon counters stopped.

Arguments:

return value:

    none

--*/
/* ------------------------------------------------------------------------- */
/*!
 * @fn          u32 samp_emon_interrupt (PINT_FRAME int_frame) 
 * @brief       Routine that gets space and calls another to fill in sample record
 *
 * @param       int_frame IN  - Contins cs, eip,  and eflags
 *
 * @return	if buffer is full, returns true to wake up writer thread
 *
 * This routine finds the space for the sample record and calls another
 * routine to actually do the work. If the buffer area is full, this
 * rotuine will return true to tell the caller to wake the writer thread.
 *
 */
u32
samp_emon_interrupt(PINT_FRAME int_frame)
{
    __u32 i, events = 0, wake_up_thread = FALSE;
    BOOLEAN sr_built;
    P_sample_record_PC p_sample = NULL, p_sample0 = NULL;

    if (IA32_family6) {
        events = samp_check_emon_counter_overflow_IA32_family6();
    } else {
        if (IA32_familyF) {
            events = samp_check_emon_counter_overflow_IA32_familyF();
        } else {
            if (IA32_family5) {
                events = samp_check_emon_counter_overflow_IA32_family5();
            }
        }
    }

    if (check_pause_mode()) {
        sample_skipped();
        return(wake_up_thread);
    }

    for (i = 0, sr_built = FALSE; events && (i < MAX_ACTIVE_EVENTS); i++, events = events >> 1) {
        if (events & 1) {
            p_sample = samp_get_buf_space(sample_rec_length, &wake_up_thread);
            if (p_sample) {
                //
                // Build sample record once for first event found
                // and copy that record for each additional event that occured
                //
                if (!sr_built) {
                    sr_built = TRUE;
                    p_sample0 = p_sample;
                    samp_build_csip_sample(int_frame, p_sample);
                } else {
                    __u32 j;
                    char *psrc, *pdest;

                    // memcpy(p_sample, &sr, sizeof(sr)); 
                    psrc = (char *) p_sample0;
                    pdest = (char *) p_sample;
                    for (j = 0; j < sample_rec_length; j++) {
                        *pdest++ = *psrc++;
                    }
                }
                p_sample->event_index = event_Ids[i];
            } else {
                break;
            }
        }
    }

    return(wake_up_thread);
}

/* ------------------------------------------------------------------------- */
/*!
 * @fn          void __outbyte(__u32 port, __u8 value)
 * @brief       Write a value to the the io port
 *
 * @return	none
 *
 * Use the compiler for this, but beware, the args are reversed!
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 * @todo Could inline this too...
 *
 */
void
__outbyte(__u32 port, __u8 value)
{
    outb(value, port);

    return;
}


/* ------------------------------------------------------------------------- */
/*!
 * @fn          __u8 __inbyte(__u32 port)
 * @brief       Read a value from the io port
 *
 * @return	none
 *
 * Use the compiler for this...
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 * @todo Could inline this too...
 *
 */
__u8
__inbyte(__u32 port)
{
    return inb(port);
}


#if defined(ALLOW_LBRS)
/* ------------------------------------------------------------------------- */
/*!
 * @fn          void disable_lbr_capture (void) 
 * @brief       Disable capturing of the lbr information
 *
 * @return	none
 *
 * Other routines in other C modules might want to disable the lbrs, but
 * only this C file will be enabling it, so no need to have a corresponding
 * enable_lbr_capture() routine...
 * 
 *
 * <I>Special Notes:</I>
 *
 *	None
 *
 *
 */
void
disable_lbr_capture(void)
{
    capture_lbrs = FALSE;
    quick_freeze_msr = 0;

    return;
}
#endif
