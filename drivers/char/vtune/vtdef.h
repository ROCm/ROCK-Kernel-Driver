/*
 *  vtdef.h
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
 *	File: vtdef.h
 *
 *	Description: 
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#if !defined(_VTUNE_DEF_H)
#define _VTUNE_DEF_H

#include "vtypes.h"
#ifdef linux64
#ifdef PERFMON_SUPPORTED
#include <linux/interrupt.h>
#include <asm/hw_irq.h>
#endif // PERFMON_SUPPORTED
#else
#include "familyf_msr.h"
#endif
#ifdef KERNEL_26X
#include <linux/timer.h>
#endif

#define SYS_VERSION_MAJOR               0
#define SYS_VERSION_MINOR               921002

#define  PCMINOR                        0   /* /dev/vtune minor number */
#define  MDMINOR                        1   /* /dev/vtune_m */
#define  PIDMINOR                       2   /* /dev/vtune_p */

#define VFLAG_OFF                       0
#define VFLAG_ON                        1
#define SAMPMODE_MANUAL                 -1

#define XHDRM_SAMPLE_QTY                2
#define P6_FAMILY                       6

#ifdef PERFMON_SUPPORTED
#define VTUNE_PERFMON_IRQ               IA64_PERFMON_VECTOR
#else
#define VTUNE_PERFMON_IRQ               0xED
#endif

#define CPUFAMILY                       0xF00
#define CPUMODEL                        0xF0

#define emask_t unsigned long

/*  NEW */
#define APIC_PERFLVT_FIXED_ENA          0x00000 /* fixed mode; "should" be 0 according to Pentium(R) 4 processor manual (was 0x02000) */
#define APIC_PERFLVT_INT_MASK           0x10000 /* mask perf interrupt 0=ints not masked, 1=perf ints masked */
#define APIC_TIMERLVT_ENA               0x22000 /* local apic timer enable */

#define APIC_BASE_MSR        0x1B   // apic base msr
#define APIC_BASE_MSR_ENABLE 0x800  // apic enable/disable bit

/*---------------------------------------------------------------*/
/*
 *         performance Monitor Model Specific registers
 */
/*---------------------------------------------------------------*/

#define EM_MSR_TSC         0x10 /* Time Stamp Counter */
#define EM_MSR_CESR        0x11 /* control and Event Select */
#define EM_MSR_CTR0        0x12 /* Counter 0 */
#define EM_MSR_CTR1        0x13 /* Counter 1 */
/*** P6 Code ***/
/** P6 MSR's **/
#define EM_MSR_PESR0        0x186   /* performance Event Select */
#define EM_MSR_PESR1        0x187   /* performance Event Select */
#define EM_MSR_PCTR0        0x0C1   /* P6 Counter 0 */
#define EM_MSR_PCTR1        0x0C2   /* P6 Counter 1 */

/* 
 *  PESR mask controls for Intel(R) Pentium(R) Pro, Pentium(R) II, 
 *  and Pentium(R) III processors
 *
 *        33222222 2 2 2 2 1 1 1 1 11111100 00000000
 *        10987654 3 2 1 0 9 8 7 6 54321098 76543210
 *       +--------+-+-+-+-+-+-+-+-+--------+--------+
 *       |        |I|E|r|I|P|E|O|U|        |        |
 *       |  CMSK  |N|N|s|N|C| |S|S|  UMSK  |   ES   |
 *       |        |V| |v|T| | | |E|        |        |
 *       |        | | |d| | | | |R|        |        |
 *       +--------+-+-+-+-+-+-+-+-+--------+--------+
 */

#define EM_PERF_DISABLE         0x0003FFFF
#define EM_ES_MASK              0x000000FF
#define EM_UMSK_MASK            0x0000FF00
#define EM_USER_MASK            0x00010000
#define EM_OS_MASK              0x00020000
#define EM_E_MASK               0x00040000
#define EM_PC_MASK              0x00080000
#define EM_INT_MASK             0x00100000
#define EM_EN_MASK              0x00400000
#define EM_INV_MASK             0x00800000
#define EM_CMSK_MASK            0xFF000000

