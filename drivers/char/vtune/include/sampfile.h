/*
 *  sampfile.h
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
 *	File: sampfile.h
 *
 *	Description: sampling data structures and constants
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#ifndef _SAMPFILE_H
#define _SAMPFILE_H

#include "vtypes.h"

// sampling methods
#define SM_RTC          2020  // real time clock
#define SM_VTD          2021  // OS Virtual Timer Device
#define SM_NMI          2022  // non-maskable interrupt time based
#define SM_EBS          2023  // event based

// sampling mechanism bitmap definitions
#define INTERRUPT_RTC   0x1
#define INTERRUPT_VTD   0x2
#define INTERRUPT_NMI   0x4
#define INTERRUPT_EBS   0x8

// eflags defines
#define EFLAGS_VM       0x00020000  // V86 mode
#define EFLAGS_IOPL0    0
#define EFLAGS_IOPL1    0x00001000
#define EFLAGS_IOPL2    0x00002000
#define EFLAGS_IOPL3    0x00003000

typedef struct cpu_map_s {
  __u32 ul_cpu_num;
  __u32 ul_package_num;
  __u32 ul_hardware_thread_num;
} cpu_map;

// added by Bob Knight, 08-17-2001
typedef struct cpuid_output_s {
  __u32 ul_cpu_num;
  __u32 ul_EAX_input;
  __u32 ul_EAX_output;
  __u32 ul_EBX_output;
  __u32 ul_ECX_output;
  __u32 ul_EDX_output;
} cpuid_output;

/*
  X86 processor code descriptor
*/
typedef struct code_descriptor_s {
  union {
    __u32 low_word;       // low dword of descriptor
    struct {              // low broken out by fields
      __u16 limit_low;    // segment limit 15:00
      __u16 base_low;     // segment base 15:00
    };
  };
  union {
    __u32 high_word;      // high word of descriptor
    struct {              // high broken out by bit fields
      __u32 base_mid:8;   // base 23:16
      __u32 accessed:1;   // accessed
      __u32 readable:1;   // readable
      __u32 conforming:1; // conforming code segment
      __u32 one_one:2;    // always 11
      __u32 dpl:2;        // dpl
      __u32 pres:1;       // present bit
      __u32 limit_hi:4;   // limit 19:16
      __u32 sys:1;        // available for use by system
      __u32 reserved_0:1; // reserved, always 0
      __u32 default_size:1;  // default operation size (1=32bit, 0=16bit)
      __u32 granularity:1;   // granularity (1=32 bit, 0=20 bit)
      __u32 base_hi:8;    // base hi 31:24
    };
  };
} code_descriptor;

typedef struct sample_record_PC_s {
  union {
    struct {
      ULARGE_INTEGER iip;  // Itanium(R) processor interrupt instruction pointer
      ULARGE_INTEGER ipsr; // Itanium(R) processor interrupt processor status register
    };
    struct {
      __u32 eip;           // x86 instruction pointer
      __u32 eflags;        // x86 eflags
      code_descriptor csd; // x86 code seg descriptor (8 bytes)
    };
  };
  __u16 cs;                // x86 cs (0 for Itanium(R) processor)
  union {
    __u16 cpu_and_OS;      // cpu and OS info as one word
    struct {               // cpu and OS info broken out
      __u16 cpu_num:8;     // cpu number (0 - 255)
      __u16 resrvd:4;      // reserved bits
      __u16 not_vmid0:1;   // Windows* 95, vmid0 flag (1 means NOT vmid 0)
      __u16 code_mode:2;   // processor mode, see MODE_ defines
      __u16:1;             // reserved
    };
  };
  __u32 tid;               // thread ID  (from OS, may get reused, a problem, see tid_is_raw)   06-25-99
  __u32 pid_rec_index;     // process ID rec index (index into start of pid record section)
                           // .. can validly be 0 if not raw (array index).  Use returnPid() to
                           // ..access this field
                           // .. (see pid_rec_index_raw)
  union {
    __u32 bit_fields2;
    struct {
      __u32 mr_index:20;    // module record index (index into start of module rec section)
      // .. (see mr_index_none)
      __u32 event_index:8;  // index into the events section
      __u32 tid_is_raw:1;   // tid is raw OS tid                                       06-25-99
      __u32 itp_pc:1;       // PC sample (TRUE=this is a Itanium(R) processor PC sample record)
      __u32 pid_rec_index_raw:1;  // pid_rec_index is raw OS pid
      __u32 mr_index_none:1;  // no mr_index (unknown module)
    };
  };
} sample_record_PC, *P_sample_record_PC;

