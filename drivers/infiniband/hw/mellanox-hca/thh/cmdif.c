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

#include <cmdif_priv.h>
#include <cmdif.h>
#include <tddrmm.h>
#include <mosal.h>
#include <MT23108.h>



/*================ macro definitions ===============================================*/
#ifdef THH_CMD_TIME_TRACK
extern u_int64_t THH_eventp_last_cmdif_interrupt;
#endif

#define PRM_RSC_DELTA 32
#define MAX_CMD_OBJS 32 /* max number of objects allowed to be created */
#define CMD_ETIME_EVENTS 1000000 /* max execution time is fixed when we're using events */
#define MAX_ITER_ON_EINTR 10
#define MAX_UC_FOR_GO 100 /* time in microsec to do busy wait on go bit */
#define MAX_UC_SLEEP_FOR_GO 300000000 /* time in microsec to do busy wait on go bit */
#define SHORT_UC_DELAY_FOR_GO 10 /* time in microsec to do busy wait on go bit after first time */

#ifdef IN
  #undef IN
#endif
#define IN

#ifdef OUT
  #undef OUT
#endif
#define OUT

#define HCR_DW_BYTE_OFFSET(field) (MT_BYTE_OFFSET(tavorprm_hca_command_register_st,field) & (~3))
#define HCR_DW_BIT_OFFSET(field) (MT_BIT_OFFSET(tavorprm_hca_command_register_st,field) - HCR_DW_BYTE_OFFSET(field) * 8)
#define HCR_BIT_SIZE(field) MT_BIT_SIZE(tavorprm_hca_command_register_st,field)


#define PREP_TOKEN_DW(token_dw, val) MT_INSERT32((token_dw),(val),HCR_DW_BIT_OFFSET(token),MT_BIT_SIZE(tavorprm_hca_command_register_st,token)); \
                                     token_dw = MOSAL_cpu_to_be32(token_dw);



#define DEFAULT_TOKEN 0x1234 /* default token value to use when there can be no outstanding commands */
#define FREE_LIST_EOL ((u_int32_t)(-1))

#define NEW_EQE_FORMAT





/*================ static variables definitions ====================================*/



/*================ static functions prototypes =====================================*/
static THH_cmd_status_t sys_en_hca(struct cmd_if_context_st *entry);
static THH_cmd_status_t main_cmd_flow(struct cmd_if_context_st *entry, command_fields_t *cmd_prms);
static MT_bool cmdif_is_free(struct cmd_if_context_st *entry);
static void cleanup_cmdobj(struct cmd_if_context_st *entry);
static inline void  write_command_dw(u_int32_t *dst_buf, u_int8_t go,
                                     u_int8_t use_event, u_int8_t op_mod, u_int16_t opcode);
static inline THH_cmd_status_t cmd_if_status(struct cmd_if_context_st *entry);
static inline void set_mailbox(u_int32_t *dst_buf, MT_phys_addr_t mbx_pa);
static inline void ptr_to_mailbox_ptr(MT_phys_addr_t  ptr, addr_64bit_t *mbx_pmtr);
static inline void cvt_be32_to_cpu(void *buf, u_int32_t size);
static inline void cvt_cpu_to_be32(void *buf, u_int32_t size);
static void *memcpy_to_tavor(void *dst, const void *src, MT_size_t size);
static void *memcpy_from_tavor(void *dst, const void *src, MT_size_t size);
static inline u_int64_t d32_to_s64(u_int32_t hi, u_int32_t lo);
static THH_cmd_status_t cmd_flow_events(struct cmd_if_context_st *entry, command_fields_t *cmd_prms);
static THH_cmd_status_t cmd_flow_no_events(struct cmd_if_context_st *entry, command_fields_t *cmd_prms);
/* ===> parse functions <=== */
static void parse_HCR(u_int32_t *result_hcr_image_p, priv_hcr_t *hcr);
#ifdef NEW_EQE_FORMAT
static void parse_new_HCR(u_int32_t *result_hcr_image_p, priv_hcr_t *hcr);
#endif
static void edit_hcr(struct cmd_if_context_st *entry, command_fields_t *cmd_prms, u_int16_t token, cmd_ctx_t *ctx_p, int event);
static void extract_hcr(command_fields_t *cmd_prms, priv_hcr_t *hcr);

static void print_outs_commands(ctx_obj_t *ctxo_p);
static void track_exec_cmds(struct cmd_if_context_st *entry, cmd_ctx_t *ctx_p);
static void print_track_arr(struct cmd_if_context_st *entry);

/* ===> pool handling <=== */
static HH_ret_t alloc_cmd_contexts(struct cmd_if_context_st *entry, u_int32_t num, MT_bool in_at_ddr, MT_bool out_at_ddr);
static HH_ret_t de_alloc_cmd_contexts(struct cmd_if_context_st *entry);
static HH_ret_t acq_cmd_ctx(struct cmd_if_context_st *entry, cmd_ctx_t **ctx_pp);
static void rel_cmd_ctx(struct cmd_if_context_st *entry, cmd_ctx_t *ctx_p);
static HH_ret_t re_alloc_resources(struct cmd_if_context_st *entry, MT_bool in_at_ddr, MT_bool out_at_ddr);

static int log2(u_int64_t arg);


/* ===> print functions <=== */
//static void print_hcr_dump(struct cmd_if_context_st *entry, u_int32_t cmd);
//static void print_hcr_fileds(u_int32_t *buf, u_int32_t cmd);

/********************************** UTILS *********************************/
static void cmd_hexdump( void *buf, int size, const char * dump_title )
{
    int i, j, maxlines, bytes_left, this_line;
    char linebuf[200], tempout[20];
    u_int8_t *iterator;

    
    iterator = (u_int8_t *)buf;
    bytes_left = size;
    if (size <= 0) {
        return;
    }

    MTL_ERROR1("%s, starting at addr 0x%p, size=%d:\n",
               dump_title, buf, size);
    

    maxlines = (size / 16) + ((size % 16) ? 1 : 0);

    for (i = 0; i < maxlines; i++) {
        memset(linebuf, 0, sizeof(linebuf));
        this_line = (bytes_left > 16 ? 16 : bytes_left);

        for (j = 0; j < this_line; j++) {
            if ((j % 4) == 0) {
                strcat(linebuf," ");
            }
            sprintf(tempout, "%02x", *iterator);
            iterator++; bytes_left--;
            strcat(linebuf,tempout);
        }
        MTL_ERROR1("%s\n", linebuf);
    }
    MTL_ERROR1("%s END\n", dump_title);
}

static void dump_cmd_err_info(cmd_ctx_t *ctx_p, command_fields_t *cmd_prms) 
{
    MTL_ERROR1("CMD ERROR DUMP. opcode=0x%x, opc_mod = 0x%x, exec_time_micro=%u\n",
               (u_int32_t)cmd_prms->opcode, cmd_prms->opcode_modifier, cmd_prms->exec_time_micro);
    cmd_hexdump((void *) ctx_p->hcr_buf,HCR_SIZE*sizeof(u_int32_t), "HCR dump"); 
    if ((cmd_prms->in_trans == TRANS_MAILBOX) && (cmd_prms->in_param_size > 0)) {
        cmd_hexdump((void *) ctx_p->in_prm.prm_alloc_va,cmd_prms->in_param_size, "IN MAILBOX dump"); 
    }
    if ((cmd_prms->out_trans == TRANS_MAILBOX) && (cmd_prms->out_param_size > 0)) {
        cmd_hexdump((void *) ctx_p->out_prm.prm_alloc_va,cmd_prms->out_param_size, "OUT MAILBOX dump"); 
    }
}

/* ==== inline functions ===============*/

static HH_ret_t inline get_ctx_by_idx(struct cmd_if_context_st *entry, u_int16_t idx, cmd_ctx_t **ctx_pp)
{
  if ( (idx<entry->ctx_obj.num) && (entry->ctx_obj.ctx_arr[idx].ref_cnt>0) ) {
    *ctx_pp = &entry->ctx_obj.ctx_arr[idx];
    return HH_OK;
  }
  return HH_EAGAIN;
}

static THH_eqn_t inline eqn_set(struct cmd_if_context_st *entry, THH_eqn_t new_eqn)
{
  THH_eqn_t old_eqn;

  MOSAL_spinlock_irq_lock(&entry->eqn_spl);
  old_eqn = entry->eqn;
  entry->eqn = new_eqn;
  MOSAL_spinlock_unlock(&entry->eqn_spl);
  return old_eqn;
}


/*
 *  write_command_dw
 */
static inline void  write_command_dw(u_int32_t *dst_buf, u_int8_t go,
                                     u_int8_t use_event, u_int8_t op_mod, u_int16_t opcode)
{
  u_int32_t cmd=0;

  MT_INSERT32(cmd,opcode,HCR_DW_BIT_OFFSET(opcode),HCR_BIT_SIZE(opcode));
  MT_INSERT32(cmd,op_mod,HCR_DW_BIT_OFFSET(opcode_modifier),HCR_BIT_SIZE(opcode_modifier));
  MT_INSERT32(cmd,use_event,HCR_DW_BIT_OFFSET(e),HCR_BIT_SIZE(e));
  MT_INSERT32(cmd,go,HCR_DW_BIT_OFFSET(go),HCR_BIT_SIZE(go));
  MT_INSERT32(cmd,0,HCR_DW_BIT_OFFSET(status),HCR_BIT_SIZE(status)); /* status */
  *dst_buf = MOSAL_cpu_to_be32(cmd);
}


/*
 *  ptr_to_mailbox_ptr
 */
static inline void ptr_to_mailbox_ptr(MT_phys_addr_t ptr, addr_64bit_t *mbx_pmtr)
{
  if ( sizeof(ptr) == 4 ) {
    MTL_DEBUG4("pointer is 32 bit\n");
    mbx_pmtr->addr_h = 0;
    mbx_pmtr->addr_l = (u_int32_t)ptr;
  }
  else if ( sizeof(ptr) == 8 ) {
    MTL_DEBUG4("pointer is 64 bit\n");
    mbx_pmtr->addr_h = (u_int32_t)(((u_int64_t)ptr)>>32);
    mbx_pmtr->addr_l = (u_int32_t)ptr;
  }
  else {
    MTL_ERROR1("bad address pointer size: %d\n",(int)sizeof(ptr));
  }
  mbx_pmtr->addr_h = MOSAL_cpu_to_be32(mbx_pmtr->addr_h);
  mbx_pmtr->addr_l = MOSAL_cpu_to_be32(mbx_pmtr->addr_l);
}

