/*
 *  familyf_msr.h 
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
 *	File: familyf_msr.h 
 *
 *	Description: Definition file for Intel(R) Pentium(R) 4 processor
 *                   performance event counters.
 *                   The presence of the Debug Trace and precise EMON features
 *                   can be determined via the DTS bit (bit 21) in the feature
 *                   flags returned by the CPUID instruction. 
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#if !defined(_VTUNE_FAMILYF_MSR_H)
#define _VTUNE_FAMILYF_MSR_H

#define CCCR_REG  1
#define COUNT_REG 2
#define PEBS_REG  3
#define ESCR_REG  4
#define UNKNOWN_REG 0

#define CCCR_FIRST 864
#define CCCR_LAST  881
#define COUNT_FIRST (CCCR_FIRST - 0x60)
#define COUNT_LAST  (CCCR_LAST  - 0x60)
#define ESCR_FIRST 928
#define ESCR_LAST  993

#define NUM_ESCRS (ESCR_LAST - ESCR_FIRST +1)
#define NUM_CCCRS (ESCR_LAST - ESCR_FIRST +1)

#define NUMBER_OF_WMT_EVENT_ESCRS     2
#define NUMBER_OF_WMT_COUNTERS        18
#define NUMBER_OF_WMT_CONTROLS        18
#define NUMBER_OF_WMT_PUBS            4
#define NUMBER_OF_WMT_UNITS           23
#define NUMBER_OF_WMT_SELECTORS       (NUMBER_OF_WMT_EVENT_ESCRS*NUMBER_OF_WMT_UNITS)
#define NUMBER_OF_WMT_MASKS           16
#define NUMBER_OF_WMT_CRUS            3
#define MAX_WMTEVENTS                 18    //18 counters
#define MAX_WMT_COUNTER_VALUE         0x10000000000

#define WMT_RATE                      0x10000
#define WMT_CC_USER                   1
#define WMT_CC_SUP                    2
#define WMT_CC_ALL                    3

/*

    Event Selection control register: ESCR

    +------+-----------+---------+--------+--------+--------+-------+--------+
    |  31  |   30-25   |  24-09  | 08-04  |   03   |   02   |   01  |   00   |
    +------+-----------+---------+--------+--------+--------+-------+--------+
    |Rsvd  |EventSelect|Eventmask|  Rsvd  | T0_OS  | T0_USR | T1_OS | T1_USR |
    +------+-----------+---------+--------+--------+--------+-------+--------+

    +------------------------------------------------------------------------+
    | 63-32                                                                  |
    +------------------------------------------------------------------------+
    |Rsvd                                                                    |
    +------------------------------------------------------------------------+

    Event Select control register (ESCR) field encodings

    +------------+-----------------------------------------------------------+
    |  Field     | description                                               |
    +------------+-----------------------------------------------------------+
    |T1_USR      | If (Thread == T1) and  (T1_CPL=1|2|3), output event count |
    |T1_OS       | If (Thread == T1) and  (T1_CPL=0), output event count     |
    |T0_USR      | If (Thread == T0) and  (T0_CPL=1|2|3), output event count |
    |T0_OS       | If (Thread == T0) and  (T0_CPL=0), output event count     |
    |Event mask  | Selects an event-specific mask                            |
    |Event Select| Selects an unit-specific event                            |
    +------------+-----------------------------------------------------------+

*/