// time stamp counter section   (see sample_record_section_flags in sample_file_header)
typedef struct sample_record_TSC_s {
  ULARGE_INTEGER tsc; // processor timestamp counter
} sample_record_TSC;

typedef struct module_record_s {
  __u16 rec_length;       // total length of this record (including this length, always __u32 multiple)
                          // ..output from sampler is variable length (pathname at end of record)
                          // ..sampfile builder moves path names to a separate "literal pool" area
                          // ..so that these records become fixed length, and can be treated as an array
                          // .. see modrec_fixed_len in header
  
  __u16 segment_type:2;   // V86, 16, 32, 64 (see MODE_ defines), maybe inaccurate for Windows* 95
                          // .. a 16 bit module may become a 32 bit module, inferred by
                          // ..looking at 1st sample record that matches the module selector
  __u16 load_event:1;     // 0 for load, 1 for unload
  __u16:13;               // reserved
  
  __u16 selector;         // code selector or V86 segment
  union {
    __u16 segment_name_length;  // length of the segment name if the segment_name_set bit is set
    __u16 reserved;
  };
  //   __u16    reserved;
  __u32 segment_number;   // segment number, Windows* 95 (and now Java*) can have multiple pieces for one module
  union {
    __u32 flags;          // all the flags as one dword
    struct {
      __u32 exe:1;        // this module is an exe
                          // __u32 global_moduleTB3 : 1;  // globally loaded module.  There may be multiple module records
      __u32 global_module:1;  // globally loaded module.  There may be multiple module records
                          // ..for a global module, but the samples will only point to the
                          // ..1st one, the others will be ignored.  NT's Kernel32
                          // ..is an example of this.  REVISIT this??
      __u32 bogus_win95:1;    // "bogus" Windows* 95 module.  By bogus, we mean a module that has
                          // ..a pid of 0, no length and no base.  selector actually used
                          // ..as a 32 bit module.
      __u32 pid_rec_index_raw:1;  // pid_rec_index is raw OS pid
      __u32 sample_found:1;   // at least one sample referenced this module
      __u32 tsc_used:1;   // tsc set when record written
      __u32 duplicate:1;  // 1st pass analysis has determined this is a duplicate load
      __u32 global_module_Tb5:1;  // module mapped into all processes on system
      __u32 segment_name_set:1;   // set if the segment name was collected (initially done for Xbox* collections)
      __u32 first_module_rec_in_process:1;    // if the pid_creates_tracked_in_module_recs flag is set 
                          //  in the SampleHeaderEx struct and this flag 
                          //  is set, the associated module indicates 
                          //  the beginning of a new process 
      __u32:22;           // reserved
    };
  };
  union {
    ULARGE_INTEGER length64; // module length
    __u32 length;         // module length (not used for 16 bit, bytes (mult of 16) for V86, bytes for 32 bit)
  };
  union {
    ULARGE_INTEGER load_addr64; // load address
    __u32 load_addr;      // load address (0 for 16 bit, bytes (mul of 16) for V86, bytes for 32 bit)
  };
  __u32 pid_rec_index;    // process ID rec index (index into  start of pid record section).
                          // .. (see pid_rec_index_raw).  If pid_rec_index == 0 and pid_rec_index_raw == 1
                          // ..then this is a kernel or global module.  Can validly
                          // ..be 0 if not raw (array index).  Use returnPid() to access this
                          // ..field
  __u32 load_sample_count; // sample count when module loaded (add 1 to this when comparing)
                          // .. if 0, loaded prior to sampling start
  __u32 unload_sample_count;  // sample count when module unloaded
  __u32 path;             // module path name (section offset on disk)
                          // ..when initally written by sampler name is at end of this
                          // ..struct, when merged with main file names are pooled at end
                          // ..of module_record Section so ModulesRecords can be
                          // ..fixed length
  __u16 path_length;      // path name length (inludes terminating \0)
  __u16 filename_offset;  // offset into path name of base filename
  union {
    __u32 segment_name;   // offset to the segment_name from the beginning of the 
                          //  module section in a processed module section 
                          //  (s/b 0 in a raw module record)
                          // in a raw module record, the segment name will follow the 
                          //  module name and the module name's terminating NULL char
    __u32 reserved2;
  } mr_sn;
  ULARGE_INTEGER tsc;     // time stamp counter when record written (see tsc_used)
} module_record;

typedef struct event_reg_set_ex_s {
  union {
    __u32 counter_number; // counter number to set
    struct {              // 
      __u32 data:24;      // depends on command
      __u32 command:8;    // see below
    };
  };
  __u32 event_ID;
  __u32 reserved;         // added to get the structure on an 8 byte boundary
  __u32 esr_value;        // value to set register with.  If 2 regs are set with
                          // ..1 esr_value (e.g. Pentium(R) processor) 2 event_reg_set entries are needed
                          // ..with the esr_value the same in both of them
  ULARGE_INTEGER esr_count;   // counter value (may be transformed depending on esr value)
} event_reg_set_ex;