#ifdef linux64

#define itp_read_reg_pmc(a)     ia64_get_pmc(a)
#define itp_read_reg_pmd(a)     ia64_get_pmd(a)
#define itp_read_reg_pmv()      itp_get_pmv()

#define itp_write_reg_pmc(a, b) ia64_set_pmc(a, b)
#define itp_write_reg_pmd(a, b) ia64_set_pmd(a, b)
#ifdef KERNEL_26X
#define itp_write_reg_pmv(a) \
    asm volatile ("mov cr.pmv=%0" :: "r"(a) : "memory")
#else
#define itp_write_reg_pmv(a)    ia64_set_pmv(a)
#endif

#define itp_get_cpuid(a)        ia64_get_cpuid(a)
#define itp_srlz_d()            ia64_srlz_d()

#ifdef KERNEL_26X
static inline __u64
ia64_get_dcr (void)
{
	__u64 r;
	asm volatile ("mov %0=cr.dcr" : "=r"(r));
	return r;
}

static inline void
ia64_set_dcr (__u64 val)
{
	asm volatile ("mov cr.dcr=%0;;" :: "r"(val) : "memory");
	ia64_srlz_d();
}
#endif
#define itp_get_dcr()           ia64_get_dcr()
#define itp_set_dcr(a)          ia64_set_dcr(a)

static __inline__ unsigned long
itp_get_itc(void)
{
    unsigned long result;

    __asm__ __volatile__("mov %0=ar.itc":"=r"(result)::"memory");
    return result;
}

static inline __u64
itp_get_pmv(void)
{
    __u64 r;
      __asm__("mov %0=cr.pmv":"=r"(r));
    return r;
}

//
// GDT entry
//

typedef struct _KGDTENTRY {
    __u16 limit_low;
    __u16 base_low;
    union {
        struct {
            char base_mid;
            char flags1;    // Declare as bytes to avoid alignment
            char flags2;    // Problems.
            char base_hi;
        } bytes;
        struct {
            __u32 base_mid:8;
            __u32 type:5;
            __u32 dpl:2;
            __u32 pres:1;
            __u32 limit_hi:4;
            __u32 sys:1;
            __u32 reserved_0:1;
            __u32 default_big:1;
            __u32 granularity:1;
            __u32 base_hi:8;
        } bits;
    } high_word;
} KGDTENTRY, *PKGDTENTRY;

#define TYPE_TSS    0x01    // 01001 = NonBusy TSS
#define TYPE_LDT    0x02    // 00010 = LDT

//
// UnScrambled Descriptor format
//
typedef struct _KDESCRIPTOR_UNSCRAM {
    union {
        __u64 descriptor_words;
        struct {
            __u64 base:32;
            __u64 limit:20;
            __u64 type:5;
            __u64 dpl:2;
            __u64 pres:1;
            __u64 sys:1;
            __u64 reserved_0:1;
            __u64 default_big:1;
            __u64 granularity:1;
        } bits;
    } words;
} KXDESCRIPTOR, *PKXDESCRIPTOR;

#define TYPE_CODE_USER                0x1A  // 0x11011 = Code, Readable, Accessed
#define TYPE_DATA_USER                0x13  // 0x10011 = Data, Readwrite, Accessed

#define DESCRIPTOR_EXPAND_DOWN        0x14
#define DESCRIPTOR_DATA_READWRITE     (0x8|0x2) // Data, Read/write

#define DPL_USER    3
#define DPL_SYSTEM  0

#define GRAN___u8   0
#define GRAN_PAGE   1

#define SELECTOR_TABLE_INDEX 0x04

#endif  // linux64


// Defines for Linux* OS list functions
#define LIST_ENTRY   struct list_head
#define PLIST_ENTRY  LIST_ENTRY *

#define non_paged_pool 0

/*
 * ===========================================================================
 *      5-04-01 - TVK (New Model Port ) 
 * ===========================================================================
 */

