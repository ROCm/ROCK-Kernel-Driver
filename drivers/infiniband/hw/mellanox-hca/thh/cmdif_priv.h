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

#ifndef __cmdif_priv_h
#define __cmdif_priv_h

#include <mtl_types.h>
#include <mosal.h>
#include <MT23108.h>
#include <cmd_types.h>
#include <thh.h>
#include <thh_hob.h>

#define PSEUDO_MT_BYTE_SIZE(x) (sizeof(struct x) >> 3)


typedef struct {
    u_int32_t in_param[2];   /* Input Parameter: 64 bit parameter or 64 bit pointer to input mailbox (see command description) */
    u_int32_t input_modifier;/* Input Parameter Modifier */
    u_int32_t out_param[2]; /* Output Parameter: 64 bit parameter or 64 bit pointer to output mailbox (see command description) */
    u_int16_t token;        /* Software assigned token to the command, to uniquely identify it. The token is returned to the software in the EQE reported. */
    u_int16_t opcode;       /* Command opcode */
    u_int8_t opcode_modifier;/* Opcode Modifier, see specific description for each command. */
    u_int8_t e;              /* Event Request\;0 - Don't report event (software will poll the G bit)\;1 - Report event to EQ when the command completes */
    u_int8_t go;           /* Go (0=Software ownership for the HCR, 1=Hardware ownership for the HCR)\;Software can write to the HCR only if Go bit is cleared.\;Software must set the Go bit to trigger the HW to execute the command. Software must not write to this register value other than 1 for the Go bit. */
    u_int8_t status;       /* Command execution status report. Valid only if command interface in under SW ownership (Go bit is cleared)\;0 - command completed without error. If different than zero, command execution completed with error. Syndrom encoding is depended on command executed and is defined for each command */
}
priv_hcr_t;

typedef struct {
  MOSAL_syncobj_t *syncobj_p; /* pointer to synchronization object */
  u_int16_t token_val; /* the value of the token */
  priv_hcr_t hcr;
  MT_bool in_use; /* free if 0, otherwise used */
  MT_bool signalled;
}
wait_context_t;


typedef enum {
  TRANS_NA,
  TRANS_IMMEDIATE,
  TRANS_MAILBOX
}
trans_type_t;

typedef struct {
  u_int8_t *in_param; /* holds the virtually contigious buffer of the parameter block passed */
  u_int8_t *in_param_va; /* used internaly to hold the address of the allocated physical contigious buffer
                             - no need to initialize by the wrapper */ 
  u_int32_t in_param_size;
  trans_type_t in_trans;

  u_int32_t input_modifier;

  u_int8_t *out_param; /* holds the virtually contigious buffer of the parameter block passed */
  u_int8_t *out_param_va; /* used internaly to hold the address of the allocated physical contigious buffer
                             - no need to initialize by the wrapper */ 
  u_int32_t out_param_size;
  trans_type_t out_trans;

  tavor_if_cmd_t opcode;
  u_int8_t opcode_modifier;

  u_int32_t exec_time_micro;
}
command_fields_t;


typedef struct {
  MT_virt_addr_t wide_pool; /* pointer to base address of the pool, which is not aligned */
  MT_virt_addr_t pool; /* pointer to start address of the pool - this address is aligned */
  MT_phys_addr_t pool_pa; /* physical address of the pool */
  u_int32_t num_bufs; /* number of buffers in the pool */
  MT_size_t buf_size; /* size of a buffer in the pool */
  void **buf_ptrs; /* array holding pointers to the buffers in the pool */
}
pool_control_t;

#define TOKEN_VALUES_BASE (1+0) /* the '1' must be there to ensure the value of UNALLOCATED_TOKEN is valid */
#define UNALLOCATED_TOKEN 0
#define MAX_IN_PRM_SIZE 0x400 /* is this the right value ?? */
#define MAX_OUT_PRM_SIZE 0x400 /* is this the right value ?? */
#define LOOP_DELAY_TRESHOLD 10000  /* beyond 10 msec we make the deay in loop */

/*================ type definitions ================================================*/

typedef struct {
  MT_virt_addr_t prm_base_va; /* base virtual address of params buffer */
  MT_phys_addr_t prm_base_pa; /* base physical address of params buffer */
  MT_size_t prm_buf_sz; /* size of allocated buffer used for params */ 
  MT_virt_addr_t prm_alloc_va; /* pointer to allocated buffer. useful when params
                                  are in main memory and due to alignment requirements
                                  prm_base_va may be higher then the allocated buffer
                                  pointer */
  MT_bool in_ddr; /* TRUE if prms are in ddr */
  int allocated; /* set to 1 when the object has been allocated to aid in cleanup */
}
prms_buf_t;

