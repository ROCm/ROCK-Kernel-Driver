/*
 *  driversharedarea.h
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
 *	File: driversharedarea.h
 *
 *	Description: sampling data structures shared between Windows*
 *                   and Linux*
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#ifndef _DRIVERSHAREDAREA_H
#define _DRIVERSHAREDAREA_H

#include "sampfile.h"

#define AREA_VERSION    12  // for area_version (read as 0.12)

/*
******************************************************************************
*           Windows* and Linux* shared structs
******************************************************************************
*/

/*
  The following structure is created and mapped by TBS Sampler device drivers,
  Windows*.  This area is pagefixed and can be globally mapped.
  The mapping address may be different in each process, so care must be taken
  to use only relative offsets within this area, no absolute pointers.
*/

typedef struct driver_shared_area_s {
  __u32 length;                  // length of this entire area
  __u32 area_version;            // version of this mapping area
  __u32 driver_version;          // driver version number  (e.g. 400 read as 4.00)
  __u32 offset_marks_global_area;// offset of marks global area
  __u32 sample_skip_count;       // count of samples skipped due to pause mode or some other     01-15-98
                                 // ..reason. This is a total for all cpus all counters          01-15-98
  __u32 suspend_start_count;     // count of times sampling suspended during a sampling session  01-15-98
                                 // ..suspension is usually due to buffer full write             01-15-98
  __u32 suspend_end_count;       // count of times sampling suspend ended during a sampling session  01-15-98
  __u32 reserved[17];            // other offsets are coming
                                 // BK 11-14-97 changed pause_count from __u32 to __s32 to be able to use Interlocked... APIs correctly
  __u32 pause_count;             // pause / resume count.  Sampling paused if greater than 0
  __u32 sample_session_count;    // incremented for each TBSStart()
  __u32 sample_count;            // current sample count
  __u32 lock_owner;              // pid of current sampler lock owner (no lock if NULL)
  __u32 num_open;                // number of current open's to device driver
  char vtune_output_file[256];   // fully qualified name of sample file output
  __u32 cpu_ID[32];              // id's of cpu's (num cpu in sys_info.dw_number_of_processors)
  __u32 mega_hertz;              // approximate speed of this machine in megahertz (used for TSC conversion)
  __u32 vtune_filename_offset;   // offset into path of filename for vtune_output_file
  __u32 reserved2[11];
  //
  union {
    __u32 flags;                 // status flags as one __u32
    struct {                     // options broken out by bit fields
      __u32 running:1;           // sampling is running
      __u32 start_delay:1;       // start delay in effect (loads being tracked, no sampling)
      __u32 pid_set:1;           // the sampler device driver set pid's in the sample records
      __u32 tid_set:1;           // the sampler device driver set tid's in the sample records
      __u32 module_tracking:1;   // driver tracks module loads
      __u32 module_rec2and5:1;   // driver can build either 2.0 or TB5 format module records.  01-05-02
      // This flag is was defined for Windows* 95 VXD. The VXD builds
      // the 2.0 format module records for VTune analyzer version 5
      // and VTune analyzer version 6 beta.  TB5 format module records
      // will be built for VTune analyzer version 6 FCS.
      // The TB5 module record option is activated for a sampling 
      // session via a command in the samp_parm_ex configuration structure
      __u32 pid_creates_tracked_in_module_recs:1;  // if this flag is set, the
      //  1stModuleRecInProcess flag in the module_record 
      //  struct indicates that a process 
      //  was created 
      __u32:25;                  // flags to come
    };
  };
  //
  //  following fields set by driver after sampling complete
  __u32 duration;                // total sample time in milliseconds
  __u32 tot_samples;             // total samples taken (sample_count at end of session)
  __u16 sample_rec_length;       // length of each sample record
  __u16 reserved3;
  __u32 tot_idt_ints;            // total physical interrupts taken for this sampling session
  __u32 tot_profile_ints;        // total number of calls by OS to our profile interrupt routine
  __u32 tot_skip_samp;           // total samples skipped during file I/O
  union {
    __u32 driver_flags;          // supported samplings methods on this machine
    struct {
      __u32 method_RTC:1;        // real time clock
      __u32 method_VTD:1;        // OS Virtual Timer Device
      __u32 method_NMI:1;        // non-maskable interrupt time based
      __u32 method_EBS:1;        // event based
      __u32:28;                  // flags to come
    };
  };
  __u16 num_event_counters;  // number of event counters supported
  __u16 reserved4;
  __u32 reserved5[6];
} driver_shared_area;