#define STATUS_SUCCESS           0
#define STATUS_INVALID_PARAMETER -EINVAL
#define STATUS_NOT_SUPPORTED     -ENOSYS
#define STATUS_DEVICE_BUSY       -EBUSY

#define MAX_ACTIVE_EVENTS        32

#define MAX_PROCESSORS           64

#define MAX_REG_SET_ENTRIES      120

#define IA32_FAMILY5_MAX_COUNTER 0x0ffffffffffULL  // max counter on P5... cpu counters are 40 bits
#define IA32_FAMILY6_MAX_COUNTER 0x0ffffffffffULL  // max counter on P6... cpu counters are 40 bits
#define IA32_FAMILYF_MAX_COUNTER 0x0ffffffffffULL  // max counter on Pentium(R) 4 processor ... cpu counters are 40 bits

typedef struct _REG_SET {
    __u8 options;
    __u8 pmc_num;
    union {
        __u8 c_index;   // if this is a CCCR, then ctrindex is the
        // index for the associated counter... 
        // reg_setx[ctrindex] is associated counter

        __u8 event_I_dindex;    // index of this event in the event ID table
    };

    __u8 event_ID;      // event ID passed to SampConfigure routine
    // in event_reg_set_ex structure
    __u32 reg_num;

    __u32 c_ovf[MAX_PROCESSORS];    // counter overflow status for
    // each cpu... non zero = xounter
    // has overflowed on that cpu
    ULARGE_INTEGER reg_val;

    ULARGE_INTEGER event_inc;   // value added to event_total on counter
    // overflow interrupt      03-30-01

    ULARGE_INTEGER event_total[MAX_PROCESSORS]; // event total from start of
    // sampling session   03-30-01
} REG_SET, *PREG_SET;

// Defines for REG_SET options field
#define REG_SET_CCCR            0x01    // this entry is a CCCR
#define REG_SET_COUNTER         0x02    // this entry is a counter
#define REG_SET_CINDEX_VALID    0x04    // indexToCtr is set

// Defines for bits of sample method                              09-10-00
#define METHOD_VTD     0x01
#define METHOD_RTC     0x02
#define METHOD_EBS     0x04

/* User allocation req's for SP structs */
typedef struct {
    unsigned int size_user_Sp3;
    unsigned int strlen_raw_file_name_Sp3;
    unsigned int strlen_mod_info_file_name_Sp3;
} samp_user_config_stat_t;

#define ITP_CPUID_REG3_FAMILY   0xFF000000
#define ITP_CPUID_REG3_MODEL   0xFF0000
#define ITP_CPU_FAMILY_ITANIUM  7
#define ITP_CPU_FAMILY_ITANIUM2 0x1F

//
// These correspond to the slot for the bundle that was interrupted
// OS/Apps are supposed to look here for slot info, but often the assumption
// is that the slot is the bottom 2 bits of the IIP... Sometimes driver updates
// the IIP to have that. Sometimes it doesn't. We're pretty good about keeping
// the RI bits accurate though...
//
#define ITP_IPSR_RI_SHIFT           41
#define ITP_IPSR_RI_MASK            (((__u64) 3) << ITP_IPSR_RI_SHIFT)

#define ITANIUM_INSTRUCTION_EAR_PMC 10
#define ITANIUM_DATA_EAR_PMC        11
#define ITANIUM_BRANCH_TRACE_BUFFER_PMC 10
#define ITANIUM_MAXCOUNTER          0xFFFFFFFF
#define ITANIUM_MAX_CONFIG_PMC      13
#define ITANIUM_PMD17_SLOT_MASK     0xC
#define ITANIUM_PMD17_SLOT_SHIFT    2
#define ITANIUM_PMD17_BUNDLE_MASK   0
#define ITANIUM_PMC_ES_MASK         0x7F00
#define ITANIUM_PMC_ES_SHIFT        8
#define ITANIUM_DEAR_EVENT_CODE     0x67
#define ITANIUM_IEAR_EVENT_CODE     0x23
#define ITANIUM_BTRACE_EVENT_CODE   0x11