/*================ global functions definitions ====================================*/

/*
 *  THH_cmd_create
 */
HH_ret_t THH_cmd_create(THH_hob_t hob, u_int32_t hw_ver, MT_phys_addr_t cr_base, MT_phys_addr_t uar0_base, THH_cmd_t *cmd_if_p,
                        MT_bool inf_timeout, u_int32_t num_cmds_outs)
{
  struct cmd_if_context_st *entry;
  HH_ret_t rc;
  u_int64_t cps;

  FUNC_IN;

  /* allocate memory for cmdif object */
  entry = TMALLOC(struct cmd_if_context_st);
  if ( !entry ) {
    MTL_ERROR1(MT_FLFMT("%s: failed to allocate memory for cmdif object"), __func__);
    MT_RETURN(HH_EAGAIN);
  }
  memset(entry, 0, sizeof(struct cmd_if_context_st));

  /* check if we should post commands UAR0 */
  if ( uar0_base == (MT_phys_addr_t) MAKE_ULONGLONG(0xFFFFFFFFFFFFFFFF) ) {
    entry->post_to_uar0 = FALSE;
  }
  else {
    entry->post_to_uar0 = TRUE;
    MTL_ERROR1(MT_FLFMT("%s: posting to uar0"), __func__);
  }

  entry->hob = hob;
  entry->hcr_virt_base = (void *)MOSAL_io_remap(cr_base + TAVOR_HCR_OFFSET_FROM_CR_BASE,
                                                sizeof(struct tavorprm_hca_command_register_st)/8);

  /* map hcr to kernel space */
  if ( !entry->hcr_virt_base ) {
    MTL_ERROR1(MT_FLFMT("%s: MOSAL_io_remap() failed. pa="PHYS_ADDR_FMT", size=" SIZE_T_FMT), __func__,
               cr_base + TAVOR_HCR_OFFSET_FROM_CR_BASE, sizeof(struct tavorprm_hca_command_register_st)/8);
    cleanup_cmdobj(entry);
    MT_RETURN(HH_EAGAIN);
  }

  /* if we're going to post to uar0 we need to map 8 bytes from UAR0 to kernel space */
  if ( entry->post_to_uar0 ) {
    entry->uar0_virt_base = (void *)MOSAL_io_remap(uar0_base, 8);
    if ( !entry->uar0_virt_base ) {
      cleanup_cmdobj(entry);
      MT_RETURN(HH_EAGAIN);
    }
  }

  entry->sys_enabled = FALSE;
  entry->ddrmm = THH_DDRMM_INVALID_HANDLE;
  entry->eqn = THH_INVALID_EQN;

  entry->inf_timeout = inf_timeout;
  entry->req_num_cmds_outs = num_cmds_outs;

  entry->tokens_shift = 0;
  entry->tokens_counter = 0;
  entry->tokens_idx_mask = (1<<entry->tokens_shift)-1;

  cps = MOSAL_get_counts_per_sec();
  if ( cps & MAKE_ULONGLONG(0xffffffff00000000) ) {
    MTL_ERROR1(MT_FLFMT("%s: *** delay time calculation for go bit will not be accurate !!!"), __func__);
  }
  entry->counts_busy_wait_for_go = (u_int64_t)(((u_int32_t)cps)/1000000) * MAX_UC_FOR_GO;
  entry->counts_sleep_wait_for_go = (u_int64_t)(((u_int32_t)cps)/1000000) * MAX_UC_SLEEP_FOR_GO;
  entry->short_wait_for_go = (u_int64_t)(((u_int32_t)cps)/1000000) * SHORT_UC_DELAY_FOR_GO;

  entry->in_at_ddr = FALSE;
  entry->out_at_ddr = FALSE;
  rc = alloc_cmd_contexts(entry, 1, entry->in_at_ddr, entry->in_at_ddr);
  if ( rc != HH_OK ) {
    MTL_ERROR1(MT_FLFMT("failed to allocate command contexts"));
    cleanup_cmdobj(entry);
    MT_RETURN(HH_EAGAIN);
  }

  entry->track_arr = TNMALLOC(u_int16_t, 256);
  if ( !entry->track_arr ) {
    cleanup_cmdobj(entry);
    MT_RETURN(HH_EAGAIN);
  }
  memset(entry->track_arr, 0, sizeof(u_int16_t)*256);

  MOSAL_mutex_init(&entry->sys_en_mtx);
  MOSAL_sem_init(&entry->no_events_sem, 1);
  MOSAL_mutex_init(&entry->hcr_mtx);

  MOSAL_sem_init(&entry->use_events_sem, 0);
  MOSAL_sem_init(&entry->fw_outs_sem, 0);

  MOSAL_spinlock_init(&entry->close_spl);
  entry->close_action = FALSE;
  MOSAL_syncobj_init(&entry->fatal_list);

  MOSAL_spinlock_init(&entry->eqn_spl);

  MOSAL_spinlock_init(&entry->ctr_spl);
  entry->events_in_pipe = 0;
  entry->poll_in_pipe = 0;
  
  *cmd_if_p = (THH_cmd_t)entry;

  MT_RETURN(HH_OK);
}



/*
 *  THH_cmd_destroy
 */
HH_ret_t THH_cmd_destroy(THH_cmd_t cmd_if)
{
  struct cmd_if_context_st *entry = (struct cmd_if_context_st *)cmd_if;

  FUNC_IN;


  MOSAL_mutex_free(&entry->sys_en_mtx);
  MOSAL_sem_free(&entry->no_events_sem);
  MOSAL_sem_free(&entry->use_events_sem);
  MOSAL_sem_free(&entry->fw_outs_sem);
  MOSAL_syncobj_free(&entry->fatal_list);
  
  cleanup_cmdobj(entry);

  MT_RETURN(HH_OK);
}


/*
 *  THH_cmd_set_fw_props
 */
THH_cmd_status_t THH_cmd_set_fw_props(THH_cmd_t cmd_if, THH_fw_props_t *fw_props)
{
  struct cmd_if_context_st *entry = (struct cmd_if_context_st *)cmd_if;
  unsigned int i;

  FUNC_IN;
  if ( entry->query_fw_done==FALSE ) {
    entry->queried_max_outstanding = 1<<fw_props->log_max_outstanding_cmd;
    if ( entry->req_num_cmds_outs <= entry->queried_max_outstanding ) {
      entry->max_outstanding = entry->req_num_cmds_outs;
    }
    else {
      entry->max_outstanding = entry->queried_max_outstanding;
    }

    entry->sw_num_rsc = 1<<ceil_log2((1<<ceil_log2(entry->max_outstanding)) + PRM_RSC_DELTA);
    MTL_DEBUG1(MT_FLFMT("%s: fw=%d, delta=%d, used=%d"), __func__,
                        entry->queried_max_outstanding, PRM_RSC_DELTA, entry->sw_num_rsc);

    if ( re_alloc_resources(entry, entry->in_at_ddr, entry->out_at_ddr) != HH_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: re_alloc_resources failed"), __func__);
      MT_RETURN(THH_CMD_STAT_EAGAIN);
    }

    for ( i=0; i<entry->max_outstanding; ++i ) {
      MOSAL_sem_rel(&entry->fw_outs_sem);
    }
    /* the following line is not an error but just to make sure the line is printed */
    MTL_DEBUG1(MT_FLFMT("%s: queried_max_outstanding=%d, max_outstanding=%d"), __func__,
               entry->queried_max_outstanding, entry->max_outstanding);
    memcpy(&entry->fw_props,fw_props,sizeof(THH_fw_props_t));
    entry->tokens_shift = log2(entry->sw_num_rsc);
    entry->tokens_idx_mask = (1<<entry->tokens_shift)-1;
    entry->query_fw_done = TRUE;
  }
  MT_RETURN(THH_CMD_STAT_OK);
}





/*
 *  THH_cmd_set_eq
 */
HH_ret_t THH_cmd_set_eq(THH_cmd_t cmd_if)
{
  struct cmd_if_context_st *entry = (struct cmd_if_context_st *)cmd_if;
  int i;
  THH_eqn_t old_eqn;
  unsigned long ctr;

  FUNC_IN;
  old_eqn = eqn_set(entry, 0);
  if ( old_eqn != THH_INVALID_EQN ) {
    /* eqn already set. clr_eq before setting a new one */
    MT_RETURN(HH_OK);
  }

  /* make sure there are no commands that passed the if in main_cmd_flow
     before we changed state tp prevent deadlock when we acquire the semaphore */
  do {
    MOSAL_spinlock_lock(&entry->ctr_spl);
    ctr = entry->poll_in_pipe;
    MOSAL_spinlock_unlock(&entry->ctr_spl);
    if ( ctr == 0 ) break;
    MOSAL_delay_execution(20000);
     
  } while ( 1 );

  MOSAL_sem_acq_ui(&entry->no_events_sem);

  for ( i=0; i<(int)entry->sw_num_rsc; ++i ) {
    MTL_DEBUG2(MT_FLFMT("increase sem level"));
    MOSAL_sem_rel(&entry->use_events_sem);
  }

  MT_RETURN(HH_OK);
}


/*
 *  THH_cmd_clr_eq
 */