// event_reg_set command definitions
#define ERS_CMD_SET_CONFIG_AND_COUNTER_REGS 0   // data field is logical counter number                          
                                                // esr_value is value of config register (max 32 bits)
                                                // esr_count is initial value of count register (value will be negated before write to counter if counter is count up)
#define ERS_CMD_WRITE_MSR  1   // data field is unused     
                               // esr_value is msr number       
                               // esr_count is msr value        
#define ERS_CMD_NOP        2   // driver can ignore this entry      

#define ERS_CMD_OS_EVENT   3   // sample based on OS event          
                               // ..esr_value = OS event ID (On Windows NT*, See KeProfileinterruptWithSource and enum in NTDDK.H)
                               // ..esr_count = event frequency

#define ERS_CMD_TBS_VTD    4   // esr_count field of event_reg_set_ex struct contains the sampling interval in microseconds
#define ERS_CMD_TBS_RTC    5   // esr_count field of event_reg_set_ex struct contains the sampling inteval in microseconds
#define ERS_CMD_TBS_STATCARD 6 // esr_count field of event_reg_set_ex struct contains the sampling inteval in microseconds
//#define ERS_CMD_EBS      7   // for consistency with other method commands (EBS is assumed if the other method
                               // commands not used - in other words this command is not required)
#define ERS_CMD_WRITE_PMC  8   //      

typedef struct event_reg_set_s {
  union {
    __u32 counter_number;      // counter number to set
    struct {                   // 
      __u32 data:24;           // depends on command
      __u32 command:8;         // see below
    } ers_data;
  } ers_counter_number;
  __u32 esr_value;             // value to set register with.  If 2 regs are s et with
                               // ..1 esr_value (e.g. Pentium(R) processor) 2 event_reg_set entries are needed
                               // ..with the esr_value the same in both of them
  ULARGE_INTEGER esr_count;    // counter value (may be transformed depending on esr value)
} event_reg_set;

typedef struct _INT_FRAME {
  __u32 eip;
  __u32 seg_cs;
  __u32 E_flags;
  __u32 reserved;
  __u64 csd;                   // unscrambled code segment descriptor
  ULARGE_INTEGER iip;
  ULARGE_INTEGER ipsr;
} INT_FRAME, *PINT_FRAME;

/*
  Pid Record.  Windows NT* and Windows* 95 can resue a pid number very rapidly.
  This presents a uniqueness problem for sampling of any significant duration, 
  especially if measurements are being taken on a system where lots of 
  process create / terminates have deliberately been induced.  One of these
  records is written for every process create and terminate.  Post analysis
  is used to convert the raw system pid in the sample records into section
  offset for these records, so that views by process can be meaningful even
  if the process id is rapidly reused by the system.
*/
typedef struct pid_record_s {
  __u16 rec_length;            // total length of this record (including this length, always __u32 multiple)
                               // ..output from sampler is variable length (pathname at end of record)
                               // ..sampfile builder moves path names to a separate "literal pool" area
                               // ..so that these records become fixed length, and can be treated as an array
  __u16 pid_event:1;           // 0 means created, 1 means destroyed (destroys not currently recorded)
  __u16 pid_manufactured:1;    // 1 means that pid record was "manufactured" by analyzer, the sampler  
                               // ..wrote no pid create record, its existence was inferred.  This
                               // ..is usually for pids in existence before sampling starts (no enum)
  __u16 name_generated:1;      // the name of this pid was generated                            
  __u16:13;                    // reserved
  __u32 os_pid;                // OS assigned pid value
  __u32 os_pid_instance;       // "instance" of OS pid, a duplication count of how many different pids  
                               // ..with the same os pid have occurred                                
  __u32 index_num;             // the index value for this record.  Stored in sample and module records. 
                               // ..saved in case this area needs sorting                             
  __u32 sample_count;          // sample count at pid creation
  __u32 sample_count_term;     // sample count at pid termination (inferred by analyzer)
  ULARGE_INTEGER tsc;          // time stamp counter at pid creation
  __u32 path;                  // exe path name (section offset on disk)
  __u16 path_length;           // exe path name length (inludes terminating \0)
  __u16 filename_offset;       // offset into path name of base exe filename
} pid_record;

// processor execution modes
#define MODE_UNKNOWN    99
// the following defines must start at 0
#define MODE_64BIT      3
#define MODE_32BIT      2
#define MODE_16BIT      1
#define MODE_V86        0

#endif   // _SAMPFILE_H