#define ITANIUM2_MAXCOUNTER         0x7FFFFFFFFFFF
#define ITANIUM2_MAX_CONFIG_PMC     15
#define ITANIUM2_PMD17_SLOT_MASK    0x3
#define ITANIUM2_PMD17_SLOT_SHIFT   0
#define ITANIUM2_PMD17_BUNDLE_MASK  0x4
#define ITANIUM2_PMC_ES_MASK        0xFF00
#define ITANIUM2_PMC_ES_SHIFT       8
#define ITANIUM2_DEAR_EVENT_CODE    0xC8
#define ITANIUM2_IEAR_EVENT_CODE    0x43
#define ITANIUM2_BTRACE_EVENT_CODE  0x11

#define ITANIUM2_PMC12_DS_MASK      (1 << 7)

#define ITANIUM2_PMD16_BBI_MASK     0x07
#define ITANIUM2_PMD16_FULL_MASK    0x08

#define ITANIUM2_BTB_SLOT_SHIFT     2
#define ITANIUM2_BTB_SLOT_MASK      (3 << ITANIUM2_BTB_SLOT_SHIFT)

#define ITANIUM2_BTB_MP_MASK        0x02
#define ITANIUM2_BTB_B_MASK         0x01

#define ITANIUM2_NUM_BTBREGS        8

#define PMC0_FREEZE   1
#define PMC0_OFLOW0   0x10  // perf counter 4 overflow status bit
#define PMC0_OFLOW1   0x20  // perf counter 5 overflow status bit
#define PMC0_OFLOW2   0x40  // perf counter 6 overflow status bit
#define PMC0_OFLOW3   0x80  // perf counter 7 overflow status bit
#define PMC0_OFLOWALL (PMC0_OFLOW0 | PMC0_OFLOW1 | PMC0_OFLOW2 | PMC0_OFLOW3)   // perf counter 4-7 overflow status bit

#define PMV_MASK_BIT 0x10000    // pmv.m  0 = unmask counter overflow interrrupts
                //                        1 = mask counter overflow interrupts
                // Note: 
                // Counter overflow interrupt is edge triggered so
                // is an counter overflow interrupt signal is generated while
                // pmv.m is set, then the interrupt is lost

#define PMC4_PLM  0x0F      // privilege level mask
#define PMC4_OI   0x20      // overflow interrupt 0 = no interrupt, 1 = generate counter overflow interrupt

// TBS global data

/* CPU information (originally in include/sampfile.h) */

// in the future, we should store the multiple register values returned from 
//  an EAX input of 2 when trying to determine the L2 cache size of a processor

// added by Bob Knight, 08-17-2001
typedef struct cpu_information_s {
  __u32 ul_cpu_type_from_shell;   // the CTF enum from the shell
  __u32 ul_num_EAX_inputs;    // the highest number of EAX inputs supported by the cpu (determined by setting EAX to 0 and executing cpuid AND by execuing the cpuid instruction with an input of 2 to discover how many times to execute cpuid with an input of 2 to get the L2 cache size)
  __u32 ul_num_EAX_extended_inputs;   // the highest number of extended EAX inputs supported by the cpu (determined by setting EAX to 0x80000000 and executing cpuid)
  __u32 ul_num_cpus_available;   // the maximum number of physical cpus (not packages) available to be used by the OS (this number may be less than the number of physical logical cpus installed on the system if the system is configured to use less than the total number of available physical cpus)
  __u32 ul_cpu_speed_in_M_hz[MAX_PROCESSORS]; // raw speed of the cpu in MHz (may not be politically correct speed advertised to users - e.g. a 299 MHz cpu would be called a 300MHz cpu publically)
  __u32 ul_cpul2_cache_size[MAX_PROCESSORS];  // in Kilobytes
  __u32 ul_offset_to_cpu_map_array;   // offset from the beginning of this structure to an array of ul_num_CP_us_available cpu_map structures
  __u32 ul_offset_to_cpuid_output_array;  // offset from the beginning of this structure to an array of ul_num_CP_us_available*(ul_num_EAX_inputs+ul_num_EAX_extended_inputs) cpuid_output structures
  __u32 ul_reserved[8];
} cpu_information;