HH_ret_t THH_cmd_clr_eq(THH_cmd_t cmd_if)
{
  struct cmd_if_context_st *entry = (struct cmd_if_context_st *)cmd_if;
  int i;
  THH_eqn_t old_eqn;
  unsigned long ctr;

  FUNC_IN;
  MTL_DEBUG1(MT_FLFMT("%s: called"), __func__);

  old_eqn = eqn_set(entry, THH_INVALID_EQN);
  if ( old_eqn == THH_INVALID_EQN ) {
    MTL_DEBUG1(MT_FLFMT("%s: returning"), __func__);
    MT_RETURN(HH_OK); /* already cleared */
  }

  /* make sure there are no commands that passed the if in main_cmd_flow
     before we changed state tp prevent deadlock when we acquire the semaphore */
  do {
    MOSAL_spinlock_lock(&entry->ctr_spl);
    ctr = entry->events_in_pipe;
    MOSAL_spinlock_unlock(&entry->ctr_spl);
    if ( ctr == 0 ) break;
    MOSAL_delay_execution(20000);
     
  } while ( 1 );

  /* acquire semaphore to the full depth to avoid to others to acquire it */
  for ( i=0; i<(int)entry->sw_num_rsc; ++i ) {
    MOSAL_sem_acq_ui(&entry->use_events_sem);
    MTL_DEBUG1(MT_FLFMT("%s: acquired %d"), __func__, i);
  }

  MOSAL_sem_rel(&entry->no_events_sem);
  MTL_DEBUG1(MT_FLFMT("%s: returning"), __func__);
  MT_RETURN(HH_OK);
}



/*
 *  THH_cmd_eventh
 */
void THH_cmd_eventh(THH_cmd_t cmd_if, u_int32_t *result_hcr_image_p)
{
  struct cmd_if_context_st *entry = (struct cmd_if_context_st *)cmd_if;
  priv_hcr_t hcr;
  cmd_ctx_t *ctx_p;

  FUNC_IN;
  MOSAL_sem_rel(&entry->fw_outs_sem);
  parse_new_HCR(result_hcr_image_p, &hcr);

#ifdef THH_CMD_TIME_TRACK
  MTL_ERROR1("CMD_TIME:END: cmd=UNKNOWN token=0x%X time=["U64_FMT"]\n",
             hcr.token, THH_eventp_last_cmdif_interrupt);
#endif          

  MOSAL_spinlock_irq_lock(&entry->ctx_obj.spl);
  if ( (get_ctx_by_idx(entry, hcr.token&entry->tokens_idx_mask, &ctx_p)!=HH_OK) ||
       (hcr.token!=ctx_p->token)
     ) {
    MOSAL_spinlock_unlock(&entry->ctx_obj.spl);
    MTL_ERROR1(MT_FLFMT("%s: could not find context by token. token=0x%04x"), __func__, hcr.token);
    THH_hob_fatal_error(entry->hob, THH_FATAL_TOKEN, VAPI_EV_SYNDROME_NONE);
    MT_RETV;
  }
  ctx_p->hcr = hcr;

  MOSAL_syncobj_signal(&ctx_p->syncobj);
  MOSAL_spinlock_unlock(&entry->ctx_obj.spl);

  MT_RETV;
}


/*
 *  THH_cmd_asign_ddrmm
 */
HH_ret_t THH_cmd_assign_ddrmm(THH_cmd_t cmd_if, THH_ddrmm_t ddrmm)
{
  struct cmd_if_context_st *entry = (struct cmd_if_context_st *)cmd_if;
  MT_bool in_at_ddr, out_at_ddr;

  FUNC_IN;

  MOSAL_mutex_acq_ui(&entry->sys_en_mtx);
  if ( entry->ddrmm != THH_DDRMM_INVALID_HANDLE ) {
    /* ddrmm already assigned */
    MOSAL_mutex_rel(&entry->sys_en_mtx);
    MT_RETURN(HH_EBUSY);
  }
  /* ddrmm was not assigned - assign it */
  entry->ddrmm = ddrmm;
  if ( entry->sys_enabled ) {
#ifdef EQS_CMD_IN_DDR
    out_at_ddr = TRUE;
#else
    out_at_ddr = FALSE;
#endif
#ifdef IN_PRMS_AT_DDR
    in_at_ddr = TRUE;
#else
    in_at_ddr = FALSE;
#endif

    if ( (in_at_ddr!=entry->in_at_ddr) || (out_at_ddr!=entry->out_at_ddr) ) {
      if ( re_alloc_resources(entry, in_at_ddr, out_at_ddr) != HH_OK  ) {
        MOSAL_mutex_rel(&entry->sys_en_mtx);
        MT_RETURN(HH_EAGAIN);
      }
      else {
        entry->in_at_ddr = in_at_ddr;
        entry->out_at_ddr = out_at_ddr;
      }
    }

  }

  MOSAL_mutex_rel(&entry->sys_en_mtx);

  MT_RETURN(HH_OK);
}


/*
 *  THH_cmd_revoke_ddrmm
 */
HH_ret_t THH_cmd_revoke_ddrmm(THH_cmd_t cmd_if)
{
  struct cmd_if_context_st *entry = (struct cmd_if_context_st *)cmd_if;
  HH_ret_t rc;

  FUNC_IN;
  MTL_DEBUG1(MT_FLFMT("%s called"), __func__);
  MOSAL_sem_acq_ui(&entry->no_events_sem);
  entry->in_at_ddr = FALSE;
  entry->out_at_ddr = FALSE;
  rc = re_alloc_resources(entry, entry->in_at_ddr, entry->out_at_ddr);
  if ( rc != HH_OK ) {
    MOSAL_sem_rel(&entry->no_events_sem);
    MTL_ERROR1(MT_FLFMT("%s: re_alloc_resources failed - %s"), __func__, HH_strerror(rc));
    MT_RETURN(rc);
  }
  entry->ddrmm = THH_DDRMM_INVALID_HANDLE;

  MOSAL_sem_rel(&entry->no_events_sem);
  MT_RETURN(HH_OK);
}



/*
 *  THH_cmd_SYS_EN
 */
THH_cmd_status_t THH_cmd_SYS_EN(IN THH_cmd_t cmd_if)
{
  command_fields_t cmd_prms = {0};
  THH_cmd_status_t rc;

  FUNC_IN;
  cmd_prms.opcode = TAVOR_IF_CMD_SYS_EN;
  cmd_prms.in_trans = TRANS_NA;
  cmd_prms.out_trans = TRANS_NA;
  rc = cmd_invoke(cmd_if, &cmd_prms);
  if ( rc != THH_CMD_STAT_OK ) {
    MTL_ERROR1("%s\n", str_THH_cmd_status_t(rc));
  }
  MT_RETURN(rc);
}



/*
 *  str_THH_cmd_status_t
 */
const char *str_THH_cmd_status_t(THH_cmd_status_t status)
{
  switch ( status ) {
    case THH_CMD_STAT_OK:
      return THH_CMD_STAT_OK_STR;

    case THH_CMD_STAT_INTERNAL_ERR:
      return THH_CMD_STAT_INTERNAL_ERR_STR;

    case THH_CMD_STAT_BAD_OP:
      return THH_CMD_STAT_BAD_OP_STR;

    case THH_CMD_STAT_BAD_PARAM:
      return THH_CMD_STAT_BAD_PARAM_STR;

    case THH_CMD_STAT_BAD_SYS_STATE:
      return THH_CMD_STAT_BAD_SYS_STATE_STR;

    case THH_CMD_STAT_BAD_RESOURCE:
      return THH_CMD_STAT_BAD_RESOURCE_STR;

    case THH_CMD_STAT_RESOURCE_BUSY:
      return THH_CMD_STAT_RESOURCE_BUSY_STR;

    case THH_CMD_STAT_DDR_MEM_ERR:
      return THH_CMD_STAT_DDR_MEM_ERR_STR;

    case THH_CMD_STAT_EXCEED_LIM:
      return THH_CMD_STAT_EXCEED_LIM_STR;

    case THH_CMD_STAT_BAD_RES_STATE:
      return THH_CMD_STAT_BAD_RES_STATE_STR;

    case THH_CMD_STAT_BAD_INDEX:
      return THH_CMD_STAT_BAD_INDEX_STR;

    case THH_CMD_STAT_BAD_QPEE_STATE:
      return THH_CMD_STAT_BAD_QPEE_STATE_STR;

    case THH_CMD_STAT_BAD_SEG_PARAM:
      return THH_CMD_STAT_BAD_SEG_PARAM_STR;

    case THH_CMD_STAT_REG_BOUND:
      return THH_CMD_STAT_REG_BOUND_STR;

    case THH_CMD_STAT_BAD_PKT:
      return THH_CMD_STAT_BAD_PKT_STR;

    case THH_CMD_STAT_EAGAIN:
      return THH_CMD_STAT_EAGAIN_STR;

    case THH_CMD_STAT_EABORT:
      return THH_CMD_STAT_EABORT_STR;

    case THH_CMD_STAT_ETIMEOUT:
      return THH_CMD_STAT_ETIMEOUT_STR;

    case THH_CMD_STAT_EFATAL:
      return THH_CMD_STAT_EFATAL_STR;

    case THH_CMD_STAT_EBADARG:
      return THH_CMD_STAT_EBADARG_STR;

    case THH_CMD_STAT_EINTR:
      return THH_CMD_STAT_EINTR_STR;

    case THH_CMD_STAT_BAD_SIZE:
      return THH_CMD_STAT_BAD_SIZE_STR;

    default:
      return "unrecognized status";
  }
}