//
//      ESCR                        MSR           performance Counters
//
#define BSU_CR_ESCR0                0x3A0   //Counter 0, 1
#define BSU_CR_ESCR1                0x3A1   //Counter 2, 3
#define FSB_CR_ESCR0                0x3A2   //Counter 0, 1
#define FSB_CR_ESCR1                0x3A3   //Counter 2, 3
#define FIRM_CR_ESCR0               0x3A4   //Counter 8, 9
#define FIRM_CR_ESCR1               0x3A5   //Counter 10, 11
#define FLAME_CR_ESCR0              0x3A6   //Counter 8, 9
#define FLAME_CR_ESCR1              0x3A7   //Counter 10, 11
#define DAC_CR_ESCR0                0x3A8   //Counter 8, 9
#define DAC_CR_ESCR1                0x3A9   //Counter 10, 11
#define MOB_CR_ESCR0                0x3AA   //Counter 0, 1
#define MOB_CR_ESCR1                0x3AB   //Counter 2, 3
#define PMH_CR_ESCR0                0x3AC   //Counter 0, 1
#define PMH_CR_ESCR1                0x3AD   //Counter 2, 3
#define SAAT_CR_ESCR0               0x3AE   //Counter 8, 9
#define SAAT_CR_ESCR1               0x3AF   //Counter 10, 11
#define U2L_CR_ESCR0                0x3B0   //Counter 8, 9
#define U2L_CR_ESCR1                0x3B1   //Counter 10, 11
#define BPU_CR_ESCR0                0x3B2   //Counter 0, 1
#define BPU_CR_ESCR1                0x3B3   //Counter 2, 3
#define IS_CR_ESCR0                 0x3B4   //Counter 0, 1
#define IS_CR_ESCR1                 0x3B5   //Counter 2, 3
#define ITLB_CR_ESCR0               0x3B6   //Counter 0, 1
#define ITLB_CR_ESCR1               0x3B7   //Counter 2, 3
#define CRU_CR_ESCR0                0x3B8   //Counter 12, 13, 16
#define CRU_CR_ESCR1                0x3B9   //Counter 14, 15, 17
#define IQ_CR_ESCR0                 0x3BA   //Counter 12, 13, 16
#define IQ_CR_ESCR1                 0x3BB   //Counter 14, 15, 17
#define RAT_CR_ESCR0                0x3BC   //Counter 12, 13, 16
#define RAT_CR_ESCR1                0x3BD   //Counter 14, 15, 17
#define SSU_CR_ESCR0                0x3BE   //Counter 12, 13, 16
#define MS_CR_ESCR0                 0x3C0   //Counter 4, 5
#define MS_CR_ESCR1                 0x3C1   //Counter 6, 7
#define TBPU_CR_ESCR0               0x3C2   //Counter 4, 5
#define TBPU_CR_ESCR1               0x3C3   //Counter 6, 7
#define TC_CR_ESCR0                 0x3C4   //Counter 4, 5
#define TC_CR_ESCR1                 0x3C5   //Counter 6, 7
#define IX_CR_ESCR0                 0x3C8   //Counter 0, 1
#define IX_CR_ESCR1                 0x3C9   //Counter 2, 3
#define ALF_CR_ESCR0                0x3CA   //Counter 12, 13, 16
#define ALF_CR_ESCR1                0x3CB   //Counter 14, 15, 17
#define CRU_CR_ESCR2                0x3CC   //Counter 12, 13, 16
#define CRU_CR_ESCR3                0x3CD   //Counter 14, 15, 17
#define CRU_CR_ESCR4                0x3E0   //Counter 12, 13, 16
#define CRU_CR_ESCR5                0x3E1   //Counter 14, 15, 17

#define ESCR_INTERNAL_MASK          0x80000000  //pos: 31
#define ESCR_EventSelect_MASK       0x7e000000  //pos: 30-25
#define ESCR_MASK_MASK              0x01fffe00  //pos: 24-09
#define ESCR_Tagvalue_MASK          0x000001e0  //pos: 08-05
#define ESCR_TagUop_MASK            0x00000010  //pos: 04
#define ESCR_T0_OS_MASK             0x00000008  //pos: 03
#define ESCR_T0_USR_MASK            0x00000004  //pos: 02
#define ESCR_T0_MASK                0x0000000c
#define ESCR_T1_OS_MASK             0x00000002  //pos: 01
#define ESCR_T1_USR_MASK            0x00000001  //pos: 00
#define ESCR_T1_MASK                0x00000003

#define WMT_CR_DTES_AREA    0x0600  //New MSR defined for Pentium(R) 4 processor
#define DEBUG_CTL_MSR       0x1d9
#define debug_ctl_msr_DTS     0x8000    //enable store record to memory
#define debug_ctl_msr_LBR     0x0001 	//enable lbr

#define CPUID_DTS_MASK      0x00200000  //Debug Trace Sampling