/* Sampling system info (originally in include/vtuneshared.h) */
typedef struct _sys_samp_info {
  int cpu_I_dmap[MAX_PROCESSORS];  // CPU ID info for all  processors
  int cpu_feature_bits;       // feature bits, global for all now
  int num_processors;         // Total CPUs detected
  int cpu_speed;              // CPU Speed
  int lib_version_major;      // Sampling library Version Major
  int lib_version_minor;      // Sampling library Version Minor
  int sysdrv_version_major;   // Sampling driver Version Major
  int sysdrv_version_minor;   // Sampling driver Version Minor
  int feature_set;            // features supported by CPU and OS
  int kernel_cs;              // kernel selector (see __KERNEL_CS in asm-i386/segment.h)
  int reservedC;              // Reserved Field C (TBD)
  int reservedD;              // Reserved Field D (TBD)
  int reservedE;              // Reserved Field E (TBD)
} sys_samp_info;

typedef struct _PER_CPU {
  __u32 processor_status;
  __u32 processor_EBS_status;
  BOOLEAN start_all;
#ifdef linux64
  __u64 original_apic_perf_local_vector;
#else
  __u32 unused_ntprocessorcontrolregister;
  __u32 original_apic_perf_local_vector;
#ifdef linux32
  __u64 original_EBS_idt_entry;
  PDTS_BUFFER DTES_buf;
#elif defined(linux32_64)
  void *original_EBS_idt_entry;
  PIA32EM64T_DTS_BUFFER DTES_buf;
#else
#error Unknown architecture
#endif
  BOOLEAN resume_sampling;
  void *samp_EBS_idt_routine;
  ULARGE_INTEGER org_dbg_ctl_msr_val;
  ULARGE_INTEGER org_DTES_area_msr_val;
  PULARGE_INTEGER p_emon_reg_save_area; // pointer to reg save area per cpu.        05-31-00
                                        // used to save/restore emon regs across
                                        // an ebs session
  __u32 unused_emonints;
#endif
} PER_CPU, *PPER_CPU;

extern PER_CPU eachCPU[MAX_PROCESSORS];

#if defined(linux32) || defined(linux32_64)
extern __u32 package_status[MAX_PROCESSORS + 1];   // add 1 since packageNumber[] is
                                            // indexed by the package number
                                            // which is relative to 1
#endif

#if defined(DEBUG)
#define VDK_PRINT_DEBUG(fmt,args...) { printk(KERN_INFO "VSD: [DEBUG] " fmt,##args); }
#else
#define VDK_PRINT_DEBUG(fmt,args...) {;}
#endif

#define VDK_PRINT(fmt,args...) { printk(KERN_INFO "VSD: " fmt,##args); }

#define VDK_PRINT_WARNING(fmt,args...) { printk(KERN_INFO "VSD: [Warning] " fmt,##args); }

#define VDK_PRINT_ERROR(fmt,args...) { printk(KERN_INFO "VSD: [ERROR] " fmt,##args); }

#define VDK_PRINT_TITLE(t,l) VDK_PRINT("VTune(TM) Performance Analyzer %s sampling driver v%d.%d has been %s.\n", t, SYS_VERSION_MAJOR, SYS_VERSION_MINOR, l)

#ifdef CONFIG_SMP
#define VDK_PRINT_BANNER(l) VDK_PRINT_TITLE("SMP",l)
#else
#define VDK_PRINT_BANNER(l) VDK_PRINT_TITLE("UP",l)
#endif

//
// Depending on how the task_struct is defined in .../include/linux/sched.h
//
#if defined(USE_PARENT_TASK_FIELD)
#define VT_GET_PARENT(x)  ((x)->parent)
#else	
#define VT_GET_PARENT(x)  ((x)->p_opptr)
#endif	

#endif /* _VTUNE_DEF_H */