const char *cmd_str(tavor_if_cmd_t opcode)
{
  switch ( opcode ) {
    case TAVOR_IF_CMD_SYS_EN:
      return "TAVOR_IF_CMD_SYS_EN";
    case TAVOR_IF_CMD_SYS_DIS:
      return "TAVOR_IF_CMD_SYS_DIS";
    case TAVOR_IF_CMD_QUERY_DEV_LIM:
      return "TAVOR_IF_CMD_QUERY_DEV_LIM";
    case TAVOR_IF_CMD_QUERY_FW:
      return "TAVOR_IF_CMD_QUERY_FW";
    case TAVOR_IF_CMD_QUERY_DDR:
      return "TAVOR_IF_CMD_QUERY_DDR";
    case TAVOR_IF_CMD_QUERY_ADAPTER:
      return "TAVOR_IF_CMD_QUERY_ADAPTER";
    case TAVOR_IF_CMD_INIT_HCA:
      return "TAVOR_IF_CMD_INIT_HCA";
    case TAVOR_IF_CMD_CLOSE_HCA:
      return "TAVOR_IF_CMD_CLOSE_HCA";
    case TAVOR_IF_CMD_INIT_IB:
      return "TAVOR_IF_CMD_INIT_IB";
    case TAVOR_IF_CMD_CLOSE_IB:
      return "TAVOR_IF_CMD_CLOSE_IB";
    case TAVOR_IF_CMD_QUERY_HCA:
      return "TAVOR_IF_CMD_QUERY_HCA";
    case TAVOR_IF_CMD_SET_IB:
      return "TAVOR_IF_CMD_SET_IB";
    case TAVOR_IF_CMD_SW2HW_MPT:
      return "TAVOR_IF_CMD_SW2HW_MPT";
    case TAVOR_IF_CMD_QUERY_MPT:
      return "TAVOR_IF_CMD_QUERY_MPT";
    case TAVOR_IF_CMD_HW2SW_MPT:
      return "TAVOR_IF_CMD_HW2SW_MPT";
    case TAVOR_IF_CMD_READ_MTT:
      return "TAVOR_IF_CMD_READ_MTT";
    case TAVOR_IF_CMD_WRITE_MTT:
      return "TAVOR_IF_CMD_WRITE_MTT";
    case TAVOR_IF_CMD_MAP_EQ:
      return "TAVOR_IF_CMD_MAP_EQ";
    case TAVOR_IF_CMD_SW2HW_EQ:
      return "TAVOR_IF_CMD_SW2HW_EQ";
    case TAVOR_IF_CMD_HW2SW_EQ:
      return "TAVOR_IF_CMD_HW2SW_EQ";
    case TAVOR_IF_CMD_QUERY_EQ:
      return "TAVOR_IF_CMD_QUERY_EQ";
    case TAVOR_IF_CMD_SW2HW_CQ:
      return "TAVOR_IF_CMD_SW2HW_CQ";
    case TAVOR_IF_CMD_HW2SW_CQ:
      return "TAVOR_IF_CMD_HW2SW_CQ";
    case TAVOR_IF_CMD_QUERY_CQ:
      return "TAVOR_IF_CMD_QUERY_CQ";
    case TAVOR_IF_CMD_RST2INIT_QPEE:
      return "TAVOR_IF_CMD_RST2INIT_QPEE";
    case TAVOR_IF_CMD_INIT2RTR_QPEE:
      return "TAVOR_IF_CMD_INIT2RTR_QPEE";
    case TAVOR_IF_CMD_RTR2RTS_QPEE:
      return "TAVOR_IF_CMD_RTR2RTS_QPEE";
    case TAVOR_IF_CMD_RTS2RTS_QPEE:
      return "TAVOR_IF_CMD_RTS2RTS_QPEE";
    case TAVOR_IF_CMD_SQERR2RTS_QPEE:
      return "TAVOR_IF_CMD_SQERR2RTS_QPEE";
    case TAVOR_IF_CMD_2ERR_QPEE:
      return "TAVOR_IF_CMD_2ERR_QPEE";
    case TAVOR_IF_CMD_RTS2SQD_QPEE:
      return "TAVOR_IF_CMD_RTS2SQD_QPEE";
    case TAVOR_IF_CMD_SQD2RTS_QPEE:
      return "TAVOR_IF_CMD_SQD2RTS_QPEE";
    case TAVOR_IF_CMD_ERR2RST_QPEE:
      return "TAVOR_IF_CMD_ERR2RST_QPEE";
    case TAVOR_IF_CMD_QUERY_QPEE:
      return "TAVOR_IF_CMD_QUERY_QPEE";
    case TAVOR_IF_CMD_CONF_SPECIAL_QP:
      return "TAVOR_IF_CMD_CONF_SPECIAL_QP";
    case TAVOR_IF_CMD_MAD_IFC:
      return "TAVOR_IF_CMD_MAD_IFC";
    case TAVOR_IF_CMD_READ_MGM:
      return "TAVOR_IF_CMD_READ_MGM";
    case TAVOR_IF_CMD_WRITE_MGM:
      return "TAVOR_IF_CMD_WRITE_MGM";
    case TAVOR_IF_CMD_MGID_HASH:
      return "TAVOR_IF_CMD_MGID_HASH";
    case TAVOR_IF_CMD_CONF_NTU:
      return "TAVOR_IF_CMD_CONF_NTU";
    case TAVOR_IF_CMD_QUERY_NTU:
      return "TAVOR_IF_CMD_QUERY_NTU";
    case TAVOR_IF_CMD_RESIZE_CQ:
      return "TAVOR_IF_CMD_RESIZE_CQ";
    case TAVOR_IF_CMD_SUSPEND_QPEE:
      return "TAVOR_IF_CMD_SUSPEND_QPEE";
    case TAVOR_IF_CMD_UNSUSPEND_QPEE:
      return "TAVOR_IF_CMD_UNSUSPEND_QPEE";
    case TAVOR_IF_CMD_SW2HW_SRQ:
      return "TAVOR_IF_CMD_SW2HW_SRQ";
    case TAVOR_IF_CMD_HW2SW_SRQ:
      return "TAVOR_IF_CMD_HW2SW_SRQ";
    case TAVOR_IF_CMD_QUERY_SRQ:
      return "TAVOR_IF_CMD_QUERY_SRQ";
    default:
      return "[UNKNOWN_COMMAND]";
  }
}


/*================ static functions definitions ====================================*/


/*
 *  cmd_invoke
 */
THH_cmd_status_t cmd_invoke(THH_cmd_t cmd_if, command_fields_t *cmd_prms)
{
  struct cmd_if_context_st *entry = (struct cmd_if_context_st *)cmd_if;
  THH_cmd_status_t rc;

  FUNC_IN;

  if ( entry->have_fatal && ((cmd_prms->input_modifier==0) || (cmd_prms->opcode!=TAVOR_IF_CMD_CLOSE_HCA)) ) {
    MT_RETURN(THH_CMD_STAT_EFATAL);
  }

  if (entry->sys_enabled == FALSE ) {
    /* sys not enabled - we only allow sys enable cmd */
    if ( cmd_prms->opcode != TAVOR_IF_CMD_SYS_EN ) {
      MT_RETURN(THH_CMD_STAT_EAGAIN);
    }
    else {
      rc = sys_en_hca(entry);
      MT_RETURN(rc);
    }
  }
  else {
    /* system enabled */
    if ( cmd_prms->opcode == TAVOR_IF_CMD_SYS_EN ) {
      /* we don't allow re-invoking sys enable */
      MT_RETURN(THH_CMD_STAT_BAD_SYS_STATE);
    }
    else {
      rc = main_cmd_flow(entry, cmd_prms);
      if ( rc != THH_CMD_STAT_OK ) {
        MTL_ERROR1(MT_FLFMT("Failed command 0x%X (%s): status=0x%X (%s)\n"), 
                   cmd_prms->opcode, cmd_str(cmd_prms->opcode),rc,str_THH_cmd_status_t(rc));
      }
      MT_RETURN(rc);
    }
  }
}


/*
 *  sys_en_hca
 */
static THH_cmd_status_t sys_en_hca(struct cmd_if_context_st *entry)
{
  u_int32_t token = 0, i, dw6;
  THH_cmd_status_t rc;

  FUNC_IN;
  MOSAL_mutex_acq_ui(&entry->sys_en_mtx);
  if ( entry->sys_enabled ) {
    MOSAL_mutex_rel(&entry->sys_en_mtx);
    MT_RETURN(THH_CMD_STAT_OK); /* already enabled */
  }
  if ( !cmdif_is_free(entry) ) {
    MOSAL_mutex_rel(&entry->sys_en_mtx);
    /* no need to call the hob at this early stage - just return THH_CMD_STAT_EFATAL */
    MT_RETURN(THH_CMD_STAT_EFATAL);
  }

  PREP_TOKEN_DW(token, DEFAULT_TOKEN);

  MOSAL_MMAP_IO_WRITE_DWORD((u_int8_t *)(entry->hcr_virt_base)+HCR_DW_BYTE_OFFSET(token), token);

  write_command_dw(&dw6, 1, 0, 0, TAVOR_IF_CMD_SYS_EN);
  MOSAL_MMAP_IO_WRITE_DWORD(((u_int32_t *)(entry->hcr_virt_base))+6, dw6);
  for ( i=0; i<(TAVOR_IF_CMD_ETIME_SYS_EN/10000); ++i ) {
    MOSAL_delay_execution(10000);
    if ( cmdif_is_free(entry) ) {
      MTL_TRACE1("command executed in %d mili seconds\n", i*10);
      break;
    }
  }
  if ( !cmdif_is_free(entry) ) {
    MOSAL_mutex_rel(&entry->sys_en_mtx);
    /* no need to call the hob at this early stage - just return THH_CMD_STAT_EFATAL */
    MT_RETURN(THH_CMD_STAT_ETIMEOUT);
  }
  rc = cmd_if_status(entry);
  if ( rc == THH_CMD_STAT_OK ) {
    entry->sys_enabled = TRUE;
  }
  MOSAL_mutex_rel(&entry->sys_en_mtx);
  MT_RETURN(rc);
}


/*
 *  main_cmd_flow
 */
static THH_cmd_status_t main_cmd_flow(struct cmd_if_context_st *entry, command_fields_t *cmd_prms)
{
  THH_cmd_status_t rc;

  FUNC_IN;
  MOSAL_spinlock_lock(&entry->ctr_spl);
  if ( entry->eqn == THH_INVALID_EQN ) {
    /* events not enabled */
    entry->poll_in_pipe++;
    MOSAL_spinlock_unlock(&entry->ctr_spl);
    rc = cmd_flow_no_events(entry, cmd_prms);
    MOSAL_spinlock_lock(&entry->ctr_spl);
    entry->poll_in_pipe--;
    MOSAL_spinlock_unlock(&entry->ctr_spl);
    MT_RETURN(rc);
  }
  else {
    /* events are enabled */
    entry->events_in_pipe++;
    MOSAL_spinlock_unlock(&entry->ctr_spl);
    rc = cmd_flow_events(entry, cmd_prms);
    MOSAL_spinlock_lock(&entry->ctr_spl);
    entry->events_in_pipe--;
    MOSAL_spinlock_unlock(&entry->ctr_spl);
    MT_RETURN(rc);
  }
}


/*
 *  cmdif_is_free
 */