/*
  Sample Parameter area.  Passed to driver to start / configure sampling.
*/

#define MAX_ESR_SETS 16      // Pentium(R) 4 processor  05-31-00

typedef struct _samp_parm_header {
  char eye_catcher[4];           // eyecatcher "SPRM"
  __u16 length;                  // length of this header
  __u16 sp_version;              // sampparm version
  __u32 sp_offset;               // offset to sampparm structure from the beginning of this structure
  __u32 sp_length;               // length of the sampparm struct
} samp_parm_header;

typedef struct samp_parm3_s {
  union {
    __u32 options;               // options for sampling as one __u32
    struct {                     // options broken out by bit fields
      __u32 sample_TSC:1;        // record TimeStampCounter in samples
      __u32 start_in_pause_mode:1; // if set, start sampling in Pause mode  BK 3-10-98
      // the next two bits are designed to remain backwards compatible with other users who
      // are using the TBS subsystem DLLs without VTune analyzer BK 9-10-98
      __u32 PC_sampling_off:1;  // reset (i.e. 0) if traditional (TBS/EBS) sampling is supposed to be launched
      __u32 chronologies_on:1;  // set if chronologies are to be collected
      __u32 EBS_chronologies_on:1; // set if EBS chronologies are to be collected
      __u32 stop_returns_result:1; // if set, TBSstop returns error codes, else TBSstop returns sample count
      __u32:26;                   // options to come
    } sp3_o;
  } sp3_options;
  __u16 method;                 // sampling type, see SM_ defines in SampFile.h
  __u16 reserved;
  __u32 samp_rate;              // sample rate in microseconds
  __u32 samps_per_buffer;       // samples per buffer
  __u32 maximum_samples;        // stop sampling when this number reached (0 = infinite)
  __u32 max_interval;           // maximum time to sample in seconds (0 = infinite)
  __u32 start_delay;            // seconds to delay start of sampling
  char *module_info_file_name;  // name of file to write module records to
  char *raw_sample_file_name;   // name of file to write samples to (before merging is done)
  __u32 num_event_reg_set;      // number of event_reg_set structures
  event_reg_set esr_set[MAX_ESR_SETS]; // used to set Event registers   Pentium(R) 4 processor 05-31-00
  __u32 reserved2[9];
} samp_parm3;

typedef struct samp_parm5_s {
  union {
    __u32 options;              // options for sampling as one __u32
    struct {                    // options broken out by bit fields
      __u32 sample_TSC:1;       // record TimeStampCounter in samples
      __u32 track_pid_creates:1;
      __u32 track_tid_creates:1;
      __u32 start_in_pause_mode:1;  // if set, start sampling in Pause mode  BK 3-10-98
      __u32:28; // options to come
    } sp5_o;
  } sp5_options;
  __u32 samps_per_buffer;       // samples per buffer
  __u32 maximum_samples;        // stop sampling when this number reached (0 = infinite)
  __u32 max_interval;           // maximum time to sample in seconds (0 = infinite)
  __u32 start_delay;            // seconds to delay start of sampling
  char *module_info_file_name;  // name of file to write module records to
  char *raw_sample_file_name;   // name of file to write samples to (before merging is done)
  __u32 reserved2[16];
  __u32 num_event_reg_set;      // number of event_reg_set structures
  event_reg_set_ex esr_set[1];  // used to set Event registers
} samp_parm5;