/*

    Counter Configuration and control registers: CCCR
    
    +---+----+-----+------+------+------+----+------+-----+----+-------+------+------+-----+
    |31 |30  |28-29| 27   | 26   | 25   | 24 |23-20 | 19  | 18 | 17-16 |15-13 |  12  |11-00|
    +---+----+-----+------+------+------+----+------+-----+----+-------+------+------+-----+
    |OVF|CAS |Rsvd |OVF_  |OVF_  |FORCE_|EDGE|THRESH|COMPL|COMP|ACTIVE_|SELECT|ENABLE|Rsvd |
    |   |CADE|     |PMI_T1|PMI_T0|OVF   |    |HOLD  |EMENT|ARE |THREADS|      |      |     |
    +---+----+-----+------+------+------+----+------+-----+----+-------+------+------+-----+

    +--------------------------------------------------------------------------------------+
    | 63-32                                                                                |
    +--------------------------------------------------------------------------------------+
    |Rsvd                                                                                  |
    +--------------------------------------------------------------------------------------+

    Counter Configuration and control registers (CCCR) field encodings

    +--------------+-----------------------------------------------------------------------+
    |  Field       | description                                                           |
    +--------------+-----------------------------------------------------------------------+
    |ENABLE        |If set counting is enabled. Bit cleared on reset                       |
    +--------------+-----------------------------------------------------------------------+
    |SELECT        |Identifies the ESCR to be used for selecting event to be counted in    |
    |              |  this counter                                                         |
    +--------------+-----------------------------------------------------------------------+
    |ACTIVE_THREADS|Select when counting should occur based on which threads are active    |
    +--------------+-----------------------------------------------------------------------+
    |COMPARE       |If set, enable filtering using the THRESHOLD value                     |
    +--------------+-----------------------------------------------------------------------+
    |COMPLEMENT    |If clear, use > comparison; if set use the <= comparison               |
    +--------------+-----------------------------------------------------------------------+
    |THRESHOLD     |threshold value to be used for comparison                              |
    +--------------+-----------------------------------------------------------------------+
    |EDGE          |If set, edge detect; increment counter by 1 if the previous input value|
    |              |  was zero and the current input is non-zero                           |
    +--------------+-----------------------------------------------------------------------+
    |FORCE_OVF     |If set, force an overflow on every counter increment.                  |
    +--------------+-----------------------------------------------------------------------+
    |OVF_PMI_T0    |If set, cause a performance Monitor interrupt (PMI) to logical         |
    |              | processor T0 on counter overflow.                                     |
    +--------------+-----------------------------------------------------------------------+
    |OVF_PMI_T1    |If set, cause a performance Monitor interrupt (PMI) to logical         | 
    |              |  processor T1 on counter overflow.                                    |
    +--------------+-----------------------------------------------------------------------+
    |CASCADE       |If set, enable counting when the "alternate" counter of the counter    |
    |              |  pair overflows.                                                      |
    +--------------+-----------------------------------------------------------------------+
    |OVF           |A status bit that indicates that the counter has overflowed. This bit  |
    |              |  is reset by software.                                                |
    +--------------+-----------------------------------------------------------------------+

    Note: The counter can also be enabled via the cascading feature if the CASCADE bit is set. 
    
    +-------------+------------------------------------------------------------------------+
    |ACTIVE_THREAD| Abbrev  |         description                                          |
    +-------------+------------------------------------------------------------------------+
    |    00       |    NT   | No logical processor mode                                    |
    |             |         | (only count when neither T0 or T1 is active)                 |
    +-------------+------------------------------------------------------------------------+
    |    01       |    ST   | Single logical processor mode                                |
    |             |         | (count when only one logical processor -                     |
    |             |         |  either T0 or T1 - is active)                                |
    +-------------+------------------------------------------------------------------------+
    |    10       |    DT   | Dual logical processor mode                                  |
    |             |         | (count when both T0 and T1 are active)                       |       
    +-------------+------------------------------------------------------------------------+
    |    11       |   ANY   | Count when either logical processor is active                |
    +-------------+------------------------------------------------------------------------+


*/

#define ACTIVE_THREAD_NT            0
#define ACTIVE_THREAD_ST            1
#define ACTIVE_THREAD_DT            2
#define ACTIVE_THREAD_ANY           3