static MT_bool cmdif_is_free(struct cmd_if_context_st *entry)
{
  u_int32_t val;
  volatile u_int32_t *offset = (volatile u_int32_t *)entry->hcr_virt_base, *ptr;
  MT_bool is_free;

  ptr = &offset[HCR_DW_BYTE_OFFSET(go)>>2];
  val = MOSAL_be32_to_cpu(MOSAL_MMAP_IO_READ_DWORD(ptr));
  is_free = !MT_EXTRACT32(val, HCR_DW_BIT_OFFSET(go), HCR_BIT_SIZE(go));
  return is_free;
}


/*
 *  cleanup_cmdobj
 */
static void cleanup_cmdobj(struct cmd_if_context_st *entry)
{
  FUNC_IN;
  if ( entry->track_arr ) FREE(entry->track_arr);
  de_alloc_cmd_contexts(entry);
  if ( entry->hcr_virt_base ) MOSAL_io_unmap((MT_virt_addr_t)(entry->hcr_virt_base));
  if ( entry->uar0_virt_base) MOSAL_io_unmap((MT_virt_addr_t)(entry->uar0_virt_base));
  FREE(entry);
  MT_RETV;
}





/*
 *  cmd_if_status
 */
static inline THH_cmd_status_t cmd_if_status(struct cmd_if_context_st *entry)
{
  u_int32_t *offset = (u_int32_t *)entry->hcr_virt_base;
  u_int32_t cmd;

  cmd = offset[HCR_DW_BYTE_OFFSET(status)>>2];
  cmd = MOSAL_be32_to_cpu(cmd);
  return (THH_cmd_status_t)MT_EXTRACT32(cmd, HCR_DW_BIT_OFFSET(status), HCR_BIT_SIZE(status));
}


/*
 *  set_mailbox
 */
static inline void set_mailbox(u_int32_t *dst_buf, MT_phys_addr_t mbx_pa)
{
  addr_64bit_t addr;

  ptr_to_mailbox_ptr(mbx_pa, &addr);
  dst_buf[0] = addr.addr_h;
  dst_buf[1] = addr.addr_l;
}


/*
 *  cvt_be32_to_cpu
 *  size in bytes
 */
static inline void cvt_be32_to_cpu(void *buf, u_int32_t size)
{
  u_int32_t i, *p=(u_int32_t *)(buf);
  for ( i=0; i<(size>>2); ++i ) {
    *p = MOSAL_be32_to_cpu(*p);
    p++;
  }
}


/*
 *  cvt_cpu_to_be32
 *  size in bytes
 */
static inline void cvt_cpu_to_be32(void *buf, u_int32_t size)
{
  u_int32_t i, *p=(u_int32_t *)(buf);
  for ( i=0; i<(size>>2); ++i ) {
    *p = MOSAL_cpu_to_be32(*p);
    p++;
  }
}


/*
 *  d32_to_s64
 */
static inline u_int64_t d32_to_s64(u_int32_t hi, u_int32_t lo)
{
  return(((u_int64_t)hi) << 32) | (u_int64_t)(lo);
}


/*
 *
 */
#if 0
static inline void prep_token_dw(u_int32_t *token_dw_p, u_int16_t val)
{
  MT_INSERT32((*token_dw_p),(val),HCR_DW_BIT_OFFSET(token),MT_BIT_SIZE(tavorprm_hca_command_register_st,token));
  *token_dw_p = MOSAL_cpu_to_be32(*token_dw_p);
}
#endif

/*
 *  cmd_flow_events
 */
static THH_cmd_status_t cmd_flow_events(struct cmd_if_context_st *entry, command_fields_t *cmd_prms)
{
  THH_cmd_status_t ret=THH_CMD_STAT_EFATAL;
  int i;
#ifdef MAX_DEBUG
  u_int64_t command_time;  /* For sampling time command started */
#endif
  u_int64_t start_time, busy_end_time, go_end_time;
  call_result_t rc;
  HH_ret_t rc1;
  cmd_ctx_t *ctx_p;

  FUNC_IN;


  MOSAL_sem_acq_ui(&entry->use_events_sem);
  if ( entry->have_fatal ) {
    ret = THH_CMD_STAT_EFATAL;
    goto ex_ues_rel;
  }

  MOSAL_sem_acq_ui(&entry->fw_outs_sem); /* released in THH_cmd_eventh */
  if ( entry->have_fatal ) {
    ret = THH_CMD_STAT_EFATAL;
    goto ex_fos_rel;
  }


  MOSAL_spinlock_irq_lock(&entry->ctx_obj.spl);
  rc1 = acq_cmd_ctx(entry, &ctx_p);
  MOSAL_spinlock_unlock(&entry->ctx_obj.spl);
  if ( rc1 != HH_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: failed to acquire context. this is fatal !!!"), __func__);
    THH_hob_fatal_error(entry->hob, THH_FATAL_NONE, VAPI_EV_SYNDROME_NONE);
    print_outs_commands(&entry->ctx_obj);
    print_track_arr(entry);
    ret = THH_CMD_STAT_EFATAL;
    goto ex_fos_rel;
  }
  MTL_DEBUG8(MT_FLFMT("token=0x%04x"), ctx_p->token);
  edit_hcr(entry, cmd_prms, ctx_p->token, ctx_p, 1);

  /* execute the command */
#ifdef MAX_DEBUG
  command_time= MOSAL_get_time_counter();
#endif

  MOSAL_syncobj_clear(&ctx_p->syncobj);

  MOSAL_mutex_acq_ui(&entry->hcr_mtx);
  if ( entry->have_fatal ) {
    ret = THH_CMD_STAT_EFATAL;
    goto ex_hm_rel;

  }
  if ( !entry->post_to_uar0 ) {
    /* check that the go bit is 0 */
    start_time = MOSAL_get_time_counter();
    busy_end_time = start_time + entry->counts_busy_wait_for_go;
    go_end_time = start_time + entry->counts_sleep_wait_for_go;
    while ( !cmdif_is_free(entry) ) {
      if ( MOSAL_get_time_counter() > busy_end_time ) {
        /* expired busy wait loop */
        if ( MOSAL_get_time_counter() > go_end_time ) {
          /* fatal condition detected */

          THH_hob_fatal_error(entry->hob, THH_FATAL_GOBIT, VAPI_EV_SYNDROME_NONE);
          MTL_ERROR1(MT_FLFMT("%s: go bit was not cleared for %d usec"), __func__, MAX_UC_SLEEP_FOR_GO);
          print_outs_commands(&entry->ctx_obj);
          print_track_arr(entry);
          ret = THH_CMD_STAT_EFATAL;
          goto ex_hm_rel;
        }

        /* we go to sleep for 1 os tick */
        MOSAL_usleep_ui(1000);

        /* in case of fatal state terminate the loop immediately */
        if ( entry->have_fatal ) {
          ret = THH_CMD_STAT_EFATAL;
          goto ex_hm_rel;
        }

        /* calculate short busy waits */
        busy_end_time = MOSAL_get_time_counter() + entry->short_wait_for_go;
      }
    }
  }


#ifdef THH_CMD_TIME_TRACK
  MTL_ERROR1("CMD_TIME:START: cmd=%s token=0x%X time=["U64_FMT"]\n",
             cmd_str(cmd_prms->opcode), ctx_p->token, MOSAL_get_time_counter());
#endif
  /* execute the command */
  if ( entry->post_to_uar0 ) {
    for ( i=0; i<4; ++i ) {
      MOSAL_MMAP_IO_WRITE_DWORD(((u_int32_t *)(entry->uar0_virt_base))+i, ctx_p->hcr_buf[i]);
    }
    for ( i=4; i<8; ++i ) {
      MOSAL_MMAP_IO_WRITE_DWORD(((u_int32_t *)(entry->uar0_virt_base))+i-4, ctx_p->hcr_buf[i]);
    }
  }
  else {
    for ( i=0; i<7; ++i ) {
      MOSAL_MMAP_IO_WRITE_DWORD(((u_int32_t *)(entry->hcr_virt_base))+i, ctx_p->hcr_buf[i]);
    }
  }

  track_exec_cmds(entry, ctx_p);
  MOSAL_mutex_rel(&entry->hcr_mtx);

  MTL_TRACE7(MT_FLFMT("using timeout %d usec (0 signifies inifinite !!!)"), entry->inf_timeout ? MOSAL_SYNC_TIMEOUT_INFINITE : cmd_prms->exec_time_micro);
  rc = MOSAL_syncobj_waiton_ui(&ctx_p->syncobj, entry->inf_timeout ? MOSAL_SYNC_TIMEOUT_INFINITE : cmd_prms->exec_time_micro);
  if ( entry->have_fatal ) {
    ret = THH_CMD_STAT_EFATAL;
    goto ex_ctx_rel;
  }
  switch ( rc ) {
    case MT_OK:

#ifdef MAX_DEBUG
      {   
        unsigned long counts_per_usec = ((unsigned long)MOSAL_get_counts_per_sec())/1000000;
        if (counts_per_usec == 0)  counts_per_usec= 1;
        command_time= MOSAL_get_time_counter() - command_time;
        MTL_DEBUG4(MT_FLFMT("Command completed after approx. "U64_FMT" CPU clocks (~ %lu [usec])"),
                   command_time,((unsigned long)command_time)/counts_per_usec); 
      }
#endif      
      /* woke by event */
      break;

    case MT_ETIMEDOUT:
      break;

    default:
      MTL_ERROR1(MT_FLFMT("%s: unexpeted return code from MOSAL_syncobj_waiton: %s(%d)"),
                 __func__, mtl_strerror_sym(rc), rc);
  } /* end of switch (rc) */


  if ( rc == MT_OK  ) {
    ret = ctx_p->hcr.status;
    extract_hcr(cmd_prms, &ctx_p->hcr);
    MTL_DEBUG8(MT_FLFMT("successful completion for token=0x%04x\n"), ctx_p->token);
  }
  else {
    MTL_ERROR1(MT_FLFMT("Command not completed after timeout: cmd=%s (0x%x), token=0x%04x, pid="MT_ULONG_PTR_FMT", go=%d"),
               cmd_str(cmd_prms->opcode),cmd_prms->opcode, ctx_p->token, MOSAL_getpid(), cmdif_is_free(entry)==TRUE ? 0 : 1);
    MOSAL_sem_rel(&entry->fw_outs_sem);
    dump_cmd_err_info(ctx_p, cmd_prms);
    THH_hob_fatal_error(entry->hob, THH_FATAL_CMD_TIMEOUT, VAPI_EV_SYNDROME_NONE);
    ret = THH_CMD_STAT_EFATAL;
    print_outs_commands(&entry->ctx_obj);
    print_track_arr(entry);
    goto ex_ctx_rel;
  }



  MOSAL_spinlock_irq_lock(&entry->ctx_obj.spl);
  rel_cmd_ctx(entry, ctx_p);
  MOSAL_spinlock_unlock(&entry->ctx_obj.spl);
  MOSAL_sem_rel(&entry->use_events_sem);

  /* check if we're in a fatal state */
  if ( entry->have_fatal ) { ret = THH_CMD_STAT_EFATAL; }
  goto ex_no_clean; /* normal function exit */


ex_hm_rel:
  MOSAL_mutex_rel(&entry->hcr_mtx);
ex_ctx_rel:
  MOSAL_spinlock_irq_lock(&entry->ctx_obj.spl);
  rel_cmd_ctx(entry, ctx_p);
  MOSAL_spinlock_unlock(&entry->ctx_obj.spl);
ex_fos_rel:
  MOSAL_sem_rel(&entry->fw_outs_sem);
ex_ues_rel:
  MOSAL_sem_rel(&entry->use_events_sem);
ex_no_clean:
  MT_RETURN(ret);
}


