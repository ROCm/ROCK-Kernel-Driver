/*
 *  vtuneshared.h
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
 *	File: vtuneshared.h
 *
 *	Description: sampling data structures shared between driver and library
 *
 *	Author(s): George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#ifndef _VTUNESHARED_H
#define _VTUNESHARED_H

#include "driversharedarea.h"

/*
******************************************************************************
*           Linux* driver shared defs
******************************************************************************
*/

#define DEV_SAMPLING_DRIVER     "/dev/vtune_d"  // device 0
#define DEV_MODULES             "/dev/vtune_m"  // device 1
#define DEV_PID_CREATES         "/dev/vtune_p"  // device 2

#define VLIB_MAPFUNC            50

/* Error Codes: general */
#define LIB_RET_SUCCESS            0
#define LIBERR_DEVICE_FAIL         100
#define LIBERR_IOCTL_FAIL          102
#define LIBERR_DEVICE_CONFIG       104
#define LIBERR_CPUINFO_FAIL        106
#define LIBERR_SYSINFO_FAIL        108
#define LIB_SAMP_CANCEL            110
/* Error Codes: sampling record related */
#define LIBERR_VTEMP_CREATE        200
#define LIBERR_SAMPF_CREATE        202
#define LIBERR_PCWRITE             204
#define LIBERR_FTEMPSEEK           206
#define LIBERR_SAMPSEEK            208
#define LIBERR_SAMPREAD            210
#define LIBERR_SAMPWRITE           212
#define LIBERR_USERSAMPALLOC_FAIL  214
/* Error Codes: module record related */
#define LIBERR_DIRPROC_FAIL        300
#define LIBERR_MODF_CREATE         302
#define LIBERR_MODDEV_FAIL         304
#define LIBERR_MODQUERY            306
#define LIBERR_MODOPEN             308
#define LIBERR_MODINFO             310
#define LIBERR_MODWRITE            312
#define LIBERR_USERMODALLOC_FAIL   314
/* Error Codes: pid record related */
#define LIBERR_PIDDEV_OPEN_FAIL    400
#define LIBERR_PIDFILE_CREATE_FAIL 402
#define LIBERR_PIDFILE_OPEN_FAIL   404
#define LIBERR_PIDREC_WRITE_FAIL   406

/* Sampling Driver State */
#define SINFO_STARTED             0x08  // sampling started
#define SINFO_WRITE               0x04  // write procedure scheduled
#define SINFO_STOPPING            0x02  // driver is stopping sampling
#define SINFO_STOP_COMPLETE       0x01  // driver completed "stop sampling" cmd
#define SINFO_EBS_RESTART         0x20  // restart EBS
#define SINFO_DO_STOP             0x40  // schedule stop
#define SINFO_DO_WRITE            0x80  // schedule write

/* NMI type */
#define EISA_NMI               1
#define QUIX_NMI               2
#define STAT_NMI               3
#define EMON_NMI               4

/* Driver IOCTL Defs */
#define VTUNE_CONFIG           1  // return # of processors
#define VTUNE_START            2  // return 0 or 1, is prf enabled?
#define VTUNE_STOP             3  // return number of text symbols loaded
#define VTUNE_STAT             4  // return size of symbol names
#define VTUNE_ABORT            5  // users abort collection data
#define VTUNE_PARM             6  // return user configure pararameters
#define VTUNE_DUMP             7  // dump system debug info
#define VTUNE_SYSINFO          9  // return os system info
#define VTUNE_CONFIG_EX       12  // new model config
#define VTUNE_START_EX        15  // new model start
#define VTUNE_READPERF        18  // read perf data
#define VTUNE_GETCPUINFO      19  // get cpu info
#ifdef ENABLE_TGID
#define VTUNE_GETTGRP         21  // get thread group info	
#endif

#define PS_VM                 0x00020000  // virtual 86 mode flag

#define MAXNAMELEN            1024  // BUG: should not be fixed value (Windows* limit is 256) (and 2048 is too large for some kernels)

/*
 *  Sample information structure... maintained by Linux* driver
 */
typedef struct _Samp_info {
  int sample_time;        // total sample time in milleseconds
  int profile_ints_idt;   // total physical profile interrupts during  sampling
  int profile_ints;       // total profile interrupts while sampling  (callback from OS)
  int sample_count;       // total samples taken. Current sample number.
  int test_IO_errs;       // number of times that VxD IO request was delayed (Windows* 95)
  int sampling_active;    // pc sampling active (0 = pc sampling suspended)
  int flags;              // flags
  int sample_rec_length;  // length of sample record (to be deprecated ... use DSA instead)
  int rsvd2;              // reserved 2
  char cpu_family;        // cpu family
  char cpu_model;         // cpu model
  char cpu_stepping;      // cpu stepping
  char rsvd3;             // reserved 3
} sampinfo_t;

#define FSET_DRV_PROCESS_CREATES_TRACKED_IN_MODULE_RECS 0x1

#ifdef ENABLE_TGID
typedef struct thread_info_s
{
  int tgrp_id;
} thread_info;
#endif

#endif   // _VTUNESHARED_H