//
//      CCCR                        MSR           performance Counters
//
#define BPU_CR_PUB_CCCR0            0x360   //BPU_CR_PUB_COUNTER0
#define BPU_CR_PUB_CCCR1            0x361   //BPU_CR_PUB_COUNTER1
#define BPU_CR_PUB_CCCR2            0x362   //BPU_CR_PUB_COUNTER2
#define BPU_CR_PUB_CCCR3            0x363   //BPU_CR_PUB_COUNTER3
#define MS_CR_PUB_CCCR0             0x364   //MS_CR_PUB_COUNTER0
#define MS_CR_PUB_CCCR1             0x365   //MS_CR_PUB_COUNTER1
#define MS_CR_PUB_CCCR2             0x366   //MS_CR_PUB_COUNTER2
#define MS_CR_PUB_CCCR3             0x367   //MS_CR_PUB_COUNTER3
#define FLAME_CR_PUB_CCCR0          0x368   //FLAME_CR_PUB_COUNTER0
#define FLAME_CR_PUB_CCCR1          0x369   //FLAME_CR_PUB_COUNTER1
#define FLAME_CR_PUB_CCCR2          0x36a   //FLAME_CR_PUB_COUNTER2
#define FLAME_CR_PUB_CCCR3          0x36b   //FLAME_CR_PUB_COUNTER3
#define IQ_CR_PUB_CCCR0             0x36c   //IQ_CR_PUB_COUNTER0
#define IQ_CR_PUB_CCCR1             0x36d   //IQ_CR_PUB_COUNTER1
#define IQ_CR_PUB_CCCR2             0x36e   //IQ_CR_PUB_COUNTER2
#define IQ_CR_PUB_CCCR3             0x36f   //IQ_CR_PUB_COUNTER3
#define IQ_CR_PUB_CCCR4             0x370   //IQ_CR_PUB_COUNTER4
#define IQ_CR_PUB_CCCR5             0x371   //IQ_CR_PUB_COUNTER5

#define CCCR_Overflow_MASK          0x80000000  //pos: 31
#define CCCR_Cascade_MASK           0x40000000  //pos: 30
#define CCCR_OVF_UBP1_MASK          0x20000000  //pos: 29
#define CCCR_OVF_UBP0_MASK          0x10000000  //pos: 28
#define CCCR_OVF_PMI_T1_MASK        0x08000000  //pos: 27
#define CCCR_OVF_PMI_T0_MASK        0x04000000  //pos: 26
#define CCCR_ForceOverflow_MASK     0x02000000  //pos: 25
#define CCCR_Edge_MASK              0x01000000  //pos: 24
#define CCCR_threshold_MASK         0x00f00000  //pos: 23-20
#define CCCR_Complement_MASK        0x00080000  //pos: 19
#define CCCR_Compare_MASK           0x00040000  //pos: 18
#define CCCR_active_threads_MASK     0x00030000 //pos: 17-16
#define CCCR_Select_MASK            0x0000e000  //pos: 15-13
#define CCCR_Enable_MASK            0x00001000  //pos: 12

//
//  CCCR Select encoding
//
#define CCCR_SELECT_BSU             7
#define CCCR_SELECT_FSB             6
#define CCCR_SELECT_FIRM            1
#define CCCR_SELECT_FLAME           0
#define CCCR_SELECT_DAC             5
#define CCCR_SELECT_MOB             2
#define CCCR_SELECT_PMH             4
#define CCCR_SELECT_SAAT            2
#define CCCR_SELECT_U2L             3
#define CCCR_SELECT_BPU             0
#define CCCR_SELECT_IS              1
#define CCCR_SELECT_ITLB            3
#define CCCR_SELECT_CRUD            4
#define CCCR_SELECT_CRUN            5
#define CCCR_SELECT_CRUR            6
#define CCCR_SELECT_IQ              0
#define CCCR_SELECT_RAT             2
#define CCCR_SELECT_SSU             3
#define CCCR_SELECT_MS              0
#define CCCR_SELECT_TBPU            2
#define CCCR_SELECT_TC              1
#define CCCR_SELECT_IX              5
#define CCCR_SELECT_ALF             1

/*

    Each performance counter is 40-bits wide. The RDPMC instruction has been 
    enhanced to enable reading of either the full counter-width (40-bits) 
    or the lower 32-bits of the counter. 

    +------------------------------------------------------------------------+
    | 39-0                                                                   |
    +------------------------------------------------------------------------+
    | performance Counter register                                           |
    +------------------------------------------------------------------------+

    +------------------------------------------------------------------------+
    | 63-40                                                                  |
    +------------------------------------------------------------------------+
    |Rsvd                                                                    |
    +------------------------------------------------------------------------+

*/

