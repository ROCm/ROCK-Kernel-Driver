/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef H_THH_H
#define H_THH_H

#include "thh_common.h"

#define THH_VMALLOC_THRESHOLD (2*MOSAL_SYS_PAGE_SIZE)

#ifdef __LINUX__
#define THH_SMART_MALLOC(size) ({                                          \
                                  void *p;                                 \
                                  if ( (size) > THH_VMALLOC_THRESHOLD ) {  \
                                    p = VMALLOC(size);                  \
                                  }                                     \
                                  else {                                \
                                    p = MALLOC(size);                   \
                                  }                                     \
                                  p;                                    \
                               })




#define THH_SMART_FREE(ptr,size) do {                                      \
                                   if ( (size) > THH_VMALLOC_THRESHOLD )   {  \
                                     VFREE(ptr);                           \
                                   }                                       \
                                   else {                                  \
                                     FREE(ptr);                            \
                                   }                                       \
                                 }                                         \
                                 while(0)
#else
#define THH_SMART_MALLOC(size) VMALLOC(size)
#define THH_SMART_FREE(ptr,size) VFREE(ptr)
#endif                                 

#define THH_FW_VER_VALUE(major,minor,subminor)  \
  ( (((u_int64_t)(major)) << 32) | (((u_int64_t)(minor)) << 16) | ((u_int64_t)(subminor)) )


/* THH objects handles */
typedef struct THH_hob_st     *THH_hob_t;
typedef MT_ulong_ptr_t THH_cmd_t; /* type to identify the cmdif object */
typedef struct THH_eventp_st  *THH_eventp_t;
typedef struct THH_ddrmm_st   *THH_ddrmm_t;
typedef struct THH_uldm_st    *THH_uldm_t;
typedef struct THH_mrwm_st    *THH_mrwm_t;  
typedef struct THH_cqm_st     *THH_cqm_t;
typedef struct THH_qpm_st     *THH_qpm_t;
typedef struct THH_srqm_st     *THH_srqm_t;
typedef struct THH_mcgm_st    *THH_mcgm_t;
typedef struct THH_sqp_demux_st *THH_sqp_demux_t;



/* event's handlers types */
typedef u_int8_t THH_event_type_t;
typedef u_int8_t THH_event_subtype_t;
typedef void (*THH_mlx_eventh_t)(HH_hca_hndl_t hh_hndl, 
                                 THH_event_type_t event_type,
                                 THH_event_subtype_t event_subtype,
                                 void* event_data, 
                                 void* private_data);

typedef union {
  HH_comp_eventh_t comp_event_h;
  HH_async_eventh_t ib_comp_event_h;
  THH_mlx_eventh_t mlx_event_h;
}THH_eventp_handler_t;

/* structure for passing module flags or parameters from 'insmod' to THH_hob_create */
typedef struct THH_module_flags_st {
    MT_bool legacy_sqp;  /* TRUE if should perform INIT_IB in THH_hob_open_hca */
    MT_bool av_in_host_mem; /* TRUE if udav's should use host memory. */
                            /* FALSE if udav's should use DDR SDRAM on Tavor */
    MT_bool inifinite_cmd_timeout; /* when TRUE cmdif will wait infinitely for the completion of a command */
    MT_bool fatal_delay_halt; /* when TRUE, HALT_HCA/disable on fatal error will be delayed to before the reset */
    u_int32_t num_cmds_outs; /* max number of outstanding commands that will be used by the driver
                                The real value will not exceed tha value reported by fw */
    u_int32_t async_eq_size; /* The size of the async event queue (max # of outstanding async events) */
    MT_bool cmdif_post_uar0; /* when TRUE cmdif will post commands to uar0 */
} THH_module_flags_t;

/*
 * THH_hob_state_t tracks the status of an HCA -- is it OK, or has a fatal error occurred.
 * Actually, the states used in practice use the FATAL states as modifiers of the base states.
 * Thus, the states we may see in practice are:
 *   THH_STATE_CREATING, THH_STATE_OPENING, THH_STATE_RUNNING,  THH_STATE_CLOSING, THH_STATE_DESTROYING
 *     and fatal modifiers on these states:
 *                            
 *      THH_STATE_CREATING | THH_STATE_FATAL_HCA_HALTED
 *      THH_STATE_OPENING | THH_STATE_FATAL_HCA_HALTED
 * 
 *      THH_STATE_RUNNING | THH_STATE_FATAL_START
 *      THH_STATE_RUNNING | THH_STATE_FATAL_HCA_HALTED
 *
 *      THH_STATE_CLOSING | THH_STATE_FATAL_HCA_HALTED
 *      THH_STATE_DESTROYING | THH_STATE_FATAL_HCA_HALTED
 *
 *      Note that in the RUNNING state, have two FATAL possibilities.  When FATAL first occurs,
 *      we enter the RUNNING/FATAL_START state, in which all commands and all calls to THH
 *      (with very few exceptions) return FATAL.  In addition, we attempt to halt the HCA.
 *      After the halt-hca attempt returns, we enter the RUNNING/FATAL-HCA-HALTED state.
 *                
 */
enum {
    THH_STATE_NONE      = 0,
    THH_STATE_CREATING  = 0x1, 
    THH_STATE_CLOSED    = 0x2, 
    THH_STATE_OPENING   = 0x4, 
    THH_STATE_RUNNING   = 0x8, 
    THH_STATE_CLOSING   = 0x10, 
    THH_STATE_DESTROYING= 0x20, 
    THH_STATE_FATAL_START  = 0x40,       /* CATASTROPHIC EVENT has been reported */
    THH_STATE_FATAL_HCA_HALTED = 0x80   /* Failed HCA has been halted           */
};
typedef u_int32_t THH_hob_state_t;

#define THH_STATE_HAVE_ANY_FATAL   (THH_STATE_FATAL_START | THH_STATE_FATAL_HCA_HALTED)

/* Fatal event type enumeration, for passing to fatal event handlers */
typedef enum {
    THH_FATAL_NONE, 
    THH_FATAL_MASTER_ABORT,  /* detected master abort */
    THH_FATAL_GOBIT,         /* GO bit of HCR remains set (i.e., stuck) */
    THH_FATAL_CMD_TIMEOUT,   /* timeout on a command execution */
    THH_FATAL_EQ_OVF,        /* an EQ has overflowed */
    THH_FATAL_EVENT,         /* firmware has generated a LOCAL CATASTROPHIC ERR event */
    THH_FATAL_CR,            /* unexpected read from CR-space */
    THH_FATAL_TOKEN,         /* invalid token on command completion */
    THH_FATAL_END            /* indicates end of fatal error codes */
} THH_fatal_err_t;

#endif  /* H_THH_H */