/*
 *  cmd_flow_no_events
 */
static THH_cmd_status_t cmd_flow_no_events(struct cmd_if_context_st *entry, command_fields_t *cmd_prms)
{
  priv_hcr_t hcr;
  THH_cmd_status_t rc;
  u_int32_t i;
  HH_ret_t rc1;
  cmd_ctx_t *ctx_p;

  FUNC_IN;

  if ( entry->have_fatal && ((cmd_prms->input_modifier==0) || (cmd_prms->opcode!=TAVOR_IF_CMD_CLOSE_HCA)) ) {
    MT_RETURN(THH_CMD_STAT_EFATAL);
  }

  MOSAL_sem_acq_ui(&entry->no_events_sem);
  /* make sure go bit is cleared */
  if ( !cmdif_is_free(entry) ) {
    MTL_ERROR1(MT_FLFMT("%s: go bit is set"), __func__);
    MOSAL_sem_rel(&entry->no_events_sem);
    THH_hob_fatal_error(entry->hob, THH_FATAL_GOBIT, VAPI_EV_SYNDROME_NONE);
    MT_RETURN(THH_CMD_STAT_EFATAL);
  }

  MOSAL_spinlock_irq_lock(&entry->ctx_obj.spl);
  rc1 = acq_cmd_ctx(entry, &ctx_p);
  MOSAL_spinlock_unlock(&entry->ctx_obj.spl);
  if ( rc1 != HH_OK ) {
    MOSAL_sem_rel(&entry->no_events_sem);
    MTL_ERROR1(MT_FLFMT("%s: acq_cmd_ctx failed"), __func__);
    MT_RETURN(THH_CMD_STAT_EFATAL); /* this is not EAGAIN since this thing must not happen */ 
  }

  edit_hcr(entry, cmd_prms, DEFAULT_TOKEN, ctx_p, 0);


  /* execute the command */
  for ( i=0; i<7; ++i ) {
    MOSAL_MMAP_IO_WRITE_DWORD(((u_int32_t *)(entry->hcr_virt_base))+i, ctx_p->hcr_buf[i]);
  }

  if ( !entry->inf_timeout ) {
    i = cmd_prms->exec_time_micro/10000;
  }
  else {
    i = 0xffffffff; /* not matematically infinite but practically it's a long time */
  }
  if ( (cmd_prms->exec_time_micro>=LOOP_DELAY_TRESHOLD) || (entry->inf_timeout==TRUE) ) {
    for ( i=0; i<(cmd_prms->exec_time_micro/10000); ++i ) {
      MOSAL_usleep_ui(10000);
      if ( cmdif_is_free(entry) ) {
        if ( i>=1 ) {
          MTL_TRACE1("command executed in %d mili seconds\n", i*10);
        }
        else {
          MTL_TRACE1("command executed in less than 10 mili seconds\n");
        }
        break;
      }
    }
  }
  else {
    MOSAL_delay_execution(cmd_prms->exec_time_micro);
  }


  if ( !cmdif_is_free(entry) ) {
    MTL_TRACE1("command failed after %d msec\n", i*10);
    MOSAL_spinlock_irq_lock(&entry->ctx_obj.spl);
    rel_cmd_ctx(entry, ctx_p);
    MOSAL_spinlock_unlock(&entry->ctx_obj.spl);
    MOSAL_sem_rel(&entry->no_events_sem);
    dump_cmd_err_info(ctx_p, cmd_prms);
    THH_hob_fatal_error(entry->hob, THH_FATAL_GOBIT, VAPI_EV_SYNDROME_NONE);
    MT_RETURN(THH_CMD_STAT_ETIMEOUT);
  }

  parse_HCR((u_int32_t*)entry->hcr_virt_base, &hcr);
  extract_hcr(cmd_prms, &hcr);
  MOSAL_spinlock_irq_lock(&entry->ctx_obj.spl);
  rel_cmd_ctx(entry, ctx_p);
  MOSAL_spinlock_unlock(&entry->ctx_obj.spl);


  rc = cmd_if_status(entry);
  MOSAL_sem_rel(&entry->no_events_sem);
  MTL_DEBUG2("status=0x%08x\n", rc);
  MT_RETURN(rc);
}

/*
 *  memcpy_to_tavor
 */
static void *memcpy_to_tavor(void *dst, const void *src, MT_size_t size)
{
  u_int32_t *dst32 = (u_int32_t *)dst;
  u_int32_t *src32 = (u_int32_t *)src;
  MT_size_t i;

  for ( i=0; i<(size>>2); ++i ) {
    dst32[i] = MOSAL_cpu_to_be32(src32[i]);
  }
  return dst;
}


/*
 *  memcpy_from_tavor
 */
static void *memcpy_from_tavor(void *dst, const void *src, MT_size_t size)
{
  u_int32_t *dst32 = (u_int32_t *)dst;
  u_int32_t *src32 = (u_int32_t *)src;
  MT_size_t i;

  for ( i=0; i<(size>>2); ++i ) {
    dst32[i] = MOSAL_be32_to_cpu(src32[i]);
  }
  return dst;
}

/*====== parse input output mailbox structs ===============*/
#if 0
static void parse_QUERY_FW(void *buf, THH_fw_props_t *fw_props_p)
{
  cvt_be32_to_cpu(buf, sizeof(struct tavorprm_query_fw_st)>>5);
  fw_props_p->fw_rev_major = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_query_fw_st, fw_rev_major), MT_BIT_SIZE(tavorprm_query_fw_st, fw_rev_major));
  fw_props_p->fw_rev_minor = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_query_fw_st, fw_rev_minor), MT_BIT_SIZE(tavorprm_query_fw_st, fw_rev_minor));
  fw_props_p->cmd_interface_rev = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_query_fw_st, cmd_interface_rev), MT_BIT_SIZE(tavorprm_query_fw_st, cmd_interface_rev));
  fw_props_p->log_max_outstanding_cmd = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_query_fw_st, log_max_outstanding_cmd), MT_BIT_SIZE(tavorprm_query_fw_st, log_max_outstanding_cmd));
  fw_props_p->fw_base_addr = d32_to_s64(MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_query_fw_st, fw_base_addr_h), MT_BIT_SIZE(tavorprm_query_fw_st, fw_base_addr_h)),
                                        MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_query_fw_st, fw_base_addr_l), MT_BIT_SIZE(tavorprm_query_fw_st, fw_base_addr_l)));

  fw_props_p->fw_end_addr = d32_to_s64(MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_query_fw_st, fw_end_addr_h), MT_BIT_SIZE(tavorprm_query_fw_st, fw_end_addr_h)),
                                       MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_query_fw_st, fw_end_addr_l), MT_BIT_SIZE(tavorprm_query_fw_st, fw_end_addr_l)));

}
#endif


static void parse_HCR(u_int32_t *result_hcr_image_p, priv_hcr_t *hcr)
{
  u_int32_t buf[sizeof(struct tavorprm_hca_command_register_st) >> 5];

  MOSAL_MMAP_IO_READ_BUF_DWORD(result_hcr_image_p,buf,sizeof(buf)>>2);

  cvt_be32_to_cpu(buf, sizeof(buf));

  /* in param */
  hcr->in_param[0] = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hca_command_register_st, in_param_h),
                                     MT_BIT_SIZE(tavorprm_hca_command_register_st, in_param_h));
  hcr->in_param[1] = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hca_command_register_st, in_param_l),
                                     MT_BIT_SIZE(tavorprm_hca_command_register_st, in_param_l));

  /* input modifier */
  hcr->input_modifier = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hca_command_register_st, input_modifier),
                                        MT_BIT_SIZE(tavorprm_hca_command_register_st, input_modifier));

  /* out param */
  hcr->out_param[0] = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hca_command_register_st, out_param_h),
                                      MT_BIT_SIZE(tavorprm_hca_command_register_st, out_param_h));
  hcr->out_param[1] = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hca_command_register_st, out_param_l),
                                      MT_BIT_SIZE(tavorprm_hca_command_register_st, out_param_l));

  /* token */
  hcr->token = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hca_command_register_st, token),
                               MT_BIT_SIZE(tavorprm_hca_command_register_st, token));

  /* opcode */
  hcr->opcode = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hca_command_register_st, opcode),
                                MT_BIT_SIZE(tavorprm_hca_command_register_st, opcode));

  /* opcode modifier */
  hcr->opcode_modifier = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hca_command_register_st, opcode_modifier),
                                         MT_BIT_SIZE(tavorprm_hca_command_register_st, opcode_modifier));

  /* e bit */
  hcr->e = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hca_command_register_st, e),
                           MT_BIT_SIZE(tavorprm_hca_command_register_st, e));

  /* go bit */
  hcr->go = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hca_command_register_st, go),
                            MT_BIT_SIZE(tavorprm_hca_command_register_st, go));

  /* status */
  hcr->status = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hca_command_register_st, status),
                                MT_BIT_SIZE(tavorprm_hca_command_register_st, status));
}