typedef struct samp_parm6_s {
  union {
    __u32 options;              // options for sampling as one __u32
    struct {                    // options broken out by bit fields
      __u32 sample_TSC:1;       // record TimeStampCounter in samples
      __u32 track_pid_creates:1;
      __u32 track_tid_creates:1;
      __u32 start_in_pause_mode:1;  // if set, start sampling in Pause mode  BK 3-10-98
      __u32 calibration:1;      // calibration... don't collect PC samples or module info
      __u32 count_events:1;     // keep event totals
      __u32 ptrs_are_offsets:1; // all pointer fields in this structure are offsets
      __u32 create_final_file:1;//
      __u32 use_marks_collector:1; //
      __u32:23;                 // options to come
    };
  };
  __u32 samps_per_buffer;       // samples per buffer
  __u32 maximum_samples;        // stop sampling when this number reached (0 = infinite)
  __u32 max_interval;           // maximum time to sample in seconds (0 = infinite)
  __u32 start_delay;            // seconds to delay start of sampling
  __u32 reserved1[3];           // reserved for future use and for 8 byte alignment of the following 64-bit fields
  union {
    ULARGE_INTEGER module_info_file_name64;
    char *module_info_file_name; // name of file to write module records to
  };
  union {
    ULARGE_INTEGER raw_sample_file_name64;
    char *raw_sample_file_name; // name of file to write samples to (before merging is done)
  };
  __u32 reserved2[15];          // reserved for future use and 8 byte alignment of event_reg_set_ex array
  __u32 num_event_reg_set;      // number of event_reg_set structures
  event_reg_set_ex esr_set[1];  // used to set Event registers
} samp_parm6;

typedef struct samp_parm_ex {
  samp_parm_header hdr;         // sampparms header
  union {
    samp_parm3 sp3;     // sampparms
    samp_parm5 sp5;     // ..
    samp_parm6 sp6;     // ..
  } _spex;
} samp_parm_ex, *P_samp_parm_ex;

//
// Event totals
//  
// 
//  
typedef struct _EVENT_TOTALS {
  __u32 version;        // version of event totals
  __u32 length;         // length of this event totals buffer
  __u32 flags;          // flags
  __u32 num_events;     // number of events in the event totals arrays
  __u32 num_cpus;       // number of cpus represented in the event totals arrays
  __u32 offset_event_I_ds;    // offset of array of event IDs (__u32S). numEventIDs is number of entries in the array.
  __u32 offset_event_counts;  // offset of array of event counts (64 bit unsigned). numEventIDs is number of entries in the array.
  __u32 offset_event_counts_per_cpu;    // offset of two dimensional array of event counts (ULARGE_INTEGERs... 64 bit unsigned).
  // Can be indexed by [event number, cpu] where event number and cpu is current cpu number relative to 0.
  // is index of event_Ids array. numEventIDs and num_cpus are the two dimensions of the array/matrix.
  __u32 offset_tsc_at_start;  // time stamp counter when event counting started on a cpu. Two dimensional array with bounds [ num_events, num_cpus]
  __u32 offset_tsc_at_last_update;  // time stamp counter when event count last updated per cpu. Two dimensional array with bounds [ num_events, num_cpus]
  __u32 reserved[3];  // reverved (8 byte alignment)
} EVENT_TOTALS, *PEVENT_TOTALS;

//
// Definitions for RDPMC Driver API
//

#define RDPMC_MAX_CPUS 32
#define RDPMC_MAX_PMCS 64

typedef struct _PMC_VALUE {
  ULARGE_INTEGER pmc_current;   // current pmc value
  ULARGE_INTEGER pmc_total;
  ULARGE_INTEGER tsc_last_update;
} PMC_VALUE, *PPMC_VALUE;

typedef struct _PMC_VALUES_PER_CPU {
  PMC_VALUE pmc_val[RDPMC_MAX_CPUS];
} PMC_VALUES_PER_CPU, *PPMC_VALUES_PER_CPU;

typedef struct _RDPMC_BUF {
  __u32 version;        // version of rdpmc buf
  __u32 cpu_mask_in;    // cpu's to collect data on
  __u32 cpu_mask_out;   // cpu's on which data was collected
  __u32 reserved;       // reserved
  __u32 duration;       // total time in milliseconds, obtained from the DSA
  __u32 reserved1;      // reserved
  ULARGE_INTEGER pmc_mask;  // each bit represents a logical performance counter register. 
  // For x86, this specifies logical cpu perfrmance counter to be read (ecx for rdpmc... pmc_mask.bit0= mov ecx,0; rdpmc)
  // For Itanium(R) processor, this specifies the PMD registers to be read (ex pmc_mask.bit4 = mov 4 = t15; mov pmd[t15] = a1;)
  // reading of x86 pmc's on Itanium(R)-based processors is not supported
  PMC_VALUES_PER_CPU pmc_values[RDPMC_MAX_PMCS]; // one entry for each pmc
} RDPMC_BUF, *PRDPMC_BUF;
#define RDPMC_BUF_SIZE sizeof(RDPMC_BUF)

#endif  // _DRIVERSHAREDAREA_H