#define HCR_SIZE 8
/* type which contains all the resources used to execute a command */
typedef struct {
  u_int16_t token; /* the value used in the token field */
  MOSAL_syncobj_t syncobj; /* pointer to synchronization object */
  prms_buf_t in_prm;
  prms_buf_t out_prm;
  u_int32_t ref_cnt;         /* 0=entry not in use >0 in use */
  priv_hcr_t hcr;            /* used to pass command results from event handler to the process */
  u_int32_t next_free_idx;   /* indes of the next free element in the array */
  u_int32_t entry_idx;       /* index of this entry in the array */
  u_int32_t hcr_buf[HCR_SIZE]; /* this buffer contains the image to be written to uar0 */
} 
cmd_ctx_t;


typedef struct {
  cmd_ctx_t *ctx_arr; /* pointer to an array of command contexts */
  u_int32_t num; /* number of aloocated contexts */
  MOSAL_spinlock_t spl; /* spinlock to protect the data */
  u_int32_t free_list_head;
}
ctx_obj_t;


struct cmd_if_context_st {
  THH_hob_t      hob;
  void *hcr_virt_base; /* virtual address base of the HCR */
  void *uar0_virt_base; /* virtual address base of the HCR */
  MT_bool sys_enabled; /* true if THH_CMD_SYS_EN has been executed successfully */
  volatile THH_eqn_t eqn;  /* eqn used by this interface */
  MOSAL_spinlock_t eqn_spl;
  MOSAL_mutex_t sys_en_mtx; /* mutex used during execution of SYS_EN */
  MOSAL_semaphore_t no_events_sem; /* semaphore used during execution when EQN is not yet set */
  MOSAL_mutex_t hcr_mtx;       /* mutex used to protect the hcr */
  MOSAL_semaphore_t use_events_sem; /* semaphore used while executing commands when EQN is set */
  MOSAL_semaphore_t fw_outs_sem;

  u_int32_t max_outstanding; /* max number of outstanding commands possible */
  u_int32_t queried_max_outstanding; /* max number of outstanding commands supported by FW */
  u_int32_t req_num_cmds_outs; /* requested number of outstanding commands */

  THH_ddrmm_t ddrmm; /* handle to ddr memory manager */
  pool_control_t in_prm_pool; /* used for managing memory for in params */
  MT_phys_addr_t phys_mem_addr; /* physical address allocated */
  MT_offset_t phys_mem_size; /* size of allocated physical memory */

  pool_control_t out_prm_pool; /* used for managing memory for out params */

  MT_phys_addr_t ddr_prms_buf; /* physical address in ddr to be used with input mailboxes */

  u_int8_t tokens_shift;
  u_int16_t tokens_idx_mask;
  u_int16_t tokens_counter;

  MT_bool inf_timeout; /* when TRUE cmdif will wait inifinitely for the completion of a command */

  MT_bool in_at_ddr, out_at_ddr; /* where are input and output params located */

  unsigned int sw_num_rsc; /* number pf software resources */

  ctx_obj_t ctx_obj; /* object that contains resources needed for executing commands */
  MT_bool query_fw_done; /* set to TRUE after the first time calling query FW and allocating resources */
  THH_fw_props_t fw_props; /* valid when query_fw_done is TRUE  (after QUERY_FW) */
  u_int64_t counts_busy_wait_for_go; /* number of cpu clocks to busy wait for the go bit */
  u_int64_t short_wait_for_go; /* short busy wait for go */
  u_int64_t counts_sleep_wait_for_go; /* number of cpu clocks to wait for the go bit by suspending  */

#ifdef GO_MT_BIT_TIME_DEBUG
  u_int32_t go_wait_counts[sizeof(count_levels)/sizeof(u_int32_t)+1];
#endif
  volatile MT_bool have_fatal; /* set if a fatal error has occurred */
  MOSAL_spinlock_t  close_spl;
  MT_bool close_action;
  MOSAL_syncobj_t  fatal_list; /* list of processes waiting untill HCA is closed */

  MT_bool post_to_uar0; /* when true cmds with events are posted like doorbels */

  MOSAL_spinlock_t ctr_spl;
  unsigned long events_in_pipe;
  unsigned long poll_in_pipe;

  u_int16_t *track_arr;
};

typedef struct {
  u_int32_t addr_h;
  u_int32_t addr_l;
}
addr_64bit_t;

#define PRM_ALIGN_SHIFT 4 /* alignment required in params buffers */



THH_cmd_status_t cmd_invoke(THH_cmd_t cmd_if, command_fields_t *cmd_prms);
THH_cmd_status_t allocate_prm_resources(THH_cmd_t cmd_if, THH_fw_props_t *fw_props, MT_bool ddr);
MT_bool valid_handle(THH_cmd_t cmd_if);
void va_pa_mapping_helper(THH_cmd_t cmd_if, MT_virt_addr_t va, MT_phys_addr_t pa);


/*================ external definitions ============================================*/
#ifdef DEBUG_MEM_OV
extern MT_phys_addr_t cmdif_dbg_ddr; /* address in ddr used for out params in debug mode */
#endif

#endif /* __cmdif_priv_h */