#ifdef NEW_EQE_FORMAT
static void parse_new_HCR(u_int32_t *result_hcr_image_p, priv_hcr_t *hcr)
{
  u_int32_t buf[sizeof(struct tavorprm_hcr_completion_event_st) >> 5];

  /* we don't read the hardware so memcpy suffices */
  memcpy(buf, result_hcr_image_p, sizeof(buf));

  cvt_be32_to_cpu(buf, sizeof(buf));

  /* token */
  hcr->token = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hcr_completion_event_st, token),
                               MT_BIT_SIZE(tavorprm_hcr_completion_event_st, token));

  /* status */
  hcr->status = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hcr_completion_event_st, status),
                                MT_BIT_SIZE(tavorprm_hcr_completion_event_st, status));

  /* out param */
  hcr->out_param[0] = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hcr_completion_event_st, out_param_h),
                                      MT_BIT_SIZE(tavorprm_hcr_completion_event_st, out_param_h));
  hcr->out_param[1] = MT_EXTRACT_ARRAY32(buf, MT_BIT_OFFSET(tavorprm_hcr_completion_event_st, out_param_l),
                                      MT_BIT_SIZE(tavorprm_hcr_completion_event_st, out_param_l));
}
#endif


/*
 *  edit_hcr
 */
static void edit_hcr(struct cmd_if_context_st *entry, command_fields_t *cmd_prms, u_int16_t token, cmd_ctx_t *ctx_p, int event)
{
  u_int32_t _token = 0;

  switch ( cmd_prms->in_trans ) {
    case TRANS_NA:
      /* note! since these are zeroes I do not bother to deal with endianess */
      ctx_p->hcr_buf[0] = 0;
      ctx_p->hcr_buf[1] = 0;
      break;
    case TRANS_IMMEDIATE:
      {
        u_int32_t *caller_prms = (u_int32_t *)cmd_prms->in_param;
        ctx_p->hcr_buf[0] = MOSAL_cpu_to_be32(caller_prms[0]);
        ctx_p->hcr_buf[1] = MOSAL_cpu_to_be32(caller_prms[1]);
      }
      break;
    case TRANS_MAILBOX:
      /* convert the data pointed by in_prms from cpu to be */
      cmd_prms->in_param_va = (u_int8_t *)(ctx_p->in_prm.prm_base_va);
      memcpy_to_tavor((void *)ctx_p->in_prm.prm_base_va, cmd_prms->in_param, cmd_prms->in_param_size);
      set_mailbox(&ctx_p->hcr_buf[0], ctx_p->in_prm.prm_base_pa);
      break;
  }

  ctx_p->hcr_buf[2] = MOSAL_cpu_to_be32(cmd_prms->input_modifier);

  switch ( cmd_prms->out_trans ) {
    case TRANS_NA:
      /* note! since these are zeroes I do not bother to deal with endianess */
      ctx_p->hcr_buf[3] = 0; 
      ctx_p->hcr_buf[4] = 0;
      break;

    case TRANS_IMMEDIATE:
      break;
    case TRANS_MAILBOX:
      cmd_prms->out_param_va = (u_int8_t *)ctx_p->out_prm.prm_base_va;
      set_mailbox(&ctx_p->hcr_buf[3], ctx_p->out_prm.prm_base_pa);
      break;
  }

  MT_INSERT32(_token, token, 16, 16);
  ctx_p->hcr_buf[5] = MOSAL_cpu_to_be32(_token);
  write_command_dw(&ctx_p->hcr_buf[6], 1, event, cmd_prms->opcode_modifier, cmd_prms->opcode);
}

/*
 *  extract_hcr
 */
static void extract_hcr(command_fields_t *cmd_prms, priv_hcr_t *hcr)
{
  switch ( cmd_prms->out_trans ) {
    case TRANS_NA:
      break;
    case TRANS_IMMEDIATE:
      {
        u_int32_t *caller_prms = (u_int32_t *)cmd_prms->out_param;
        caller_prms[0] = hcr->out_param[0];
        caller_prms[1] = hcr->out_param[1];
      }
      break;
    case TRANS_MAILBOX:
      MTL_DEBUG1("out is TRANS_MAILBOX\n");
      memcpy_from_tavor(cmd_prms->out_param, cmd_prms->out_param_va, cmd_prms->out_param_size);
      break;
  }
}

/*========== memory allocation functions ============================*/

#if 0
/*
 *  print_hcr_dump
 */
static void print_hcr_dump(struct cmd_if_context_st *entry, u_int32_t cmd)
{
#if 6 <= MAX_DEBUG
  u_int32_t i, hcr_size=PSEUDO_MT_BYTE_SIZE(tavorprm_hca_command_register_st);
  u_int8_t *hcr = (u_int8_t *)entry->hcr_virt_base;

  MTL_DEBUG6("hcr dump\n");
  for ( i=0; i<(hcr_size-4); ++i ) {
    MTL_DEBUG6("%02x\n", hcr[i]);
  }
  for ( i=0; i<4; ++i ) {
    MTL_DEBUG6("%02x\n", ((u_int8_t *)(&cmd))[i]);
  }
#endif
}


/*
 *  print_hcr_fileds
 */
static void print_hcr_fileds(u_int32_t *buf, u_int32_t cmd)
{
  u_int32_t i, hcr_size=PSEUDO_MT_BYTE_SIZE(tavorprm_hca_command_register_st);
  u_int32_t *hcr32 = buf;
  u_int32_t dst32[PSEUDO_MT_BYTE_SIZE(tavorprm_hca_command_register_st)>>2];
  u_int64_t in_prm, out_prm;
  u_int32_t in_mod;
  u_int16_t token, opcode, op_mod;
  u_int8_t e, go, status;

  for ( i=0; i<((hcr_size>>2)-1); ++i ) {
    dst32[i] = MOSAL_be32_to_cpu(hcr32[i]);
  }
  dst32[i] = MOSAL_be32_to_cpu(cmd);

  in_prm = ((u_int64_t)dst32[0]<<32) + dst32[1];
  in_mod = dst32[2];
  out_prm = ((u_int64_t)dst32[3]<<32) + dst32[4];
  token = MT_EXTRACT32(dst32[5], 16, 16);
  opcode = MT_EXTRACT32(dst32[6], 0, 12);
  op_mod = MT_EXTRACT32(dst32[6], 12, 4);
  e = MT_EXTRACT32(dst32[6], 22, 1);
  go = MT_EXTRACT32(dst32[6], 23, 1);
  status = MT_EXTRACT32(dst32[6], 24, 8);

  MTL_DEBUG5("hcr fields values\n");
  MTL_DEBUG5("in_param = "U64_FMT"\n", in_prm);
  MTL_DEBUG5("input_modifier = %x\n", in_mod);
  MTL_DEBUG5("out_param = "U64_FMT"\n", out_prm);
  MTL_DEBUG5("token = %x\n", token);
  MTL_DEBUG5("opcode = %x\n", opcode);
  MTL_DEBUG5("opcode modifier = %x\n", op_mod);
  MTL_DEBUG5("e = %d\n", e);
  MTL_DEBUG5("go = %d\n", go);
  MTL_DEBUG5("status = %x\n", status);
}
#endif

/*
 * log2()
 */
static int log2(u_int64_t arg)
{
  int i;
  u_int64_t  tmp;

  if ( arg == 0 ) {
#ifndef __DARWIN__
    return INT_MIN; /* log2(0) = -infinity */
#else
    return -1; /* 0.5 = 0  => log2(0) = -1 */
#endif
  }

  tmp = 1;
  i = 0;
  while ( tmp < arg ) {
    tmp = tmp << 1;
    ++i;
  }

  return i;
}



/*
 *  alloc_prm_ctx
 */
static HH_ret_t alloc_prm_ctx(struct cmd_if_context_st *entry,
                              prms_buf_t *prm_p,
                              MT_size_t buf_sz,
                              MT_bool in_ddr)
{
  MT_phys_addr_t pa;
  MT_size_t alloc_sz;
  MT_virt_addr_t alloc_va, va;
  HH_ret_t rc;
  call_result_t mrc;

  if ( in_ddr ) {
    /* params put in ddr */
    alloc_sz = buf_sz;
    rc = THH_ddrmm_alloc(entry->ddrmm, alloc_sz, PRM_ALIGN_SHIFT, &pa);
    if ( rc != HH_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: failed to allocate "SIZE_T_FMT" bytes in ddr"), __func__, alloc_sz);
      return rc;
    }
    va = MOSAL_io_remap(pa, alloc_sz);
    if ( !va ) {
      rc = THH_ddrmm_free(entry->ddrmm, pa, alloc_sz);
      if ( rc != HH_OK ) MTL_ERROR1(MT_FLFMT("%s: THH_ddrmm_free failed. pa=" PHYS_ADDR_FMT ", size=" SIZE_T_FMT), __func__, pa, alloc_sz);
      return HH_EAGAIN;
    }
    alloc_va = va;
    prm_p->in_ddr = TRUE;
  }
  else {
    /* params put in main memory */
    alloc_sz = buf_sz + (1<<PRM_ALIGN_SHIFT) - 1;
    alloc_va = (MT_virt_addr_t)MOSAL_pci_phys_alloc_consistent(alloc_sz, PRM_ALIGN_SHIFT);
    if ( !alloc_va ) {
      return HH_EAGAIN;
    }
    va = MT_UP_ALIGNX_VIRT(alloc_va, PRM_ALIGN_SHIFT);
    mrc = MOSAL_virt_to_phys(MOSAL_get_kernel_prot_ctx(), va, &pa);
    if ( mrc != MT_OK ) {
      MOSAL_pci_phys_free_consistent((void *)(MT_ulong_ptr_t)alloc_va, alloc_sz);
      return HH_ERR;
    }
    prm_p->in_ddr = FALSE;
  }

  prm_p->prm_alloc_va = alloc_va;
  prm_p->prm_base_va = va;
  prm_p->prm_base_pa = pa;
  prm_p->prm_buf_sz = alloc_sz;
  prm_p->allocated = 1;

  return HH_OK;
}