//
//      COUNTERS                    MSR       
//
#define BPU_CR_PUB_COUNTER0         0x300   //Counter #0
#define BPU_CR_PUB_COUNTER1         0x301   //Counter #1
#define BPU_CR_PUB_COUNTER2         0x302   //Counter #2
#define BPU_CR_PUB_COUNTER3         0x303   //Counter #3
#define MS_CR_PUB_COUNTER0          0x304   //Counter #4
#define MS_CR_PUB_COUNTER1          0x305   //Counter #5
#define MS_CR_PUB_COUNTER2          0x306   //Counter #6
#define MS_CR_PUB_COUNTER3          0x307   //Counter #7
#define FLAME_CR_PUB_COUNTER0       0x308   //Counter #8
#define FLAME_CR_PUB_COUNTER1       0x309   //Counter #9
#define FLAME_CR_PUB_COUNTER2       0x30a   //Counter #10
#define FLAME_CR_PUB_COUNTER3       0x30b   //Counter #11
#define IQ_CR_PUB_COUNTER0          0x30c   //Counter #12
#define IQ_CR_PUB_COUNTER1          0x30d   //Counter #13
#define IQ_CR_PUB_COUNTER2          0x30e   //Counter #14
#define IQ_CR_PUB_COUNTER3          0x30f   //Counter #15
#define IQ_CR_PUB_COUNTER4          0x310   //Counter #16
#define IQ_CR_PUB_COUNTER5          0x311   //Counter #17

#define NUM_CCCRS_BPU               4
#define NUM_CCCRS_MS                4
#define NUM_CCCRS_FLAME             4
#define NUM_CCCRS_IQ                6

#define NUM_ESCRS_BSU               2
#define NUM_ESCRS_FSB               2
#define NUM_ESCRS_FIRM              2
#define NUM_ESCRS_FLAME             2
#define NUM_ESCRS_DAC               2
#define NUM_ESCRS_MOB               2
#define NUM_ESCRS_PMH               2
#define NUM_ESCRS_SAAT              2
#define NUM_ESCRS_U2L               2
#define NUM_ESCRS_BPU               2
#define NUM_ESCRS_IS                2
#define NUM_ESCRS_ITLB              2
#define NUM_ESCRS_CRUD              2
#define NUM_ESCRS_CRUN              2
#define NUM_ESCRS_CRUR              2
#define NUM_ESCRS_IQ                2
#define NUM_ESCRS_RAT               2
#define NUM_ESCRS_SSU               1
#define NUM_ESCRS_MS                2
#define NUM_ESCRS_TBPU              2
#define NUM_ESCRS_TC                2
#define NUM_ESCRS_IX                2
#define NUM_ESCRS_ALF               2

/*
    Hyper-Threading Technology Support
*/
#define CPUID_ACPI_MASK                 0x00400000
#define CPUID_ATHROT_MASK               0x20000000
#define CPUID_HT_MASK                   0x10000000

/*

    precise event-based sampling control register: CRU_CR_PEBS_MATRIX_HORIZ 

    +------------------+-----------------+------------------------------------+
    |       26         |      25         |               24-00                |
    +------------------+-----------------+------------------------------------+
    |ENABLE_EBS_OTH_THR|ENABLE_EBS_MY_THR|              Rsvd                  |
    +------------------+-----------------+------------------------------------+

    +-------------------------------------------------------------------------+
    | 63-27                                                                   |
    +-------------------------------------------------------------------------+
    |Rsvd                                                                     |
    +-------------------------------------------------------------------------+


*/

#define TC_CR_PRECISE_EVENT             0x3f0
#define CRU_CR_PEBS_MATRIX_HORIZ        0x3f1
#define CRU_CR_PEBS_MATRIX_VERT         0x3f2

#define ENABLE_EBS_OTH_THR              0x04000000
#define ENABLE_EBS_MY_THR               0x02000000

/*

    Configuration Area for Debug Trace and precise event: WMT_CR_DTES_ATEA

    +------------------------------------------------------------------------+
    | 31-0                                                                   |
    +------------------------------------------------------------------------+
    | Configuration Area Linear Address                                      |
    +------------------------------------------------------------------------+

    +------------------------------------------------------------------------+
    | 63-32                                                                  |
    +------------------------------------------------------------------------+
    |Rsvd                                                                    |
    +------------------------------------------------------------------------+

    MSR Address: 0600h, defined in dtsdrv.h

*/