/*
 *  de_alloc_prm_ctx
 */
static HH_ret_t de_alloc_prm_ctx(struct cmd_if_context_st *entry,
                                 prms_buf_t *prms_p)
{
  if ( prms_p->in_ddr ) {
    MOSAL_io_unmap(prms_p->prm_base_va);
    return THH_ddrmm_free(entry->ddrmm, prms_p->prm_base_pa, prms_p->prm_buf_sz);
  }
  else {
    MOSAL_pci_phys_free_consistent((void *)(MT_ulong_ptr_t)(prms_p->prm_alloc_va), prms_p->prm_buf_sz);
    return HH_OK;
  }
}

/*
 *  alloc_cmd_contexts
 */
static HH_ret_t alloc_cmd_contexts(struct cmd_if_context_st *entry, u_int32_t num, MT_bool in_at_ddr, MT_bool out_at_ddr)
{
  cmd_ctx_t *ctx_p;
  HH_ret_t rc;
  u_int32_t i, j;
  
  ctx_p = TNMALLOC(cmd_ctx_t, num);
  if ( !ctx_p ) {
    MTL_ERROR1(MT_FLFMT("%s: failed to allocated "SIZE_T_FMT" bytes"), __func__, sizeof(cmd_ctx_t)*num);
    return HH_EAGAIN;
  }
  memset(ctx_p, 0, sizeof(cmd_ctx_t)*num);
  entry->ctx_obj.ctx_arr = ctx_p;
  entry->ctx_obj.num = num;

  for ( i=0; i<num; ++i ) {

    rc = alloc_prm_ctx(entry, &ctx_p[i].in_prm, MAX_IN_PRM_SIZE, in_at_ddr);
    if ( rc != HH_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: alloc_prm_ctx failed"), __func__);
      for ( j=0; j<i; ++j ) {
        de_alloc_prm_ctx(entry, &entry->ctx_obj.ctx_arr[j].in_prm);
        de_alloc_prm_ctx(entry, &entry->ctx_obj.ctx_arr[j].out_prm);
      }
      return rc;
    }

    rc = alloc_prm_ctx(entry, &ctx_p[i].out_prm, MAX_OUT_PRM_SIZE, out_at_ddr);
    if ( rc != HH_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: alloc_prm_ctx failed"), __func__);
      for ( j=0; j<i; ++j ) {
        de_alloc_prm_ctx(entry, &entry->ctx_obj.ctx_arr[j].in_prm);
        de_alloc_prm_ctx(entry, &entry->ctx_obj.ctx_arr[j].out_prm);
      }
      de_alloc_prm_ctx(entry, &entry->ctx_obj.ctx_arr[i].in_prm);
      return rc;
    }

    entry->ctx_obj.ctx_arr[i].ref_cnt = 0;
    entry->ctx_obj.ctx_arr[i].next_free_idx = i+1;
    entry->ctx_obj.ctx_arr[i].entry_idx = i;

    entry->ctx_obj.ctx_arr[i].token = 0;
    MOSAL_syncobj_init(&entry->ctx_obj.ctx_arr[i].syncobj);
  }

  entry->ctx_obj.ctx_arr[num-1].next_free_idx = FREE_LIST_EOL;
  entry->ctx_obj.free_list_head = 0;

  MOSAL_spinlock_init(&entry->ctx_obj.spl);
  return HH_OK;
}


/*
 *  de_alloc_cmd_contexts
 */
static HH_ret_t de_alloc_cmd_contexts(struct cmd_if_context_st *entry)
{
  HH_ret_t rc, rcx=HH_OK;
  u_int32_t i;

  for ( i=0; i<entry->ctx_obj.num; ++i ) {
    if ( entry->ctx_obj.ctx_arr[i].in_prm.allocated ) {
    rc = de_alloc_prm_ctx(entry, &entry->ctx_obj.ctx_arr[i].in_prm);
      if ( rc != HH_OK ) {
        MTL_ERROR1(MT_FLFMT("%s: de_alloc_prm_ctx failed - %s"), __func__, HH_strerror(rc));
        rcx = HH_ERR;
      }
    }

    if ( entry->ctx_obj.ctx_arr[i].out_prm.allocated ) {
      rc = de_alloc_prm_ctx(entry, &entry->ctx_obj.ctx_arr[i].out_prm);
      if ( rc != HH_OK ) {
        MTL_ERROR1(MT_FLFMT("%s: de_alloc_prm_ctx failed - %s"), __func__, HH_strerror(rc));
        rcx = HH_ERR;
      }
    }
  }
  if ( entry->ctx_obj.ctx_arr ) {
    FREE(entry->ctx_obj.ctx_arr);
    entry->ctx_obj.ctx_arr = NULL;
  }
  return rcx;
}

static HH_ret_t acq_cmd_ctx(struct cmd_if_context_st *entry, cmd_ctx_t **ctx_pp)
{
  u_int32_t last_head;

  if ( !entry->ctx_obj.ctx_arr ) {
    MTL_ERROR1(MT_FLFMT("%s: no resources were allocated"), __func__);
    return HH_EAGAIN;
  }

  if ( entry->ctx_obj.free_list_head != FREE_LIST_EOL ) {
    last_head = entry->ctx_obj.free_list_head;
    entry->ctx_obj.free_list_head = entry->ctx_obj.ctx_arr[last_head].next_free_idx;
    *ctx_pp = &entry->ctx_obj.ctx_arr[last_head];
    entry->ctx_obj.ctx_arr[last_head].token = (entry->tokens_counter<<entry->tokens_shift) | last_head;
    entry->tokens_counter++;
    entry->ctx_obj.ctx_arr[last_head].ref_cnt = 1;
    return HH_OK;
  }

  return HH_EAGAIN;
}


static void rel_cmd_ctx(struct cmd_if_context_st *entry, cmd_ctx_t *ctx_p)
{
  u_int32_t entry_idx = ctx_p->entry_idx;
  u_int32_t old_head = entry->ctx_obj.free_list_head;
  entry->ctx_obj.free_list_head = entry_idx;
  ctx_p->next_free_idx = old_head;
  ctx_p->ref_cnt = 0;
}



static HH_ret_t re_alloc_resources(struct cmd_if_context_st *entry, MT_bool in_at_ddr, MT_bool out_at_ddr)
{
  HH_ret_t rc;

  rc = de_alloc_cmd_contexts(entry);
  if ( rc != HH_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: de_alloc_cmd_contexts failed - %s"), __func__, HH_strerror(rc));
    return rc;
  }

  rc = alloc_cmd_contexts(entry, entry->sw_num_rsc, in_at_ddr, out_at_ddr);
  if ( rc != HH_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: alloc_cmd_contexts failed - %s"), __func__, HH_strerror(rc));
    return rc;
  }

  return HH_OK;
}

THH_cmd_status_t THH_cmd_notify_fatal(THH_cmd_t cmd_if, THH_fatal_err_t fatal_err)
{
  struct cmd_if_context_st *entry = (struct cmd_if_context_st *)cmd_if;
  int i;

  FUNC_IN;
  MTL_DEBUG2("%s: cmd_if = %p\n", __func__, (void *) cmd_if);
  /* Don't need spinlock here.  The value is set to TRUE and stays there until
   * cmdif is destroyed.
   */
  entry->have_fatal = TRUE;

  /* wake all processes waiting for completion of commands */
  MTL_DEBUG2(MT_FLFMT("%s: waking waiting processes"), __func__);
  MOSAL_spinlock_irq_lock(&entry->ctx_obj.spl);
  for ( i=0; i<(int)entry->ctx_obj.num; ++i ) {
    MOSAL_syncobj_signal(&entry->ctx_obj.ctx_arr[i].syncobj);
  }
  MOSAL_spinlock_unlock(&entry->ctx_obj.spl);
  MTL_DEBUG2(MT_FLFMT("%s: woke waiting processes"), __func__);


  MT_RETURN(THH_CMD_STAT_OK);
}

THH_cmd_status_t THH_cmd_handle_fatal(THH_cmd_t cmd_if)
{
  struct cmd_if_context_st *entry = (struct cmd_if_context_st *)cmd_if;

  FUNC_IN;
  MTL_DEBUG1(MT_FLFMT("%s: called"), __func__);
  /* Don't need spinlock here.     */
  if (entry->have_fatal != TRUE) {
      MT_RETURN(THH_CMD_STAT_BAD_RES_STATE);  /* only callable from within fatal state */
  }
  THH_cmd_clr_eq(cmd_if);
  MTL_DEBUG1(MT_FLFMT("%s: returning"), __func__);
  MT_RETURN(THH_CMD_STAT_OK);
}




/*
 *  print_outs_commands
 */
static void print_outs_commands(ctx_obj_t *ctxo_p)
{
  u_int32_t i;

  MOSAL_spinlock_irq_lock(&ctxo_p->spl);
  MTL_ERROR1(MT_FLFMT("list of outstanding tokens:"));
  for ( i=0; i<ctxo_p->num; ++i ) {
    if ( ctxo_p->ctx_arr[i].ref_cnt > 0 ) {
      MTL_ERROR1(MT_FLFMT("outstanding i=%d, token=0x%04x"), i, ctxo_p->ctx_arr[i].token);
    }
  }

  MOSAL_spinlock_unlock(&ctxo_p->spl);
}


static void track_exec_cmds(struct cmd_if_context_st *entry, cmd_ctx_t *ctx_p)
{
  u_int16_t token=ctx_p->token;
  int idx = token & entry->tokens_idx_mask;
  entry->track_arr[idx] = token;
}



static void print_track_arr(struct cmd_if_context_st *entry)
{
  int num=(1<<entry->tokens_shift), i, shift=entry->tokens_shift;

  for (i=0; i<num; ++i) {
    MTL_ERROR1(MT_FLFMT("%s: idx=%d, token=0x%04x, counter=%d"), __func__, i, entry->track_arr[i], entry->track_arr[i]>>shift);
  }
}