/*

    The DTES memory storage has three parts: 
        the save area that has the buffer management information common to DTS 
            and precise EMON, 
        the debug trace store (DTS) for the branch trace messages, 
        and the precise event-based sampling store. 
        
    DTES buffer format:
    +------------+----------------------------------------------------------------+
    |  Offset    | Double Word Contents                                           |
    +------------+----------------------------------------------------------------+
    |   0x00     | DTS buffer base                                                |   
    +------------+----------------------------------------------------------------+ 
    |   0x04     | DTS index: the location to write the next record to            |
    +------------+----------------------------------------------------------------+
    |   0x08     | DTS absolute max: next byte past the end of the buffer         |
    +------------+----------------------------------------------------------------+
    |   0x0C     | DTS interrupt threshold. Must be < absolute maximum.           |
    |            |   Must be at an offset from the base that is a multiple of     |
    |            |   the branch trace message record size                         |
    +------------+----------------------------------------------------------------+
    |   0x10     | precise event-based sampling buffer base                       |
    +------------+----------------------------------------------------------------+
    |   0x14     | precise event-based sampling index                             |
    +------------+----------------------------------------------------------------+
    |   0x18     | precise event-based sampling absolute max                      |
    +------------+----------------------------------------------------------------+ 
    |   0x1C     | precise event-based sampling interrupt threshold.              |
    |            |   Must be < absolute maximum. Must be at an offset from        |
    |            |   the base that is a multiple of the branch trace message      |
    |            |   record size.                                                 |
    +------------+----------------------------------------------------------------+
    |   0x20     | precise event-based sampling counter reset value, lower 32 bits|
    +------------+----------------------------------------------------------------+
    |   0x24     | precise event-based sampling counter reset value, upper 8-bits |
    +------------+----------------------------------------------------------------+
    |   0x30     | Reserved - 128 bytes                                           |
    +------------+----------------------------------------------------------------+

    Each precise Event-based Sampling Record:
    
    +------------+----------------------------+
    |  Offset    | Contents                   |
    +------------+----------------------------+
    |   0x00     | Eflags                     |
    +------------+----------------------------+
    |   0x04     | Linear IP                  |
    +------------+----------------------------+
    |   0x08     | EAX                        |
    +------------+----------------------------+
    |   0x0C     | EBX                        |
    +------------+----------------------------+
    |   0x10     | ECX                        |
    +------------+----------------------------+
    |   0x14     | EDX                        |
    +------------+----------------------------+
    |   0x18     | ESI                        |
    +------------+----------------------------+ 
    |   0x1C     | EDI                        |
    +------------+----------------------------+
    |   0x20     | EBP                        |
    +------------+----------------------------+
    |   0x24     | ESP                        |
    +------------+----------------------------+

*/

typedef struct _DTS_RECORD {
    void_ptr from_address;
    void_ptr to_address;
    void_ptr branch_prediction; // Only for Pentium(R) 4 processor
} DTS_RECORD, *PDTS_RECORD;

typedef struct _DTS_BUFFER {
    __u32 base;
    __u32 index;
    __u32 max;
    __u32 threshold;
    __u32 PEBS_base;
    __u32 PEBS_index;
    __u32 PEBS_max;
    __u32 PEBS_threshold;
    __u32 PEBS_counter_reset_value1;    //lower 32 bits
    __u32 PEBS_counter_reset_value2;    //Upper 8 bits
    __u32 buffer[32];   // 128 bytes Pentium(R) 4 processor OSWG Manual
} DTS_BUFFER, *PDTS_BUFFER;

typedef enum _buffer_mode {
    buf_circular,
    buf_interrupt
} buffer_mode;

typedef struct _DTS_USER_HEADER {
    char processor_number;
    __u32 number_of_branches;
    __u32 buffer_mode;  // buf_circular, buf_interrupt or Emon
    __u32 reserved1;
} DTS_USER_HEADER, *PDTS_USER_HEADER;

#define DTS_RECORD_SIZE             sizeof(DTS_RECORD)
#define DTS_SAVE_AREA               sizeof(DTS_BUFFER)

#define OVERFLOW_RECORDS_SIZE       (OVERFLOW_RECORDS*DTS_RECORD_SIZE)
#define DTS_INFO_AREA               (DTS_SAVE_AREA+OVERFLOW_RECORDS_SIZE+DTS_RECORD_SIZE)
#define DEFAULT_BUFFER_SIZE         (MINIMUM_BRANCHES*DTS_RECORD_SIZE+DTS_INFO_AREA)

/*
    __u32 linear_IP;
*/

//
// For PEBS
//
typedef struct _PEBS_RECORD {
    __u32 E_flags;
    __u32 linear_IP;
    __u32 EAX1;
    __u32 EBX1;
    __u32 ECX1;
    __u32 EDX1;
    __u32 ESI1;
    __u32 EDI1;
    __u32 EBP1;
    __u32 ESP1;
} PEBS_RECORD, *PPEBS_RECORD;

typedef struct _PEBS_USER_HEADER {
    char processor_number;
    __u32 number_of_samples;
    __u32 buffer_mode;
    __u32 reserved1;
} PEBS_USER_HEADER, *PPEBS_USER_HEADER;

#define PEBS_RECORD_SIZE            sizeof(PEBS_RECORD)
#define PEBS_INFO_AREA              (DTS_SAVE_AREA+PEBS_OVERFLOW_SIZE+PEBS_RECORD_SIZE)

//
// Where the LBRs are... at least for now...
// no guarantee they will remain here or remain sequentially ordered...
//
#define MSR_LASTBRANCH_TOS  	0x01da
#define MSR_LASTBRANCH_0    	(MSR_LASTBRANCH_TOS + 1)
#define MSR_LASTBRANCH_1    	(MSR_LASTBRANCH_0 + 1)
#define MSR_LASTBRANCH_2    	(MSR_LASTBRANCH_0 + 2)
#define MSR_LASTBRANCH_3    	(MSR_LASTBRANCH_0 + 3)

#define LBR_SAVE_SIZE		(sizeof(ULARGE_INTEGER) * 5)

typedef struct _IA32EM64T_DTS_BUFFER {
    __u64 base;                   // Offset 0x00
    __u64 index;                  // Offset 0x08
    __u64 max;                    // Offset 0x10
    __u64 threshold;              // Offset 0x18
    __u64 PEBS_base;               // Offset 0x20
    __u64 PEBS_index;              // Offset 0x28
    __u64 PEBS_max;                // Offset 0x30
    __u64 PEBS_threshold;          // Offset 0x38
    __u64 PEBS_counter_addr;        // Offset 0x40
    __u64 PEBS_counter_reset;       // Offset 0x48
} IA32EM64T_DTS_BUFFER, *PIA32EM64T_DTS_BUFFER;


//
// For PEBS
//
typedef struct _IA32EM64T_PEBS_RECORD {
    __u64 E_flags;            // Offset 0x00
    __u64 linear_IP;          // Offset 0x08
    __u64 rax;                // Offset 0x10
    __u64 rbx;                // Offset 0x18
    __u64 rcx;                // Offset 0x20
    __u64 rdx;                // Offset 0x28
    __u64 rsi;                // Offset 0x30
    __u64 rdi;                // Offset 0x38
    __u64 rbp;                // Offset 0x40
    __u64 rsp;                // Offset 0x48
    __u64 r8;                 // Offset 0x50
    __u64 r9;                 // Offset 0x58
    __u64 r10;                // Offset 0x60
    __u64 r11;                // Offset 0x68
    __u64 r12;                // Offset 0x70
    __u64 r13;                // Offset 0x78
    __u64 r14;                // Offset 0x80
    __u64 r15;                // Offset 0x88
} IA32EM64T_PEBS_RECORD, *PIA32EM64T_PEBS_RECORD;

//
// This is the same for all PEBS enabled processoes (so far) and would
// go in a "generic x86" PMU header file...
//
#define MSR_CR_DTES_AREA        0x600

//
// this is also common with all PEBS enabled processors (so far)
//
#define DEBUG_CTRL_TR_BIT   (1 << 2)

//
// Will need to move this to a processor specific spot... Since we need
// to deal with Intel(R) Pentium(R) 4 processors with Streaming SIMD 
// Extensions 3 (SSE3) vs. those without at runtime, we need this
// information in an array that is generated as needed.  So, use base
// base and counts instead of the full list spelled out.
//
// LBR msr's for IA-32 processors.  NOT ARCHITECTURAL so they can change!
//
#define MSR_LBR_PSC_TOS             474

#define MSR_LBR_PSC_SRC_BASE        1664
#define MSR_LBR_PSC_SRC_NUMBER      16

#define MSR_LBR_PSC_TARG_BASE       1728
#define MSR_LBR_PSC_TARG_NUMBER     16

#endif /* _VTUNE_FAMILYF_MSR_H */
