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
 
 #include <cmdif.h>
 #include <cmd_types.h>
 #include <cmdif_priv.h>
 #include <sm_mad.h>

/* T.D.(matan): change the method of reporting SQ_DRAINING state to a more cosisntent one.*/
#define THH_CMD_QP_DRAINING_FLAG	0x80

/* For the READ_MTT and WRITE_MTT, the mailbox contains a 16-byte preamble */
#define MTT_CMD_PA_PREAMBLE_SIZE    (16)

#define EX_FLD(dst, a, st, fld) dst->fld = MT_EXTRACT_ARRAY32(a, MT_BIT_OFFSET(st, fld), MT_BIT_SIZE(st, fld))
#define INS_FLD(src, a, st, fld) MT_INSERT_ARRAY32(a, src->fld, MT_BIT_OFFSET(st, fld), MT_BIT_SIZE(st, fld))

#define EX_BOOL_FLD(dst, a, st, fld) dst->fld = ((MT_EXTRACT_ARRAY32(a, MT_BIT_OFFSET(st, fld), MT_BIT_SIZE(st, fld))==0) ? FALSE : TRUE)
#define INS_BOOL_FLD(src, a, st, fld) MT_INSERT_ARRAY32(a, ((src->fld==FALSE)?0:1), MT_BIT_OFFSET(st, fld), MT_BIT_SIZE(st, fld))

#define QP_EX_FLD(dst, a, st, fld) dst->fld = MT_EXTRACT_ARRAY32(a, MT_BIT_OFFSET(st, qpc_eec_data.fld), MT_BIT_SIZE(st, qpc_eec_data.fld))
#define QP_INS_FLD(src, a, st, fld) MT_INSERT_ARRAY32(a, src->fld, MT_BIT_OFFSET(st, qpc_eec_data.fld), MT_BIT_SIZE(st, qpc_eec_data.fld))

#define QP_EX_BOOL_FLD(dst, a, st, fld) dst->fld = ((MT_EXTRACT_ARRAY32(a, MT_BIT_OFFSET(st, qpc_eec_data.fld), MT_BIT_SIZE(st, qpc_eec_data.fld))==0) ? FALSE : TRUE)
#define QP_INS_BOOL_FLD(src, a, st, fld) MT_INSERT_ARRAY32(a, ((src->fld==FALSE)?0:1), MT_BIT_OFFSET(st, qpc_eec_data.fld), MT_BIT_SIZE(st, qpc_eec_data.fld))

#define EX_FLD64(dst, a, st, fld) dst->fld = (MT_EXTRACT_ARRAY32(a, MT_BIT_OFFSET(st, fld##_l), MT_BIT_SIZE(st, fld##_l)) | \
                                               (u_int64_t)(MT_EXTRACT_ARRAY32(a, MT_BIT_OFFSET(st, fld##_h), MT_BIT_SIZE(st, fld##_h))) << 32) 

#define INS_FLD64(src, a, st, fld) MT_INSERT_ARRAY32(a, (u_int32_t)src->fld, MT_BIT_OFFSET(st, fld##_l), MT_BIT_SIZE(st, fld##_l)) ; \
                                   MT_INSERT_ARRAY32(a, (u_int32_t)((src->fld) >> 32), MT_BIT_OFFSET(st, fld##_h), MT_BIT_SIZE(st, fld##_h))

#define INS_FLD64_SH(src, a, st, fld) MT_INSERT_ARRAY32(a, (u_int32_t)((src->fld) >> (MT_BIT_OFFSET(st, fld##_l) & MASK32(5))), MT_BIT_OFFSET(st, fld##_l), MT_BIT_SIZE(st, fld##_l)) ; \
                                   MT_INSERT_ARRAY32(a, (u_int32_t)((src->fld) >> 32), MT_BIT_OFFSET(st, fld##_h), MT_BIT_SIZE(st, fld##_h))

#define EX_FLD64_SH(dst, a, st, fld) dst->fld = ((MT_EXTRACT_ARRAY32(a, MT_BIT_OFFSET(st, fld##_l), MT_BIT_SIZE(st, fld##_l))) << ((MT_BIT_OFFSET(st, fld##_l)& MASK32(5)))  | \
                                               (u_int64_t)(MT_EXTRACT_ARRAY32(a, MT_BIT_OFFSET(st, fld##_h), MT_BIT_SIZE(st, fld##_h))) << 32) 

#define QP_EX_FLD64(dst, a, st, fld) dst->fld = (MT_EXTRACT_ARRAY32(a, MT_BIT_OFFSET(st, qpc_eec_data.fld##_1), MT_BIT_SIZE(st, qpc_eec_data.fld##_1)) | \
                                               (u_int64_t)(MT_EXTRACT_ARRAY32(a, MT_BIT_OFFSET(st, qpc_eec_data.fld##_0), MT_BIT_SIZE(st, qpc_eec_data.fld##_0))) << 32) 

#define QP_INS_FLD64(src, a, st, fld) MT_INSERT_ARRAY32(a, (u_int32_t)src->fld, MT_BIT_OFFSET(st, qpc_eec_data.fld##_1), MT_BIT_SIZE(st, qpc_eec_data.fld##_1)) ; \
                                   MT_INSERT_ARRAY32(a, (u_int32_t)((src->fld) >> 32), MT_BIT_OFFSET(st, qpc_eec_data.fld##_0), MT_BIT_SIZE(st, qpc_eec_data.fld##_0))

#define THH_CMDS_WRAP_DEBUG_LEVEL 4
#define CMDS_DBG    MTL_DEBUG4

#if defined(MAX_DEBUG) && THH_CMDS_WRAP_DEBUG_LEVEL <= MAX_DEBUG
#define THH_CMD_PRINT_DEV_LIMS(a)      THH_cmd_print_dev_lims(a)
#define THH_CMD_PRINT_HCA_PROPS(a)     THH_cmd_print_hca_props(a)
#define THH_CMD_PRINT_INIT_IB(a, b)    THH_cmd_print_init_ib(a, b)
#define THH_CMD_PRINT_QUERY_ADAPTER(a) THH_cmd_print_query_adapter(a)
#define THH_CMD_PRINT_QUERY_DDR(a)     THH_cmd_print_query_ddr(a)
#define THH_CMD_PRINT_QUERY_FW(a)      THH_cmd_print_query_fw(a)
#define THH_CMD_PRINT_CQ_CONTEXT(a)    THH_cmd_print_cq_context(a)
#define THH_CMD_PRINT_QP_CONTEXT(a)    THH_cmd_print_qp_context(a)
#define THH_CMD_PRINT_EQ_CONTEXT(a)    THH_cmd_print_eq_context(a)
#define THH_CMD_PRINT_MPT_ENTRY(a)     THH_cmd_print_mpt_entry(a)
#define THH_CMD_PRINT_MTT_ENTRIES(a,b) THH_cmd_print_mtt_entries(a,b)
#define THH_CMD_PRINT_MGM_ENTRY(a)     THH_cmd_print_mgm_entry(a)
#define THH_CMD_MAILBOX_PRINT(a,b,c)   THH_cmd_mailbox_print(a,b,c)
#else
#define THH_CMD_PRINT_DEV_LIMS(a)      
#define THH_CMD_PRINT_HCA_PROPS(a)     
#define THH_CMD_PRINT_INIT_IB(a, b)    
#define THH_CMD_PRINT_QUERY_ADAPTER(a) 
#define THH_CMD_PRINT_QUERY_DDR(a)     
#define THH_CMD_PRINT_QUERY_FW(a) 
#define THH_CMD_PRINT_CQ_CONTEXT(a)
#define THH_CMD_PRINT_QP_CONTEXT(a)
#define THH_CMD_PRINT_EQ_CONTEXT(a)
#define THH_CMD_PRINT_MPT_ENTRY(a)
#define THH_CMD_PRINT_MTT_ENTRIES(a,b)
#define THH_CMD_MAILBOX_PRINT(a,b,c) 
#define THH_CMD_PRINT_MGM_ENTRY(a)
#endif

/***************************************************** */
/************** CMD INTERFACE  UTILITIES ************* */
/***************************************************** */
static MT_bool THH_tavor_qpstate_2_vapi_qpstate(tavor_if_qp_state_t tavor_qp_state,
                                                        VAPI_qp_state_t * vapi_qp_state)
{
    switch(tavor_qp_state) {
    case TAVOR_IF_QP_STATE_RESET:
        *vapi_qp_state = VAPI_RESET;
        break;

    case TAVOR_IF_QP_STATE_INIT:
        *vapi_qp_state = VAPI_INIT;
        break;

    case TAVOR_IF_QP_STATE_RTR:
        *vapi_qp_state = VAPI_RTR;
        break;

    case TAVOR_IF_QP_STATE_RTS:
        *vapi_qp_state = VAPI_RTS;
        break;

    case TAVOR_IF_QP_STATE_SQER:
        *vapi_qp_state = VAPI_SQE;
        break;

    /* T.D.(matan): change this.*/
	case TAVOR_IF_QP_STATE_DRAINING:
        *vapi_qp_state = (VAPI_qp_state_t)(VAPI_SQD | THH_CMD_QP_DRAINING_FLAG);
		break;
    case TAVOR_IF_QP_STATE_SQD:
        *vapi_qp_state = VAPI_SQD;
        break;

    case TAVOR_IF_QP_STATE_ERR:
        *vapi_qp_state = VAPI_ERR;
        break;
    default:
        return FALSE;
    }
    return TRUE;
}
static  MT_bool THH_vapi_qpstate_2_tavor_qpstate(VAPI_qp_state_t vapi_qp_state,
                                         tavor_if_qp_state_t *tavor_qp_state)
{
    switch(vapi_qp_state) {
    case VAPI_RESET:
        *tavor_qp_state = TAVOR_IF_QP_STATE_RESET;
        break;

    case VAPI_INIT:
        *tavor_qp_state = TAVOR_IF_QP_STATE_INIT;
        break;

    case VAPI_RTR:
        *tavor_qp_state = TAVOR_IF_QP_STATE_RTR;
        break;

    case VAPI_RTS:
        *tavor_qp_state = TAVOR_IF_QP_STATE_RTS;
        break;

    case VAPI_SQE:
        *tavor_qp_state = TAVOR_IF_QP_STATE_SQER;
        break;

    case VAPI_SQD:
        *tavor_qp_state = TAVOR_IF_QP_STATE_SQD;
        break;

    case VAPI_ERR:
        *tavor_qp_state = TAVOR_IF_QP_STATE_ERR;
        break;
    default:
        return FALSE;
    }
    return TRUE;
}
                                                      

/***************************************************** */
/********** CMD INTERFACE PRINT UTILITIES **************/
/***************************************************** */

//--------------------------------------------------------------------------------------------------
#if defined(MAX_DEBUG) && THH_CMDS_WRAP_DEBUG_LEVEL <= MAX_DEBUG

static void THH_cmd_mailbox_print( void *mailbox, int size, const char * func )
{
    int i, j, maxlines, bytes_left, this_line;
    char linebuf[200], tempout[20];
    u_int8_t *iterator;

    if (mailbox == NULL) return;
    
    iterator = (u_int8_t *)mailbox;
    bytes_left = size;
    return;
    MTL_DEBUG4("MailboxPrint from function %s, starting at addr 0x%p, size=%d:\n",
               func, mailbox, size);
    
    if (size <= 0) {
        return;
    }

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
        MTL_DEBUG4("%s\n", linebuf);
    }
    MTL_DEBUG3("MailboxPrint END\n");
}


/*
 *  print_mtt_entries
 */
static void THH_cmd_print_mtt_entries(u_int32_t elts_this_loop, void *inprm)
{
  u_int32_t *p=(u_int32_t *)((u_int8_t *)inprm+MTT_CMD_PA_PREAMBLE_SIZE), i, 
      *pp = (u_int32_t *)inprm;
  u_int64_t tag;
  int present;
  return;
  MTL_DEBUG1("mtt_pa = "U64_FMT"\n", (((u_int64_t)(*pp))<<32)+*(pp+1));
  for ( i=0; i<elts_this_loop; ++i ) {
    tag = ((((u_int64_t)(*p))<<32) + (*(p+1))) & MAKE_LONGLONG(0xfffffffffffff000);
    present = (*(p+1)) & 1;
    p+=2;
    MTL_DEBUG1("MTT entry: "U64_FMT", %s\n", tag, present ? "present" : "non present");
  }
}

void THH_cmd_print_hca_props(THH_hca_props_t *hca_props)
{
    CMDS_DBG("HCA PROPS DUMP (THH_hca_props_t structure)\n");

    CMDS_DBG("hca_core_click = %d\n", hca_props->hca_core_clock);

    CMDS_DBG( "he (host endian) = %s\n", hca_props->he ? "Big Endian" : "Little Endian");

    CMDS_DBG( "re (Router Mode Enable) = %s\n", hca_props->re ? "TRUE" :  "FALSE");

    CMDS_DBG( "router_qp = %s\n", hca_props->router_qp ? "TRUE" :  "FALSE");

    CMDS_DBG( "ud (UD address vector protection) = %s\n", hca_props->ud ? "TRUE" :  "FALSE");
    
    CMDS_DBG( "udp (UDP port check enabled) = %s\n", hca_props->udp ? "TRUE" :  "FALSE");
    
    /* multicast parameters */
    CMDS_DBG( "\nmulticast_parameters.log_mc_table_entry_sz = %d\n", hca_props->multicast_parameters.log_mc_table_entry_sz);
    CMDS_DBG( "multicast_parameters.log_mc_table_sz = %d\n", hca_props->multicast_parameters.log_mc_table_sz);
    CMDS_DBG( "multicast_parameters.mc_base_addr = "U64_FMT"\n", hca_props->multicast_parameters.mc_base_addr);
    CMDS_DBG( "multicast_parameters.mc_hash_fn = %d\n", hca_props->multicast_parameters.mc_hash_fn);
    CMDS_DBG( "multicast_parameters.mc_table_hash_sz = %d\n", hca_props->multicast_parameters.mc_table_hash_sz);

    /* QP,EEC, EQC, RDB, CQC parameters */
    CMDS_DBG( "\nqpc_eec_cqc_eqc_rdb_parameters.cqc_base_addr = "U64_FMT"\n", hca_props->qpc_eec_cqc_eqc_rdb_parameters.cqc_base_addr);
    CMDS_DBG( "qpc_eec_cqc_eqc_rdb_parameters.eec_base_addr = "U64_FMT"\n", hca_props->qpc_eec_cqc_eqc_rdb_parameters.eec_base_addr);
    CMDS_DBG( "qpc_eec_cqc_eqc_rdb_parameters.eeec_base_addr = "U64_FMT"\n", hca_props->qpc_eec_cqc_eqc_rdb_parameters.eeec_base_addr);
    CMDS_DBG( "qpc_eec_cqc_eqc_rdb_parameters.eqc_base_addr = "U64_FMT"\n", hca_props->qpc_eec_cqc_eqc_rdb_parameters.eqc_base_addr);
    CMDS_DBG( "qpc_eec_cqc_eqc_rdb_parameters.eqpc_base_addr = "U64_FMT"\n", hca_props->qpc_eec_cqc_eqc_rdb_parameters.eqpc_base_addr);
    CMDS_DBG( "qpc_eec_cqc_eqc_rdb_parameters.qpc_base_addr = "U64_FMT"\n", hca_props->qpc_eec_cqc_eqc_rdb_parameters.qpc_base_addr);
    CMDS_DBG( "qpc_eec_cqc_eqc_rdb_parameters.rdb_base_addr = "U64_FMT"\n", hca_props->qpc_eec_cqc_eqc_rdb_parameters.rdb_base_addr);
    CMDS_DBG( "qpc_eec_cqc_eqc_rdb_parameters.log_num_eq = %d\n", hca_props->qpc_eec_cqc_eqc_rdb_parameters.log_num_eq);
    CMDS_DBG( "qpc_eec_cqc_eqc_rdb_parameters.log_num_of_cq = %d\n", hca_props->qpc_eec_cqc_eqc_rdb_parameters.log_num_of_cq);
    CMDS_DBG( "qpc_eec_cqc_eqc_rdb_parameters.log_num_of_ee = %d\n", hca_props->qpc_eec_cqc_eqc_rdb_parameters.log_num_of_ee);
    CMDS_DBG( "qpc_eec_cqc_eqc_rdb_parameters.log_num_of_qp = %d\n", hca_props->qpc_eec_cqc_eqc_rdb_parameters.log_num_of_qp);

    /* TPT parameters */
    CMDS_DBG( "\ntpt_parameters.mpt_base_adr = "U64_FMT"\n", hca_props->tpt_parameters.mpt_base_adr);
    CMDS_DBG( "tpt_parameters.mtt_base_adr = "U64_FMT"\n", hca_props->tpt_parameters.mtt_base_addr);
    CMDS_DBG( "tpt_parameters.log_mpt_sz = %d\n", hca_props->tpt_parameters.log_mpt_sz);
    CMDS_DBG( "tpt_parameters.mtt_segment_size = %d\n", hca_props->tpt_parameters.mtt_segment_size);
    CMDS_DBG( "tpt_parameters.mtt_version = %d\n", hca_props->tpt_parameters.mtt_version);
    CMDS_DBG( "tpt_parameters.pfto = %d\n", hca_props->tpt_parameters.pfto);

    /* UAR parameters */
    CMDS_DBG( "\nuar_parameters.uar_base_addr = "U64_FMT"\n", hca_props->uar_parameters.uar_base_addr);
    CMDS_DBG( "uar_parameters.uar_scratch_base_addr = "U64_FMT"\n", hca_props->uar_parameters.uar_scratch_base_addr);
    CMDS_DBG( "uar_parameters.uar_page_sz = %d\n", hca_props->uar_parameters.uar_page_sz);

    /* UDAV parameters */
    CMDS_DBG( "\nudavtable_memory_parameters.l_key = 0x%x\n", hca_props->udavtable_memory_parameters.l_key);
    CMDS_DBG( "udavtable_memory_parameters.pd = %d\n", hca_props->udavtable_memory_parameters.pd);
    CMDS_DBG( "udavtable_memory_parameters.xlation_en = %s\n", hca_props->udavtable_memory_parameters.xlation_en ? "TRUE" : "FALSE");
}

void THH_cmd_print_dev_lims(THH_dev_lim_t *dev_lim)
{
    CMDS_DBG("QUERY DEV LIMS DUMP (THH_dev_lim_t structure)\n");

    CMDS_DBG( "dev_lim->apm (Automatic Path Migration) = %s\n", dev_lim->apm ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->atm (Atomic Operations) = %s\n", dev_lim->atm ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->avp (Address Vector port checking) = %s\n", dev_lim->avp ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->udm (UD Multicast support) = %s\n", dev_lim->udm ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->mw (Memory Windows) = %s\n", dev_lim->mw ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->pg (Paging on-demand) = %s\n", dev_lim->pg ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->pkv (PKey Violation Counter) = %s\n", dev_lim->pkv ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->pkv (PKey Violation Counter) = %s\n", dev_lim->pkv ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->qkv (QKey Violation Counter) = %s\n", dev_lim->qkv ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->r (Router Mode) = %s\n", dev_lim->r ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->raw_ether (Raw Ethernet mode) = %s\n", dev_lim->raw_ether ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->raw_ipv6 (Raw IpV6 mode) = %s\n", dev_lim->raw_ipv6 ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->rc (RC Transport) = %s\n", dev_lim->rc ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->rd (RD Transport) = %s\n", dev_lim->rd ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->uc (UC Transport) = %s\n", dev_lim->uc ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->ud (UD Transport) = %s\n", dev_lim->ud ? "TRUE" :  "FALSE");
    CMDS_DBG( "dev_lim->rm (Raw Multicast) = %s\n", dev_lim->rm ? "TRUE" :  "FALSE");
    /* multicast parameters */
    CMDS_DBG( "dev_lim->cqc_entry_sz = %d\n",dev_lim->cqc_entry_sz);
    CMDS_DBG( "dev_lim->eec_entry_sz = %d\n",dev_lim->eec_entry_sz);
    CMDS_DBG( "dev_lim->eeec_entry_sz = %d\n",dev_lim->eeec_entry_sz);
    CMDS_DBG( "dev_lim->eqc_entry_sz = %d\n",dev_lim->eqc_entry_sz);
    CMDS_DBG( "dev_lim->eqpc_entry_sz = %d\n",dev_lim->eqpc_entry_sz);
    CMDS_DBG( "dev_lim->log_max_cq_sz = %d\n",dev_lim->log_max_cq_sz);
    CMDS_DBG( "dev_lim->qpc_entry_sz = %d\n",dev_lim->qpc_entry_sz);
    CMDS_DBG( "dev_lim->max_uar_sz = %d\n",dev_lim->uar_sz);
    CMDS_DBG( "dev_lim->uar_scratch_entry_sz = %d\n",dev_lim->uar_scratch_entry_sz);
    CMDS_DBG( "dev_lim->log_max_av = %d\n",dev_lim->log_max_av);
    CMDS_DBG( "dev_lim->log_max_cq = %d\n",dev_lim->log_max_cq);
    CMDS_DBG( "dev_lim->log_max_cq_sz = %d\n",dev_lim->log_max_cq_sz);
    CMDS_DBG( "dev_lim->log_max_ee = %d\n",dev_lim->log_max_ee);
    CMDS_DBG( "dev_lim->log_max_eq = %d\n",dev_lim->log_max_eq);
    CMDS_DBG( "dev_lim->log_max_gid = %d\n",dev_lim->log_max_gid);
    CMDS_DBG( "dev_lim->log_max_mcg = %d\n",dev_lim->log_max_mcg);
    CMDS_DBG( "dev_lim->log_max_mpts = %d\n",dev_lim->log_max_mpts);
    CMDS_DBG( "dev_lim->log_max_mtt_seg = %d\n",dev_lim->log_max_mtt_seg);
    CMDS_DBG( "dev_lim->log_max_mrw_sz = %d\n",dev_lim->log_max_mrw_sz);
    CMDS_DBG( "dev_lim->log_max_pd = %d\n",dev_lim->log_max_pd);
    CMDS_DBG( "dev_lim->log_max_pkey = %d\n",dev_lim->log_max_pkey);
    CMDS_DBG( "dev_lim->log_max_qp = %d\n",dev_lim->log_max_qp);
    CMDS_DBG( "dev_lim->log_max_qp_mcg = %d\n",dev_lim->log_max_qp_mcg);
    CMDS_DBG( "dev_lim->log_max_qp_sz = %d\n",dev_lim->log_max_qp_sz);
    CMDS_DBG( "dev_lim->log_max_ra_req_qp = %d\n",dev_lim->log_max_ra_req_qp);
    CMDS_DBG( "dev_lim->log_max_ra_res_qp = %d\n",dev_lim->log_max_ra_res_qp);
    CMDS_DBG( "dev_lim->log_max_ra_res_global = %d\n",dev_lim->log_max_ra_res_global);
    CMDS_DBG( "dev_lim->log_max_rdds = %d\n",dev_lim->log_max_rdds);
    CMDS_DBG( "dev_lim->log_pg_sz = %d\n",dev_lim->log_pg_sz);
    CMDS_DBG( "dev_lim->max_desc_sz = %d\n",dev_lim->max_desc_sz);
    CMDS_DBG( "dev_lim->max_mtu = %d\n",dev_lim->max_mtu);
    CMDS_DBG( "dev_lim->max_port_width = %d\n",dev_lim->max_port_width);
    CMDS_DBG( "dev_lim->max_sg = %d\n",dev_lim->max_sg);
    CMDS_DBG( "dev_lim->max_vl = %d\n",dev_lim->max_vl);
    CMDS_DBG( "dev_lim->num_ports = %d\n",dev_lim->num_ports);
    CMDS_DBG( "dev_lim->log2_rsvd_qps = %d\n",dev_lim->log2_rsvd_qps);
    CMDS_DBG( "dev_lim->log2_rsvd_ees = %d\n",dev_lim->log2_rsvd_ees);
    CMDS_DBG( "dev_lim->log2_rsvd_cqs = %d\n",dev_lim->log2_rsvd_cqs);
    CMDS_DBG( "dev_lim->num_rsvd_eqs = %d\n",dev_lim->num_rsvd_eqs);
    CMDS_DBG( "dev_lim->log2_rsvd_mrws = %d\n",dev_lim->log2_rsvd_mrws);
    CMDS_DBG( "dev_lim->log2_rsvd_mtts = %d\n",dev_lim->log2_rsvd_mtts);
    CMDS_DBG( "dev_lim->num_rsvd_uars = %d\n",dev_lim->num_rsvd_uars);
    CMDS_DBG( "dev_lim->num_rsvd_rdds = %d\n",dev_lim->num_rsvd_rdds);
    CMDS_DBG( "dev_lim->num_rsvd_pds = %d\n",dev_lim->num_rsvd_pds);
    CMDS_DBG( "dev_lim->local_ca_ack_delay = %d\n",dev_lim->local_ca_ack_delay);
}

void THH_cmd_print_query_fw(THH_fw_props_t *fw_props)
{
    CMDS_DBG("QUERY FW DUMP (THH_fw_props_t structure)\n");
    CMDS_DBG( "fw_props->cmd_interface_rev = 0x%x\n", fw_props->cmd_interface_rev);
    CMDS_DBG( "fw_props->fw_rev_major = 0x%x\n", fw_props->fw_rev_major);
    CMDS_DBG( "fw_props->fw_rev_minor = 0x%x\n", fw_props->fw_rev_minor);
    CMDS_DBG( "fw_props->fw_rev_subminor = 0x%x\n", fw_props->fw_rev_subminor);
    CMDS_DBG( "fw_props->fw_base_addr = "U64_FMT"\n", fw_props->fw_base_addr);
    CMDS_DBG( "fw_props->fw_end_addr = "U64_FMT"\n", fw_props->fw_end_addr);
    CMDS_DBG( "fw_props->error_buf_start = "U64_FMT"\n", fw_props->error_buf_start);
    CMDS_DBG( "fw_props->error_buf_size = %d\n", fw_props->error_buf_size);
}

void THH_cmd_print_query_adapter( THH_adapter_props_t *adapter_props)
{
    CMDS_DBG("QUERY ADAPTER DUMP (THH_adapter_props_t structure)\n");
    CMDS_DBG( "adapter_props->device_id = %d\n", adapter_props->device_id);
    CMDS_DBG( "adapter_props->intapin = %d\n", adapter_props->intapin);
    CMDS_DBG( "adapter_props->revision_id = %d\n", adapter_props->revision_id);
    CMDS_DBG( "adapter_props->vendor_id = %d\n", adapter_props->vendor_id);
}

void THH_cmd_print_query_ddr( THH_ddr_props_t *ddr_props)
{
    CMDS_DBG("QUERY DDR DUMP (THH_ddr_props_t structure)\n");
    CMDS_DBG( "ddr_props->ddr_start_adr = "U64_FMT"\n", ddr_props->ddr_start_adr);
    CMDS_DBG( "ddr_props->ddr_end_adr = "U64_FMT"\n", ddr_props->ddr_end_adr);
    CMDS_DBG( "\nddr_props->dimm0.di = %d\n", ddr_props->dimm0.di);
    CMDS_DBG( "ddr_props->dimm0.dimmsize = %d\n", ddr_props->dimm0.dimmsize);
    CMDS_DBG( "ddr_props->dimm0.dimmstatus = %d\n", ddr_props->dimm0.dimmstatus);
    CMDS_DBG( "ddr_props->dimm0.vendor_id = "U64_FMT"\n", ddr_props->dimm0.vendor_id);
    CMDS_DBG( "\nddr_props->dimm1.di = %d\n", ddr_props->dimm1.di);
    CMDS_DBG( "ddr_props->dimm1.dimmsize = %d\n", ddr_props->dimm1.dimmsize);
    CMDS_DBG( "ddr_props->dimm1.dimmstatus = %d\n", ddr_props->dimm1.dimmstatus);
    CMDS_DBG( "ddr_props->dimm1.vendor_id = "U64_FMT"\n", ddr_props->dimm1.vendor_id);
    CMDS_DBG( "\nddr_props->dimm2.di = %d\n", ddr_props->dimm2.di);
    CMDS_DBG( "ddr_props->dimm2.dimmsize = %d\n", ddr_props->dimm2.dimmsize);
    CMDS_DBG( "ddr_props->dimm2.dimmstatus = %d\n", ddr_props->dimm2.dimmstatus);
    CMDS_DBG( "ddr_props->dimm2.vendor_id = "U64_FMT"\n", ddr_props->dimm2.vendor_id);
    CMDS_DBG( "\nddr_props->dimm3.di = %d\n", ddr_props->dimm3.di);
    CMDS_DBG( "ddr_props->dimm3.dimmsize = %d\n", ddr_props->dimm3.dimmsize);
    CMDS_DBG( "ddr_props->dimm3.dimmstatus = %d\n", ddr_props->dimm3.dimmstatus);
    CMDS_DBG( "ddr_props->dimm3.vendor_id = "U64_FMT"\n", ddr_props->dimm3.vendor_id);
    CMDS_DBG( "ddr_props->dh = %s\n", (ddr_props->dh ? "TRUE" : "FALSE"));
    CMDS_DBG( "ddr_props->ap = %d\n", ddr_props->ap);
    CMDS_DBG( "ddr_props->di = %d\n", ddr_props->di);
}

void THH_cmd_print_init_ib(IB_port_t port, THH_port_init_props_t *port_init_props)
{
    CMDS_DBG("INIT_IB DUMP (THH_port_init_props_t structure) for port %d\n", port);
    CMDS_DBG( "port_init_props->max_gid = %d\n", port_init_props->max_gid);
    CMDS_DBG( "port_init_props->max_pkey = %d\n", port_init_props->max_pkey);
    CMDS_DBG( "port_init_props->mtu_cap = 0x%x\n", port_init_props->mtu_cap);
    CMDS_DBG( "port_init_props->port_width_cap = 0x%x\n", port_init_props->port_width_cap);
    CMDS_DBG( "port_init_props->vl_cap = 0x%x\n", port_init_props->vl_cap);
    CMDS_DBG( "port_init_props->g0 = %s\n", port_init_props->g0 ? "TRUE" : "FALSE");
    CMDS_DBG( "port_init_props->guid0 = 0x%2x%2x%2x%2x%2x%2x%2x%2x",
                port_init_props->guid0[0], port_init_props->guid0[1], port_init_props->guid0[2], 
                port_init_props->guid0[3], port_init_props->guid0[4], port_init_props->guid0[5], 
                port_init_props->guid0[6], port_init_props->guid0[7] );
}

void THH_cmd_print_cq_context(THH_cqc_t *cqc)
{
    return;
    CMDS_DBG("CQ CONTEXT DUMP (THH_cqc_t structure)\n");
    CMDS_DBG( "cqc->st = 0x%x\n", cqc->st);
    CMDS_DBG( "cqc->oi (overrun ignore) = %s\n",  cqc->oi ? "TRUE" : "FALSE");
    CMDS_DBG( "cqc->tr (translation required) = %s\n",  cqc->tr ? "TRUE" : "FALSE");
    CMDS_DBG( "cqc->status = 0x%x\n",  cqc->status);
    CMDS_DBG( "cqc->start_address = "U64_FMT"\n",  cqc->start_address);
    CMDS_DBG( "cqc->usr_page = 0x%X\n",  cqc->usr_page);
    CMDS_DBG( "cqc->log_cq_size = %d\n",  cqc->log_cq_size);
    CMDS_DBG( "cqc->c_eqn = 0x%X\n",  cqc->c_eqn);
    CMDS_DBG( "cqc->e_eqn = 0x%X\n",  cqc->e_eqn);
    CMDS_DBG( "cqc->pd = 0x%X\n",  cqc->pd);
    CMDS_DBG( "cqc->l_key = 0x%X\n",  cqc->l_key);
    CMDS_DBG( "cqc->last_notified_indx = 0x%X\n",  cqc->last_notified_indx);
    CMDS_DBG( "cqc->solicit_producer_indx = 0x%X\n",  cqc->solicit_producer_indx);
    CMDS_DBG( "cqc->consumer_indx = 0x%X\n",  cqc->consumer_indx);
    CMDS_DBG( "cqc->producer_indx = 0x%X\n",  cqc->producer_indx);
    CMDS_DBG( "cqc->cqn = 0x%X\n",  cqc->cqn);
}
void THH_cmd_print_qp_context(THH_qpee_context_t *qpc)
{
    return;
    CMDS_DBG("QPEE CONTEXT DUMP (THH_qpee_context_t structure)\n");
    CMDS_DBG( "QPC ver = 0x%x\n", qpc->ver);
    // 'te' field has been removed:
	//CMDS_DBG( "QPC Address Translation Enabled = %s\n", (qpc->te ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC Descriptor Event Enabled = %s\n", (qpc->de ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC Path Migration State = 0x%x\n", qpc->pm_state);
    CMDS_DBG( "QPC Service Type = 0x%x\n", qpc->st);
    CMDS_DBG( "QPC VAPI-encoded State = %d\n", qpc->state);
    CMDS_DBG( "QPC Sched Queue = %d\n", qpc->sched_queue);
    CMDS_DBG( "QPC msg_max = %d\n", qpc->msg_max);
    CMDS_DBG( "QPC MTU (encoded) = %d\n", qpc->mtu);
    CMDS_DBG( "QPC usr_page = 0x%x\n", qpc->usr_page);
    CMDS_DBG( "QPC local_qpn_een = 0x%x\n", qpc->local_qpn_een);
    CMDS_DBG( "QPC remote_qpn_een = 0x%x\n", qpc->remote_qpn_een);
    CMDS_DBG( "QPC pd = 0x%x\n", qpc->pd);
    CMDS_DBG( "QPC wqe_base_adr = 0x%x\n", qpc->wqe_base_adr);
    CMDS_DBG( "QPC wqe_lkey = 0x%x\n", qpc->wqe_lkey);
    CMDS_DBG( "QPC ssc = %s\n", (qpc->ssc ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC sic = %s\n", (qpc->sic ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC sae = %s\n", (qpc->sae ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC swe = %s\n", (qpc->swe ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC sre = %s\n", (qpc->sre ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC retry_count = 0x%x\n", qpc->retry_count);
    CMDS_DBG( "QPC sra_max = 0x%x\n", qpc->sra_max);
    CMDS_DBG( "QPC flight_lim = 0x%x\n", qpc->flight_lim);
    CMDS_DBG( "QPC ack_req_freq = 0x%x\n", qpc->ack_req_freq);
    CMDS_DBG( "QPC next_send_psn = 0x%x\n", qpc->next_send_psn);
    CMDS_DBG( "QPC cqn_snd = 0x%x\n", qpc->cqn_snd);
    CMDS_DBG( "QPC next_snd_wqe = "U64_FMT"\n", qpc->next_snd_wqe);
    CMDS_DBG( "QPC rsc = %s\n", (qpc->rsc ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC ric = %s\n", (qpc->ric ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC rae = %s\n", (qpc->rae ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC rwe = %s\n", (qpc->rwe ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC rre = %s\n", (qpc->rre ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC rra_max = 0x%x\n", qpc->rra_max);
    CMDS_DBG( "QPC next_rcv_psn = 0x%x\n", qpc->next_rcv_psn);
    CMDS_DBG( "QPC min_rnr_nak = 0x%x\n", qpc->min_rnr_nak);
    CMDS_DBG( "QPC ra_buff_indx = 0x%x\n", qpc->ra_buff_indx);
    CMDS_DBG( "QPC cqn_rcv = 0x%x\n", qpc->cqn_rcv);
    CMDS_DBG( "QPC next_rcv_wqe = "U64_FMT"\n", qpc->next_rcv_wqe);
    CMDS_DBG( "QPC q_key = 0x%x\n", qpc->q_key);
    CMDS_DBG( "QPC srqn = 0x%x\n", qpc->srqn);
    CMDS_DBG( "QPC srq = %s\n", (qpc->srq ? "TRUE" : "FALSE"));
    CMDS_DBG( "QPC primary.ack_timeout = %d\n" , qpc->primary_address_path.ack_timeout);
    CMDS_DBG( "QPC primary.max_stat_rate = %d\n" , qpc->primary_address_path.max_stat_rate);
}
void THH_cmd_print_eq_context(THH_eqc_t *eqc)
{
    return;
    CMDS_DBG("EQ CONTEXT DUMP (THH_eqc_t structure)\n");
    CMDS_DBG( "eqc->st = 0x%x\n", eqc->st);
    CMDS_DBG( "eqc->oi (overrun ignore) = %s\n",  eqc->oi ? "TRUE" : "FALSE");
    CMDS_DBG( "eqc->tr (translation required) = %s\n",  eqc->tr ? "TRUE" : "FALSE");
    CMDS_DBG( "eqc->owner = %s\n",  (eqc->owner == THH_OWNER_SW ? "THH_OWNER_SW" : "THH_OWNER_HW"));
    CMDS_DBG( "eqc->status = 0x%x\n",  eqc->status);
    CMDS_DBG( "eqc->start_address = "U64_FMT"\n",  eqc->start_address);
    CMDS_DBG( "eqc->usr_page = 0x%x\n",  eqc->usr_page);
    CMDS_DBG( "eqc->log_eq_size = %d\n",  eqc->log_eq_size);
    CMDS_DBG( "eqc->intr = 0x%x\n",  eqc->intr);
    CMDS_DBG( "eqc->lost_count = 0x%x\n",  eqc->lost_count);
    CMDS_DBG( "eqc->l_key = 0x%X\n",  eqc->lkey);
    CMDS_DBG( "eqc->pd = 0x%X\n",  eqc->pd);
    CMDS_DBG( "eqc->consumer_indx = 0x%X\n",  eqc->consumer_indx);
    CMDS_DBG( "eqc->producer_indx = 0x%X\n",  eqc->producer_indx);
}
void THH_cmd_print_mpt_entry(THH_mpt_entry_t *mpt)
{
    CMDS_DBG("MPT ENTRY DUMP (THH_mpt_entry_t structure)\n");
    CMDS_DBG( "MPT entry type = %s\n", (mpt->r_w ? "REGION" : "WINDOW"));
    CMDS_DBG( "MPT physical addr flag = %s\n", (mpt->pa ? "TRUE" : "FALSE"));
    CMDS_DBG( "MPT Local read access = %s\n", (mpt->lr ? "TRUE" : "FALSE"));
    CMDS_DBG( "MPT Local write access = %s\n", (mpt->lw ? "TRUE" : "FALSE"));
    CMDS_DBG( "MPT Remote read access = %s\n", (mpt->rr ? "TRUE" : "FALSE"));
    CMDS_DBG( "MPT Remote write access = %s\n", (mpt->rw ? "TRUE" : "FALSE"));
    CMDS_DBG( "MPT Atomic access = %s\n", (mpt->a ? "TRUE" : "FALSE"));
    CMDS_DBG( "MPT Atomic access = %s\n", (mpt->a ? "TRUE" : "FALSE"));
    CMDS_DBG( "MPT All writes posted  = %s\n", (mpt->pw ? "TRUE" : "FALSE"));
    CMDS_DBG( "MPT m_io  = %s\n", (mpt->m_io ? "TRUE" : "FALSE"));
    CMDS_DBG( "MPT Status = 0x%x\n", mpt->status);
    CMDS_DBG( "MPT Page size = %d (Actual size is [4K]*2^Page_size)\n", mpt->page_size);
    CMDS_DBG( "MPT mem key = 0x%x\n", mpt->mem_key);
    CMDS_DBG( "MPT pd = 0x%x\n", mpt->pd);
    CMDS_DBG( "MPT start_address = "U64_FMT"\n", mpt->start_address);
    CMDS_DBG( "MPT length = "U64_FMT"\n", mpt->reg_wnd_len);
    CMDS_DBG( "MPT lkey = 0x%x\n", mpt->lkey);
    CMDS_DBG( "MPT win_cnt = 0x%x\n", mpt->win_cnt);
    CMDS_DBG( "MPT win_cnt_limit = 0x%x\n", mpt->win_cnt_limit);
    CMDS_DBG( "MPT MTT seg addr = "U64_FMT"\n", mpt->mtt_seg_adr);
}

void THH_cmd_print_mgm_entry(THH_mcg_entry_t *mgm)
{
    IB_wqpn_t *qp_iterator;
    u_int32_t   i;

    CMDS_DBG("MGM ENTRY DUMP (THH_mcg_entry_t structure)\n");
    CMDS_DBG( "MGM next_gid_index = 0x%x\n", mgm->next_gid_index);
    CMDS_DBG("MGM GID = %d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d \n",
             mgm->mgid[0], mgm->mgid[1], mgm->mgid[2], mgm->mgid[3]
             , mgm->mgid[4], mgm->mgid[5], mgm->mgid[6], mgm->mgid[7]
             , mgm->mgid[8], mgm->mgid[9], mgm->mgid[10], mgm->mgid[10]
             , mgm->mgid[12], mgm->mgid[13], mgm->mgid[14], mgm->mgid[15]);
    CMDS_DBG( "MGM valid_qps = %d \n", mgm->valid_qps);
    for (qp_iterator = mgm->qps, i = 0; i < mgm->valid_qps; i++, qp_iterator++) {
        CMDS_DBG( "MGM qps[%d] = 0x%x\n", i, *qp_iterator);
    }
}

#else

void THH_cmd_print_hca_props(THH_hca_props_t *hca_props) {}
void THH_cmd_print_dev_lims(THH_dev_lim_t *dev_lim) {}
void THH_cmd_print_query_fw(THH_fw_props_t *fw_props) {}
void THH_cmd_print_query_adapter( THH_adapter_props_t *adapter_props) {}
void THH_cmd_print_query_ddr( THH_ddr_props_t *ddr_props) {}
void THH_cmd_print_init_ib(IB_port_t port, THH_port_init_props_t *port_init_props) {}
void THH_cmd_print_cq_context(THH_cqc_t *cqc) {}
void THH_cmd_print_qp_context(THH_qpee_context_t *qpc) {}
void THH_cmd_print_eq_context(THH_eqc_t *eqc) {}
void THH_cmd_print_mpt_entry(THH_mpt_entry_t *mpt) {}
void THH_cmd_print_mgm_entry(THH_mcg_entry_t *mgm) {}

#endif   /* #if THH_CMDS_WRAP_DEBUG_LEVEL */
/***************************************************** */
/************* END of PRINT UTILITIES **************** */
/***************************************************** */


/*
 *  THH_cmd_QUERY_DEV_LIM
 */ 
THH_cmd_status_t THH_cmd_QUERY_DEV_LIM(THH_cmd_t cmd_if, THH_dev_lim_t *dev_lim)
{
  command_fields_t cmd_desc;
  u_int8_t *outprm;
  THH_cmd_status_t rc; 
  u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_query_dev_lim_st);

  FUNC_IN;
  outprm = TNMALLOC(u_int8_t, buf_size);
  if (outprm == NULL) {
    MT_RETURN(THH_CMD_STAT_EAGAIN);
  }
  memset(outprm, 0, buf_size);

  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = 0;
  cmd_desc.out_param = outprm;
  cmd_desc.out_param_size = buf_size;
  cmd_desc.out_trans = TRANS_MAILBOX;
  cmd_desc.opcode = TAVOR_IF_CMD_QUERY_DEV_LIM;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_QUERY_DEV_LIM;
  
  rc = cmd_invoke(cmd_if, &cmd_desc);
  if ( rc != THH_CMD_STAT_OK ) {
    FREE(outprm);
    MT_RETURN(rc);
  }

  if ( dev_lim ) {
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_qp);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log2_rsvd_qps);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_qp_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_srqs);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log2_rsvd_srqs);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_srq_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_ee);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log2_rsvd_ees);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_cq);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log2_rsvd_cqs);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_cq_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_eq);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, num_rsvd_eqs);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_mpts);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_mtt_seg);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log2_rsvd_mrws);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_mrw_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log2_rsvd_mtts);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_av);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_ra_res_qp);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_ra_req_qp);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_ra_res_global);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, num_ports);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, max_vl);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, max_port_width);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, max_mtu);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, local_ca_ack_delay);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_gid);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_pkey);

    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, rc);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, uc);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, ud);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, rd);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, raw_ipv6);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, raw_ether);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, srq);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, pkv);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, qkv);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, mw);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, apm);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, atm);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, rm);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, avp);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, udm);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, pg);
    EX_BOOL_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, r);
    
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_pg_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, uar_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, num_rsvd_uars);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, max_desc_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, max_sg);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_mcg);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_qp_mcg);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_rdds);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, num_rsvd_rdds);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, log_max_pd);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, num_rsvd_pds);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, qpc_entry_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, eec_entry_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, eqpc_entry_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, eeec_entry_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, cqc_entry_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, eqc_entry_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, srq_entry_sz);
    EX_FLD(dev_lim, outprm, tavorprm_query_dev_lim_st, uar_scratch_entry_sz);
    THH_CMD_PRINT_DEV_LIMS(dev_lim);
  }

  FREE(outprm);
  MT_RETURN(rc);
}

/*
 *  THH_cmd_QUERY_FW
 */ 
THH_cmd_status_t THH_cmd_QUERY_FW(THH_cmd_t cmd_if, THH_fw_props_t *fw_props)
{
  command_fields_t cmd_desc;
  //u_int8_t outprm[PSEUDO_MT_BYTE_SIZE(tavorprm_query_fw_st)];
  u_int8_t *outprm;
  THH_cmd_status_t rc;
  u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_query_fw_st);

  FUNC_IN;
  outprm = TNMALLOC(u_int8_t, buf_size);
  if (outprm == NULL) {
    MT_RETURN(THH_CMD_STAT_EAGAIN);
  }
  memset(outprm, 0, buf_size);
  
  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = 0;
  cmd_desc.out_param = outprm;
  cmd_desc.out_param_size = buf_size;
  cmd_desc.out_trans = TRANS_MAILBOX;
  cmd_desc.opcode = TAVOR_IF_CMD_QUERY_FW;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_QUERY_FW;
  rc = cmd_invoke(cmd_if, &cmd_desc);
  if ( rc != THH_CMD_STAT_OK ) {
    FREE(outprm);
    MT_RETURN(rc);
  }

  EX_FLD(fw_props, outprm, tavorprm_query_fw_st, fw_rev_major);
  EX_FLD(fw_props, outprm, tavorprm_query_fw_st, fw_rev_minor);
  EX_FLD(fw_props, outprm, tavorprm_query_fw_st, fw_rev_subminor);
  EX_FLD(fw_props, outprm, tavorprm_query_fw_st, cmd_interface_rev);
  EX_FLD(fw_props, outprm, tavorprm_query_fw_st, log_max_outstanding_cmd);
  EX_FLD64(fw_props, outprm, tavorprm_query_fw_st, fw_base_addr);
  EX_FLD64(fw_props, outprm, tavorprm_query_fw_st, fw_end_addr);
  EX_FLD64(fw_props, outprm, tavorprm_query_fw_st, error_buf_start);
  EX_FLD(fw_props, outprm, tavorprm_query_fw_st, error_buf_size);
  FREE(outprm);

  THH_cmd_set_fw_props(cmd_if, fw_props);

  THH_CMD_PRINT_QUERY_FW(fw_props);
  
  MT_RETURN(rc);
}

/*
 *  THH_cmd_QUERY_DDR
 */ 
THH_cmd_status_t THH_cmd_QUERY_DDR(THH_cmd_t cmd_if, THH_ddr_props_t *ddr_props)
{
  command_fields_t cmd_desc;
  u_int8_t *outprm;
  THH_cmd_status_t rc;
  u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_query_ddr_st);

  FUNC_IN;
  outprm = TNMALLOC(u_int8_t, buf_size);
  if (outprm == NULL) {
    MT_RETURN(THH_CMD_STAT_EAGAIN);
  }
  memset(outprm, 0, buf_size);
  
  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = 0;
  cmd_desc.out_param = outprm;
  cmd_desc.out_param_size = buf_size;
  cmd_desc.out_trans = TRANS_MAILBOX;
  cmd_desc.opcode = TAVOR_IF_CMD_QUERY_DDR;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_QUERY_DDR;
  
  rc = cmd_invoke(cmd_if, &cmd_desc);
  if ( rc != THH_CMD_STAT_OK ) {
    FREE(outprm);
    MT_RETURN(rc);
  }

  if ( ddr_props ) {
    EX_FLD64(ddr_props, outprm, tavorprm_query_ddr_st, ddr_start_adr);
    EX_FLD64(ddr_props, outprm, tavorprm_query_ddr_st, ddr_end_adr);

    EX_BOOL_FLD(ddr_props, outprm, tavorprm_query_ddr_st, dh);
    EX_FLD(ddr_props, outprm, tavorprm_query_ddr_st, di);
    EX_FLD(ddr_props, outprm, tavorprm_query_ddr_st, ap);

    /* TBD handle dimm structs here */
    EX_FLD(ddr_props, outprm, tavorprm_query_ddr_st, dimm0.dimmsize);
    EX_FLD(ddr_props, outprm, tavorprm_query_ddr_st, dimm0.dimmstatus);
    EX_FLD64(ddr_props, outprm, tavorprm_query_ddr_st, dimm0.vendor_id);

    EX_FLD(ddr_props, outprm, tavorprm_query_ddr_st, dimm1.dimmsize);
    EX_FLD(ddr_props, outprm, tavorprm_query_ddr_st, dimm1.dimmstatus);
    EX_FLD64(ddr_props, outprm, tavorprm_query_ddr_st, dimm1.vendor_id);

    EX_FLD(ddr_props, outprm, tavorprm_query_ddr_st, dimm2.dimmsize);
    EX_FLD(ddr_props, outprm, tavorprm_query_ddr_st, dimm2.dimmstatus);
    EX_FLD64(ddr_props, outprm, tavorprm_query_ddr_st, dimm2.vendor_id);

    EX_FLD(ddr_props, outprm, tavorprm_query_ddr_st, dimm3.dimmsize);
    EX_FLD(ddr_props, outprm, tavorprm_query_ddr_st, dimm3.dimmstatus);
    EX_FLD64(ddr_props, outprm, tavorprm_query_ddr_st, dimm3.vendor_id);

    THH_CMD_PRINT_QUERY_DDR(ddr_props);
  }
  FREE(outprm);
  MT_RETURN(rc);
}

/*
 *  THH_cmd_QUERY_ADAPTER
 */ 
THH_cmd_status_t THH_cmd_QUERY_ADAPTER(THH_cmd_t cmd_if, THH_adapter_props_t *adapter_props)
{
  command_fields_t cmd_desc;
  u_int8_t *outprm;
  THH_cmd_status_t rc;
  u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_query_adapter_st);

  FUNC_IN;
  outprm = TNMALLOC(u_int8_t, buf_size);
  if (outprm == NULL) {
    MT_RETURN(THH_CMD_STAT_EAGAIN);
  }
  memset(outprm, 0, buf_size);
  
  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = 0;
  cmd_desc.out_param = outprm;
  cmd_desc.out_param_size = buf_size;
  cmd_desc.out_trans = TRANS_MAILBOX;
  cmd_desc.opcode = TAVOR_IF_CMD_QUERY_ADAPTER;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_QUERY_ADAPTER;
  
  rc = cmd_invoke(cmd_if, &cmd_desc);
  if ( rc != THH_CMD_STAT_OK ) {
    FREE(outprm);
    MT_RETURN(rc);
  }

  if ( adapter_props ) {
    EX_FLD(adapter_props, outprm, tavorprm_query_adapter_st, vendor_id);
    EX_FLD(adapter_props, outprm, tavorprm_query_adapter_st, device_id);
    EX_FLD(adapter_props, outprm, tavorprm_query_adapter_st, revision_id);
    EX_FLD(adapter_props, outprm, tavorprm_query_adapter_st, intapin);

    THH_CMD_PRINT_QUERY_ADAPTER(adapter_props);
  }
  FREE(outprm);
  MT_RETURN(rc);
}

/*
 *  THH_cmd_INIT_HCA
 */ 

THH_cmd_status_t THH_cmd_INIT_HCA(THH_cmd_t cmd_if, THH_hca_props_t *hca_props)
{
  command_fields_t cmd_desc;
  u_int8_t *inprm;
  THH_cmd_status_t rc;
  u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_init_hca_st);
  u_int64_t     rdb_base_addr_save;

  FUNC_IN;
  if (hca_props == NULL) {MT_RETURN(THH_CMD_STAT_EBADARG); }
  inprm = TNMALLOC(u_int8_t, buf_size);
  if (inprm == NULL) {
    MT_RETURN(THH_CMD_STAT_EAGAIN);
  }
  memset(inprm, 0, buf_size);
  THH_CMD_PRINT_HCA_PROPS(hca_props);

  cmd_desc.in_param = inprm;
  cmd_desc.in_param_size = buf_size;
  cmd_desc.in_trans = TRANS_MAILBOX;
  cmd_desc.input_modifier = 0;
  cmd_desc.out_param = 0;
  cmd_desc.out_param_size = 0;
  cmd_desc.out_trans = TRANS_NA;
  cmd_desc.opcode = TAVOR_IF_CMD_INIT_HCA;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = hca_props->qpc_eec_cqc_eqc_rdb_parameters.log_num_of_qp > 18 ?
                             2*(TAVOR_IF_CMD_ETIME_INIT_HCA) : TAVOR_IF_CMD_ETIME_INIT_HCA;

  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, router_qp);
  INS_BOOL_FLD(hca_props, inprm, tavorprm_init_hca_st, re);
  INS_BOOL_FLD(hca_props, inprm, tavorprm_init_hca_st, udp);
  INS_BOOL_FLD(hca_props, inprm, tavorprm_init_hca_st, he);
  INS_BOOL_FLD(hca_props, inprm, tavorprm_init_hca_st, ud);

  INS_FLD64_SH(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.qpc_base_addr);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.log_num_of_qp);
  INS_FLD64_SH(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.srqc_base_addr);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.log_num_of_srq);
  INS_FLD64_SH(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.eec_base_addr);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.log_num_of_ee);
  INS_FLD64_SH(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.cqc_base_addr);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.log_num_of_cq);
  INS_FLD64(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.eqpc_base_addr);
  INS_FLD64(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.eeec_base_addr);
  INS_FLD64_SH(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.eqc_base_addr);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.log_num_eq);

  /* zero out low order 32 bits of rdb base addr for passing to Tavor */
  rdb_base_addr_save = hca_props->qpc_eec_cqc_eqc_rdb_parameters.rdb_base_addr;
  hca_props->qpc_eec_cqc_eqc_rdb_parameters.rdb_base_addr &= MAKE_ULONGLONG(0xFFFFFFFF00000000);
  INS_FLD64(hca_props, inprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.rdb_base_addr);
  /*restore original RDB base address */
  hca_props->qpc_eec_cqc_eqc_rdb_parameters.rdb_base_addr =rdb_base_addr_save;

  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, udavtable_memory_parameters.l_key);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, udavtable_memory_parameters.pd);
  INS_BOOL_FLD(hca_props, inprm, tavorprm_init_hca_st, udavtable_memory_parameters.xlation_en);
  
  INS_FLD64(hca_props, inprm, tavorprm_init_hca_st, multicast_parameters.mc_base_addr);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, multicast_parameters.log_mc_table_entry_sz);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, multicast_parameters.log_mc_table_sz);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, multicast_parameters.mc_table_hash_sz);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, multicast_parameters.mc_hash_fn);
  
  INS_FLD64(hca_props, inprm, tavorprm_init_hca_st, tpt_parameters.mpt_base_adr);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, tpt_parameters.log_mpt_sz);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, tpt_parameters.pfto);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, tpt_parameters.mtt_segment_size);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, tpt_parameters.mtt_version);
  INS_FLD64(hca_props, inprm, tavorprm_init_hca_st, tpt_parameters.mtt_base_addr);
  
  INS_FLD64(hca_props, inprm, tavorprm_init_hca_st, uar_parameters.uar_base_addr);
  INS_FLD(hca_props, inprm, tavorprm_init_hca_st, uar_parameters.uar_page_sz);
  INS_FLD64(hca_props, inprm, tavorprm_init_hca_st, uar_parameters.uar_scratch_base_addr);
  THH_CMD_MAILBOX_PRINT(inprm, buf_size, __func__);
  
  rc = cmd_invoke(cmd_if, &cmd_desc);
  FREE(inprm);
  MT_RETURN(rc);
}


/*
 *  THH_cmd_CLOSE_HCA
 */ 
#ifdef SIMULATE_HALT_HCA
THH_cmd_status_t THH_cmd_CLOSE_HCA(THH_cmd_t cmd_if)
#else
THH_cmd_status_t THH_cmd_CLOSE_HCA(THH_cmd_t cmd_if, MT_bool do_halt)
#endif
{
  command_fields_t cmd_desc;
  THH_cmd_status_t rc;

  FUNC_IN;
  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = 0;
  cmd_desc.out_param = 0;
  cmd_desc.out_param_size = 0;
  cmd_desc.out_trans = TRANS_NA;
  cmd_desc.opcode = TAVOR_IF_CMD_CLOSE_HCA;
#ifdef SIMULATE_HALT_HCA
  cmd_desc.opcode_modifier = 0;
#else
  cmd_desc.opcode_modifier = (do_halt == FALSE ? 0 : 1);
#endif
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_CLOSE_HCA;

  rc = cmd_invoke(cmd_if, &cmd_desc);
  MT_RETURN(rc);
}


/*
 *  THH_cmd_INIT_IB
 */ 
THH_cmd_status_t THH_cmd_INIT_IB(THH_cmd_t cmd_if, IB_port_t port,
                                 THH_port_init_props_t *port_init_props)
{
  command_fields_t cmd_desc;
  u_int8_t *inprm;
  THH_cmd_status_t rc;
  u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_init_ib_st);
  u_int32_t temp_u32;

  FUNC_IN;
  if (port_init_props == NULL) {MT_RETURN(THH_CMD_STAT_EBADARG); }
  
  inprm = TNMALLOC(u_int8_t, buf_size);
  if (inprm == NULL) {
    MT_RETURN(THH_CMD_STAT_EAGAIN);
  }
  memset(inprm, 0, buf_size);
  THH_CMD_PRINT_INIT_IB(port, port_init_props);
  cmd_desc.in_param = inprm;
  cmd_desc.in_param_size = buf_size;
  cmd_desc.in_trans = TRANS_MAILBOX;
  cmd_desc.input_modifier = port;
  cmd_desc.out_param = 0;
  cmd_desc.out_param_size = 0;
  cmd_desc.out_trans = TRANS_NA;
  cmd_desc.opcode = TAVOR_IF_CMD_INIT_IB;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_INIT_IB;

  INS_FLD(port_init_props, inprm, tavorprm_init_ib_st, vl_cap);
  INS_FLD(port_init_props, inprm, tavorprm_init_ib_st, port_width_cap);
  INS_FLD(port_init_props, inprm, tavorprm_init_ib_st, mtu_cap);
  INS_FLD(port_init_props, inprm, tavorprm_init_ib_st, max_gid);
  INS_FLD(port_init_props, inprm, tavorprm_init_ib_st, max_pkey);

  INS_BOOL_FLD(port_init_props, inprm, tavorprm_init_ib_st, g0);

  /* We get GUID0 in BIG_ENDIAN format.  It needs to be split up into Host-endian format before passing to cmd_invoke */
  /* Note that need to memcpy each 4 bytes to a temporary u_int32_t variable, since there is no guarantee */
  /* that the GUID is 4-byte aligned (it is an array of unsigned chars) */
  memcpy(&temp_u32, &(port_init_props->guid0[0]), sizeof(u_int32_t));
  MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                 MT_BIT_OFFSET(tavorprm_init_ib_st, guid0_h), MT_BIT_SIZE(tavorprm_init_ib_st, guid0_h));

  memcpy(&temp_u32, &(port_init_props->guid0[4]), sizeof(u_int32_t));
  MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                 MT_BIT_OFFSET(tavorprm_init_ib_st, guid0_l), MT_BIT_SIZE(tavorprm_init_ib_st, guid0_l));

  rc = cmd_invoke(cmd_if, &cmd_desc);
  FREE(inprm);
  MT_RETURN(rc);
}

/*
 *  THH_cmd_CLOSE_IB
 */ 
THH_cmd_status_t THH_cmd_SYS_DIS(THH_cmd_t cmd_if)
{
  command_fields_t cmd_desc;
  THH_cmd_status_t rc;

  FUNC_IN;
  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = 0;
  cmd_desc.out_param = 0;
  cmd_desc.out_param_size = 0;
  cmd_desc.out_trans = TRANS_NA;
  cmd_desc.opcode = TAVOR_IF_CMD_SYS_DIS;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_SYS_DIS;

  rc = cmd_invoke(cmd_if, &cmd_desc);
  MT_RETURN(rc);
}



/*
 *  THH_cmd_CLOSE_IB
 */ 
THH_cmd_status_t THH_cmd_CLOSE_IB(THH_cmd_t cmd_if, IB_port_t port)
{
  command_fields_t cmd_desc;
  THH_cmd_status_t rc;

  FUNC_IN;
  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = port;
  cmd_desc.out_param = 0;
  cmd_desc.out_param_size = 0;
  cmd_desc.out_trans = TRANS_NA;
  cmd_desc.opcode = TAVOR_IF_CMD_CLOSE_IB;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_CLOSE_IB;

  rc = cmd_invoke(cmd_if, &cmd_desc);
  MT_RETURN(rc);
}

/*
 *  THH_cmd_QUERY_HCA
 */ 

THH_cmd_status_t THH_cmd_QUERY_HCA(THH_cmd_t cmd_if, THH_hca_props_t *hca_props)
{
  command_fields_t cmd_desc;
  u_int8_t *outprm;
  THH_cmd_status_t rc;
  u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_init_hca_st);

  FUNC_IN;
  outprm = TNMALLOC(u_int8_t, buf_size);
  if (outprm == NULL) {
    MT_RETURN(THH_CMD_STAT_EAGAIN);
  }
  memset(outprm, 0, buf_size);

  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = 0;
  cmd_desc.out_param = outprm;
  cmd_desc.out_param_size = buf_size;
  cmd_desc.out_trans = TRANS_MAILBOX;
  cmd_desc.opcode = TAVOR_IF_CMD_QUERY_HCA;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_QUERY_HCA;
  
  rc = cmd_invoke(cmd_if, &cmd_desc);
  if ( rc != THH_CMD_STAT_OK ) {
    FREE(outprm);
    MT_RETURN(rc);
  }

  if ( hca_props ) {
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, hca_core_clock);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, router_qp);
    EX_BOOL_FLD(hca_props, outprm, tavorprm_init_hca_st, re);
    EX_BOOL_FLD(hca_props, outprm, tavorprm_init_hca_st, udp);
    EX_BOOL_FLD(hca_props, outprm, tavorprm_init_hca_st, he);
    EX_BOOL_FLD(hca_props, outprm, tavorprm_init_hca_st, ud);

    EX_FLD64_SH(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.qpc_base_addr);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.log_num_of_qp);
    EX_FLD64_SH(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.srqc_base_addr);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.log_num_of_srq);
    EX_FLD64_SH(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.eec_base_addr);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.log_num_of_ee);
    EX_FLD64_SH(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.cqc_base_addr);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.log_num_of_cq);
    EX_FLD64(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.eqpc_base_addr);
    EX_FLD64(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.eeec_base_addr);
    EX_FLD64_SH(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.eqc_base_addr);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.log_num_eq);
    EX_FLD64(hca_props, outprm, tavorprm_init_hca_st, qpc_eec_cqc_eqc_rdb_parameters.rdb_base_addr);

    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, udavtable_memory_parameters.l_key);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, udavtable_memory_parameters.pd);
    EX_BOOL_FLD(hca_props, outprm, tavorprm_init_hca_st, udavtable_memory_parameters.xlation_en);

    EX_FLD64(hca_props, outprm, tavorprm_init_hca_st, multicast_parameters.mc_base_addr);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, multicast_parameters.log_mc_table_entry_sz);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, multicast_parameters.log_mc_table_sz);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, multicast_parameters.mc_table_hash_sz);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, multicast_parameters.mc_hash_fn);

    EX_FLD64(hca_props, outprm, tavorprm_init_hca_st, tpt_parameters.mpt_base_adr);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, tpt_parameters.log_mpt_sz);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, tpt_parameters.pfto);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, tpt_parameters.mtt_segment_size);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, tpt_parameters.mtt_version);
    EX_FLD64(hca_props, outprm, tavorprm_init_hca_st, tpt_parameters.mtt_base_addr);

    EX_FLD64(hca_props, outprm, tavorprm_init_hca_st, uar_parameters.uar_base_addr);
    EX_FLD(hca_props, outprm, tavorprm_init_hca_st, uar_parameters.uar_page_sz);
    EX_FLD64(hca_props, outprm, tavorprm_init_hca_st, uar_parameters.uar_scratch_base_addr);
    THH_CMD_MAILBOX_PRINT(outprm, buf_size, __func__);

    THH_CMD_PRINT_HCA_PROPS(hca_props);
  }
      
  FREE(outprm);
  MT_RETURN(rc);
}


/*
 *  THH_cmd_SET_IB
 */ 
THH_cmd_status_t THH_cmd_SET_IB(THH_cmd_t cmd_if, IB_port_t port,
                                 THH_set_ib_props_t *port_init_props)
{
  command_fields_t cmd_desc;
  u_int8_t inprm[PSEUDO_MT_BYTE_SIZE(tavorprm_set_ib_st)];
  THH_cmd_status_t rc;
  u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_set_ib_st);

  FUNC_IN;
  if (port_init_props == NULL) {MT_RETURN(THH_CMD_STAT_EBADARG);}
  memset(inprm, 0, buf_size);

  cmd_desc.in_param = inprm;
  cmd_desc.in_param_size = buf_size;
  cmd_desc.in_trans = TRANS_MAILBOX;
  cmd_desc.input_modifier = port;
  cmd_desc.out_param = 0;
  cmd_desc.out_param_size = 0;
  cmd_desc.out_trans = TRANS_NA;
  cmd_desc.opcode = TAVOR_IF_CMD_SET_IB;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_SET_IB;

  INS_BOOL_FLD(port_init_props, inprm, tavorprm_set_ib_st, rqk);
  INS_FLD(port_init_props, inprm, tavorprm_set_ib_st, capability_mask);
  
  rc = cmd_invoke(cmd_if, &cmd_desc);
  MT_RETURN(rc);
}

/*
 *  THH_cmd_SW2HW_MPT
 */ 
THH_cmd_status_t THH_cmd_SW2HW_MPT(THH_cmd_t cmd_if, THH_mpt_index_t mpt_index,
                                   THH_mpt_entry_t *mpt_entry)
{
    command_fields_t cmd_desc;
    u_int8_t inprm[PSEUDO_MT_BYTE_SIZE(tavorprm_mpt_st)];
    THH_cmd_status_t rc;
    MT_size_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_mpt_st);

    FUNC_IN;
    MTL_DEBUG4("THH_cmd_SW2HW_MPT:  mpt_index = "SIZE_T_FMT", buf_size = "SIZE_T_FMT"\n", mpt_index, buf_size); 
    if (mpt_entry == NULL) {MT_RETURN(THH_CMD_STAT_EBADARG); }
    THH_CMD_PRINT_MPT_ENTRY(mpt_entry);

    memset(inprm, 0, buf_size);
    
    cmd_desc.in_param = inprm;
    cmd_desc.in_param_size = (u_int32_t)buf_size;
    cmd_desc.in_trans = TRANS_MAILBOX;
    cmd_desc.input_modifier = (u_int32_t)mpt_index;
    cmd_desc.out_param = 0;
    cmd_desc.out_param_size = 0;
    cmd_desc.out_trans = TRANS_NA;
    cmd_desc.opcode = TAVOR_IF_CMD_SW2HW_MPT;
    cmd_desc.opcode_modifier = 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_SW2HW_MPT;

    INS_FLD(mpt_entry, inprm, tavorprm_mpt_st, ver);
    
    INS_BOOL_FLD(mpt_entry, inprm, tavorprm_mpt_st, r_w);
    INS_BOOL_FLD(mpt_entry, inprm, tavorprm_mpt_st, pa);
    INS_BOOL_FLD(mpt_entry, inprm, tavorprm_mpt_st, lr);
    INS_BOOL_FLD(mpt_entry, inprm, tavorprm_mpt_st, lw);
    INS_BOOL_FLD(mpt_entry, inprm, tavorprm_mpt_st, rr);
    INS_BOOL_FLD(mpt_entry, inprm, tavorprm_mpt_st, rw);
    INS_BOOL_FLD(mpt_entry, inprm, tavorprm_mpt_st, a);
    INS_BOOL_FLD(mpt_entry, inprm, tavorprm_mpt_st, eb);
    INS_BOOL_FLD(mpt_entry, inprm, tavorprm_mpt_st, m_io);
    
    INS_FLD(mpt_entry, inprm, tavorprm_mpt_st, status);
    INS_FLD(mpt_entry, inprm, tavorprm_mpt_st, page_size);
    INS_FLD(mpt_entry, inprm, tavorprm_mpt_st, mem_key);
    INS_FLD(mpt_entry, inprm, tavorprm_mpt_st, pd);
    INS_FLD64(mpt_entry, inprm, tavorprm_mpt_st, start_address);
    INS_FLD64(mpt_entry, inprm, tavorprm_mpt_st, reg_wnd_len);
    INS_FLD(mpt_entry, inprm, tavorprm_mpt_st, lkey);
    INS_FLD(mpt_entry, inprm, tavorprm_mpt_st, win_cnt);
    INS_FLD(mpt_entry, inprm, tavorprm_mpt_st, win_cnt_limit);
    INS_FLD64_SH(mpt_entry, inprm, tavorprm_mpt_st, mtt_seg_adr);

    THH_CMD_MAILBOX_PRINT(inprm, (int)buf_size, __func__);
#if 1
    rc = cmd_invoke(cmd_if, &cmd_desc);
#else
    MTL_DEBUG4("THH_cmd_SW2HW_MPT:  SKIPPING cmd_invoke !!!!!!!!!\n");
    rc = THH_CMD_STAT_OK;
#endif
    MT_RETURN(rc);
}

/*
 *  THH_cmd_QUERY_MPT
 */ 
THH_cmd_status_t THH_cmd_QUERY_MPT(THH_cmd_t cmd_if, THH_mpt_index_t mpt_index,
                                   THH_mpt_entry_t *mpt_entry)
{
    command_fields_t cmd_desc;
    u_int8_t outprm[PSEUDO_MT_BYTE_SIZE(tavorprm_mpt_st)];
    THH_cmd_status_t rc;
    u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_mpt_st);

    FUNC_IN;
    memset(outprm, 0, buf_size);

    cmd_desc.in_param = 0;
    cmd_desc.in_param_size = 0;
    cmd_desc.in_trans = TRANS_NA;
    cmd_desc.input_modifier = (u_int32_t)mpt_index;
    cmd_desc.out_param = outprm;
    cmd_desc.out_param_size = buf_size;
    cmd_desc.out_trans = TRANS_MAILBOX;
    cmd_desc.opcode = TAVOR_IF_CMD_QUERY_MPT;
    cmd_desc.opcode_modifier = 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_QUERY_MPT;

    rc = cmd_invoke(cmd_if, &cmd_desc);
    if ( rc != THH_CMD_STAT_OK ) {
      MT_RETURN(rc);
    }
    
    if ( mpt_entry ) {
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, ver);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, r_w);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, pa);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, lr);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, lw);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, rr);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, rw);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, a);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, eb);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, m_io);

      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, status);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, page_size);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, mem_key);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, pd);
      EX_FLD64(mpt_entry, outprm, tavorprm_mpt_st, start_address);
      EX_FLD64(mpt_entry, outprm, tavorprm_mpt_st, reg_wnd_len);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, lkey);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, win_cnt);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, win_cnt_limit);
      EX_FLD64_SH(mpt_entry, outprm, tavorprm_mpt_st, mtt_seg_adr);
    }

    MT_RETURN(rc);
}

/*
 *  THH_cmd_HW2SW_MPT
 */ 
THH_cmd_status_t THH_cmd_HW2SW_MPT(THH_cmd_t cmd_if, THH_mpt_index_t mpt_index,
                                   THH_mpt_entry_t *mpt_entry)
{
    command_fields_t cmd_desc;
    u_int8_t outprm[PSEUDO_MT_BYTE_SIZE(tavorprm_mpt_st)];
    THH_cmd_status_t rc;
    u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_mpt_st);

    FUNC_IN;
    memset(outprm, 0, buf_size);

    cmd_desc.in_param = 0;
    cmd_desc.in_param_size = 0;
    cmd_desc.in_trans = TRANS_NA;
    cmd_desc.input_modifier = (u_int32_t)mpt_index;
    cmd_desc.out_param = outprm;
    cmd_desc.out_param_size = buf_size;
    cmd_desc.out_trans = TRANS_MAILBOX;
    cmd_desc.opcode = TAVOR_IF_CMD_HW2SW_MPT;
     /* when output is not necessary putting 1 in opcode modifier
        will case the command execute faster */
    cmd_desc.opcode_modifier = mpt_entry ? 0 : 1;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_HW2SW_MPT;

    rc = cmd_invoke(cmd_if, &cmd_desc);
    if ( rc != THH_CMD_STAT_OK ) {
      MT_RETURN(rc);
    }
    
    if ( mpt_entry ) {
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, ver);

      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, r_w);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, pa);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, lr);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, lw);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, rr);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, rw);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, a);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, eb);
      EX_BOOL_FLD(mpt_entry, outprm, tavorprm_mpt_st, m_io);
      
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, status);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, page_size);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, mem_key);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, pd);
      EX_FLD64(mpt_entry, outprm, tavorprm_mpt_st, start_address);
      EX_FLD64(mpt_entry, outprm, tavorprm_mpt_st, reg_wnd_len);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, lkey);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, win_cnt);
      EX_FLD(mpt_entry, outprm, tavorprm_mpt_st, win_cnt_limit);
      EX_FLD64_SH(mpt_entry, outprm, tavorprm_mpt_st, mtt_seg_adr);
    }

    MT_RETURN(rc);
}

/*
 *  THH_cmd_READ_MTT
 */ 
THH_cmd_status_t THH_cmd_READ_MTT(THH_cmd_t cmd_if, u_int64_t mtt_pa, MT_size_t num_elems,
                                   THH_mtt_entry_t *mtt_entry)
{
    command_fields_t cmd_desc;
    u_int8_t *outprm, *iterator;
    int i, local_num_elts, elts_this_loop, max_elts_per_buffer;
    u_int32_t buf_size;
    u_int32_t     mtt_pa_transfer[2];
    THH_cmd_status_t rc = THH_CMD_STAT_OK;
    MT_bool   buf_align_adjust = TRUE;
    
    /* TBD:  need to:  a. limit number of entries in the mailbox */
    /*                 b. for performance, if the initial addr is odd, */
    /*                    do a loop of a single element, then do the rest */


    FUNC_IN;
    if (!num_elems) {
        MT_RETURN(THH_CMD_STAT_BAD_PARAM);
    }

    outprm = TNMALLOC(u_int8_t, MAX_OUT_PRM_SIZE);
    if ( !outprm ) {
      MT_RETURN(THH_CMD_STAT_EAGAIN);
    }
    memset(outprm, 0, MAX_OUT_PRM_SIZE);

    local_num_elts = (int)num_elems;
    max_elts_per_buffer = (MAX_OUT_PRM_SIZE - MTT_CMD_PA_PREAMBLE_SIZE) / ( PSEUDO_MT_BYTE_SIZE(tavorprm_mtt_st));

    while(local_num_elts > 0) {

        elts_this_loop = (local_num_elts > max_elts_per_buffer ? max_elts_per_buffer : local_num_elts);

        /* if the mtt_pa address is odd (3 LSBs ignored), and we need to use multiple commands */
        /* and we are also reading an odd number of elements, then decrease the elements in this loop */
        /* by one so that on the next go-around, the reading will start at an even mtt_pa address */
        /* If necessary, the adjustment needs to be performed only once */
        if ((buf_align_adjust) && (local_num_elts > max_elts_per_buffer) && ((mtt_pa>>3)& 0x1) && (!(elts_this_loop & 0x1))) {
            elts_this_loop--;
            buf_align_adjust = FALSE;
        }

        buf_size = elts_this_loop*PSEUDO_MT_BYTE_SIZE(tavorprm_mtt_st);
        memset(outprm, 0, buf_size);
    
        iterator = outprm;

        /* The command interface expects the mtt_pa format to be the HIGH order word first, */
        /* then the low-order word.  The command interface adjusts endianness within words */

        mtt_pa_transfer[0]= (u_int32_t)(mtt_pa >> 32);               /* MS-DWORD */
        mtt_pa_transfer[1]= (u_int32_t)(mtt_pa & 0xFFFFFFFF);        /* LS-DWORD */  
    
        cmd_desc.in_param = (u_int8_t *)&(mtt_pa_transfer[0]);
        cmd_desc.in_param_size = sizeof(mtt_pa_transfer);
        cmd_desc.in_trans = TRANS_IMMEDIATE;
        cmd_desc.input_modifier = elts_this_loop;
        cmd_desc.out_param = outprm;
        cmd_desc.out_param_size = buf_size;
        cmd_desc.out_trans = TRANS_MAILBOX;
        cmd_desc.opcode = TAVOR_IF_CMD_READ_MTT;
        cmd_desc.opcode_modifier = 0;
        cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_READ_MTT;
    
        rc = cmd_invoke(cmd_if, &cmd_desc);
        if ( rc != THH_CMD_STAT_OK ) {
          FREE(outprm);
          MT_RETURN(rc);
        }
        
        if ( mtt_entry ) {
          for (i = 0; i < elts_this_loop; i++, mtt_entry++, iterator += PSEUDO_MT_BYTE_SIZE(tavorprm_mtt_st) ) {
              EX_FLD64_SH(mtt_entry, iterator, tavorprm_mtt_st, ptag);
              EX_BOOL_FLD(mtt_entry, iterator, tavorprm_mtt_st, p);
          }
        }
    
        if (rc != THH_CMD_STAT_OK) {
            FREE(outprm);
            MT_RETURN(rc);
        }
        /* update loop parameters */
        mtt_pa += elts_this_loop * PSEUDO_MT_BYTE_SIZE(tavorprm_mtt_st);    /* incr target pointer */
        local_num_elts -= elts_this_loop;
    }
    FREE(outprm);
    MT_RETURN(rc);
}

/*
 *  THH_cmd_WRITE_MTT
 */ 
THH_cmd_status_t THH_cmd_WRITE_MTT(THH_cmd_t cmd_if, u_int64_t mtt_pa, MT_size_t num_elems,
                                   THH_mtt_entry_t *mtt_entry)
{
#if 1
    command_fields_t cmd_desc;
    u_int8_t *inprm, *iterator;
    int i, local_num_elts, elts_this_loop, max_elts_per_buffer;
    u_int32_t buf_size;
    THH_cmd_status_t rc = THH_CMD_STAT_OK;
    MT_bool   buf_align_adjust = TRUE;

    /* TBD:  need to:  a. limit number of entries in the mailbox */
    /*                 b. for performance, if the initial addr is odd, */
    /*                    do one loop of an odd number of elements, then do the rest */

    FUNC_IN;
    if ( !STACK_OK ) MT_RETURN(THH_CMD_STAT_EFATAL);
    if (!num_elems) {
        MT_RETURN(THH_CMD_STAT_BAD_PARAM);
    }

    inprm = (u_int8_t*)MALLOC(MAX_IN_PRM_SIZE);
    if ( !inprm ) {
      MT_RETURN(THH_CMD_STAT_EAGAIN);
    }
    local_num_elts = (u_int32_t)num_elems;
    max_elts_per_buffer = (MAX_IN_PRM_SIZE - MTT_CMD_PA_PREAMBLE_SIZE) / ( PSEUDO_MT_BYTE_SIZE(tavorprm_mtt_st));

    MTL_DEBUG4("THH_cmd_WRITE_MTT: local_num_elts = %d, max_elts_per_buffer = %d\n",local_num_elts, max_elts_per_buffer);
    while(local_num_elts > 0) {

        elts_this_loop = (local_num_elts > max_elts_per_buffer ? max_elts_per_buffer : local_num_elts);

        /* if the mtt_pa address is odd (3 LSBs ignored), and we need to use multiple commands */
        /* and we are also writing an odd number of elements, then decrease the elements in this loop */
        /* by one so that on the next go-around, the writing will start at an even mtt_pa address */
        /* If necessary, the adjustment needs to be performed only once */
        if ((buf_align_adjust) && (local_num_elts > max_elts_per_buffer) && ((mtt_pa>>3)& 0x1) && (!(elts_this_loop & 0x1))) {
            elts_this_loop--;
            buf_align_adjust = FALSE;
        }
        if (elts_this_loop <= 0) {
            break;
        }

        buf_size = elts_this_loop*PSEUDO_MT_BYTE_SIZE(tavorprm_mtt_st) + MTT_CMD_PA_PREAMBLE_SIZE;
        memset(inprm, 0, buf_size);
        iterator = inprm;
        
        MTL_DEBUG4("THH_cmd_WRITE_MTT: elts_this_loop = %d, buf_size = %d, buf_align_adjust = %d\n",elts_this_loop, buf_size, buf_align_adjust);
    
        cmd_desc.in_param = inprm;
        cmd_desc.in_param_size = buf_size;
        cmd_desc.in_trans = TRANS_MAILBOX;
        cmd_desc.input_modifier = elts_this_loop;
        cmd_desc.out_param = 0;
        cmd_desc.out_param_size = 0;
        cmd_desc.out_trans = TRANS_NA;
        cmd_desc.opcode = TAVOR_IF_CMD_WRITE_MTT;
        cmd_desc.opcode_modifier = 0;
        cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_WRITE_MTT;
    
        /* copy  */
        ((u_int32_t*)iterator)[0]= (u_int32_t)(mtt_pa >> 32);               /* MS-DWORD */
        ((u_int32_t*)iterator)[1]= (u_int32_t)(mtt_pa & 0xFFFFFFFF);        /* LS-DWORD */  

        iterator += MTT_CMD_PA_PREAMBLE_SIZE;
    
        for (i = 0; i < elts_this_loop; i++, mtt_entry++, iterator += PSEUDO_MT_BYTE_SIZE(tavorprm_mtt_st) ) {
            INS_FLD64_SH(mtt_entry, iterator, tavorprm_mtt_st, ptag);
            INS_BOOL_FLD(mtt_entry, iterator, tavorprm_mtt_st, p);
        }
        

        THH_CMD_MAILBOX_PRINT(inprm, buf_size, __func__);
        THH_CMD_PRINT_MTT_ENTRIES(elts_this_loop, inprm);
#if 1
        rc = cmd_invoke(cmd_if, &cmd_desc);
        if (rc != THH_CMD_STAT_OK) {
            FREE(inprm);
            MT_RETURN(rc);
        }
#else
        MTL_DEBUG4("THH_cmd_WRITE_MTT: SKIPPING cmd_invoke\n");
        rc = THH_CMD_STAT_INTERNAL_ERR;
#endif
        /* update loop parameters */
        mtt_pa += elts_this_loop * PSEUDO_MT_BYTE_SIZE(tavorprm_mtt_st);    /* incr target pointer */
        local_num_elts -= elts_this_loop;
    }
    FREE(inprm);
    MT_RETURN(rc);
#else
  FREE(inprm);
  MT_RETURN(THH_CMD_STAT_INTERNAL_ERR);
#endif 
}


THH_cmd_status_t THH_cmd_SYNC_TPT(THH_cmd_t cmd_if)
{
  command_fields_t cmd_prms = {0};
  THH_cmd_status_t rc;

  FUNC_IN;
  cmd_prms.opcode = TAVOR_IF_CMD_SYNC_TPT;
  cmd_prms.in_trans = TRANS_NA;
  cmd_prms.out_trans = TRANS_NA;
  cmd_prms.exec_time_micro = TAVOR_IF_CMD_ETIME_SYNC_TPT;
  rc = cmd_invoke(cmd_if, &cmd_prms);
  if ( rc != THH_CMD_STAT_OK ) {
    MTL_ERROR1(MT_FLFMT("THH_cmd_SYNC_TPT failed: %s\n"), str_THH_cmd_status_t(rc));
  }
  MT_RETURN(rc);
}


/*
 *  THH_cmd_MAP_EQ
 */ 
THH_cmd_status_t THH_cmd_MAP_EQ(THH_cmd_t cmd_if, THH_eqn_t eqn, u_int64_t event_mask)
{
  command_fields_t cmd_desc;
  THH_cmd_status_t rc;
  u_int32_t        event_mask_transfer[2];

  FUNC_IN;


  /* The command interface expects the event_mask format to be the HIGH order word first, */
  /* then the low-order word.  The command interface adjusts endianness within words */

  event_mask_transfer[0]= (u_int32_t)(event_mask >> 32);               /* MS-DWORD */
  event_mask_transfer[1]= (u_int32_t)(event_mask & 0xFFFFFFFF);        /* LS-DWORD */  

  MTL_DEBUG4("THH_cmd_MAP_EQ: eqn = 0x%x, event_mask = "U64_FMT"\n", eqn, event_mask);
  MTL_DEBUG4("THH_cmd_MAP_EQ: event_mask_transfer [0] = 0x%x, event_mask_transfer [1] = 0x%x\n",
             event_mask_transfer[0], event_mask_transfer[1]);

  cmd_desc.in_param = (u_int8_t *)&(event_mask_transfer[0]);
  cmd_desc.in_param_size = sizeof(event_mask_transfer);
  cmd_desc.in_trans = TRANS_IMMEDIATE;
  cmd_desc.input_modifier = eqn;
  cmd_desc.out_param = 0;
  cmd_desc.out_param_size = 0;
  cmd_desc.out_trans = TRANS_NA;
  cmd_desc.opcode = TAVOR_IF_CMD_MAP_EQ;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_MAP_EQ;

  rc = cmd_invoke(cmd_if, &cmd_desc);
  MT_RETURN(rc);
}

/*
 *  THH_cmd_SW2HW_EQ
 */ 
THH_cmd_status_t THH_cmd_SW2HW_EQ(THH_cmd_t cmd_if, THH_eqn_t eqn, THH_eqc_t *eq_context)
{
    command_fields_t cmd_desc;
    u_int8_t inprm[PSEUDO_MT_BYTE_SIZE(tavorprm_eqc_st)];
    THH_cmd_status_t rc;
    u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_eqc_st);

    FUNC_IN;
    if (eq_context == NULL) {MT_RETURN(THH_CMD_STAT_EBADARG); }
    memset(inprm, 0, buf_size);

    THH_CMD_PRINT_EQ_CONTEXT(eq_context);

    cmd_desc.in_param = inprm;
    cmd_desc.in_param_size = buf_size;
    cmd_desc.in_trans = TRANS_MAILBOX;
    cmd_desc.input_modifier = eqn;
    cmd_desc.out_param = 0;
    cmd_desc.out_param_size = 0;
    cmd_desc.out_trans = TRANS_NA;
    cmd_desc.opcode = TAVOR_IF_CMD_SW2HW_EQ;
    cmd_desc.opcode_modifier = 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_SW2HW_EQ;

    INS_FLD(eq_context, inprm, tavorprm_eqc_st, st);

    INS_BOOL_FLD(eq_context, inprm, tavorprm_eqc_st, oi);
    INS_BOOL_FLD(eq_context, inprm, tavorprm_eqc_st, tr);

    INS_FLD(eq_context, inprm, tavorprm_eqc_st, owner);
    INS_FLD(eq_context, inprm, tavorprm_eqc_st, status);
    INS_FLD64(eq_context, inprm, tavorprm_eqc_st, start_address);
    INS_FLD(eq_context, inprm, tavorprm_eqc_st, usr_page);
    INS_FLD(eq_context, inprm, tavorprm_eqc_st, log_eq_size);
    INS_FLD(eq_context, inprm, tavorprm_eqc_st, pd);
    INS_FLD(eq_context, inprm, tavorprm_eqc_st, intr);
    INS_FLD(eq_context, inprm, tavorprm_eqc_st, lost_count);
    INS_FLD(eq_context, inprm, tavorprm_eqc_st, lkey);
    INS_FLD(eq_context, inprm, tavorprm_eqc_st, consumer_indx);
    INS_FLD(eq_context, inprm, tavorprm_eqc_st, producer_indx);
    THH_CMD_MAILBOX_PRINT(inprm, buf_size, __func__);

    rc = cmd_invoke(cmd_if, &cmd_desc);
    MT_RETURN(rc);
}

/*
 *  THH_cmd_HW2SW_EQ
 */ 
THH_cmd_status_t THH_cmd_HW2SW_EQ(THH_cmd_t cmd_if, THH_eqn_t eqn, THH_eqc_t *eq_context)
{
    command_fields_t cmd_desc;
    u_int8_t outprm[PSEUDO_MT_BYTE_SIZE(tavorprm_mpt_st)];
    THH_cmd_status_t rc;
    u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_mpt_st);

    FUNC_IN;
    memset(outprm, 0, buf_size);

    cmd_desc.in_param = 0;
    cmd_desc.in_param_size = 0;
    cmd_desc.in_trans = TRANS_NA;
    cmd_desc.input_modifier = eqn;
    cmd_desc.out_param = outprm;
    cmd_desc.out_param_size = buf_size;
    cmd_desc.out_trans = TRANS_MAILBOX;
    cmd_desc.opcode = TAVOR_IF_CMD_HW2SW_EQ;
    cmd_desc.opcode_modifier = 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_HW2SW_EQ;

    rc = cmd_invoke(cmd_if, &cmd_desc);
    if ( rc != THH_CMD_STAT_OK ) {
      MT_RETURN(rc);
    }
    
    if ( eq_context ) {
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, st);
      EX_BOOL_FLD(eq_context, outprm, tavorprm_eqc_st, oi);
      EX_BOOL_FLD(eq_context, outprm, tavorprm_eqc_st, tr);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, owner);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, status);
      EX_FLD64(eq_context, outprm, tavorprm_eqc_st, start_address);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, usr_page);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, log_eq_size);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, pd);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, intr);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, lost_count);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, lkey);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, consumer_indx);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, producer_indx);
    }

    MT_RETURN(rc);
}

/*
 *  THH_cmd_QUERY_EQ
 */ 
THH_cmd_status_t THH_cmd_QUERY_EQ(THH_cmd_t cmd_if, THH_eqn_t eqn, THH_eqc_t *eq_context)
{
    command_fields_t cmd_desc;
    u_int8_t outprm[PSEUDO_MT_BYTE_SIZE(tavorprm_eqc_st)];
    THH_cmd_status_t rc;
    u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_eqc_st);

    FUNC_IN;
    memset(outprm, 0, buf_size);

    cmd_desc.in_param = 0;
    cmd_desc.in_param_size = 0;
    cmd_desc.in_trans = TRANS_NA;
    cmd_desc.input_modifier = eqn;
    cmd_desc.out_param = outprm;
    cmd_desc.out_param_size = buf_size;
    cmd_desc.out_trans = TRANS_MAILBOX;
    cmd_desc.opcode = TAVOR_IF_CMD_HW2SW_EQ;
    cmd_desc.opcode_modifier = 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_HW2SW_EQ;

    rc = cmd_invoke(cmd_if, &cmd_desc);
    if ( rc != THH_CMD_STAT_OK ) {
      MT_RETURN(rc);
    }
    
    if ( eq_context ) {
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, st);

      EX_BOOL_FLD(eq_context, outprm, tavorprm_eqc_st, oi);
      EX_BOOL_FLD(eq_context, outprm, tavorprm_eqc_st, tr);
      
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, owner);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, status);
      EX_FLD64(eq_context, outprm, tavorprm_eqc_st, start_address);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, usr_page);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, log_eq_size);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, pd);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, intr);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, lost_count);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, lkey);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, consumer_indx);
      EX_FLD(eq_context, outprm, tavorprm_eqc_st, producer_indx);
    }

    MT_RETURN(rc);
}

/*
 *  THH_cmd_SW2HW_EQ
 */ 
THH_cmd_status_t THH_cmd_SW2HW_CQ(THH_cmd_t cmd_if, HH_cq_hndl_t cqn, THH_cqc_t *cq_context)
{
    command_fields_t cmd_desc;
    u_int8_t inprm[PSEUDO_MT_BYTE_SIZE(tavorprm_completion_queue_context_st)];
    THH_cmd_status_t rc;
    u_int32_t  buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_completion_queue_context_st);

    FUNC_IN;
    if (cq_context == NULL) {MT_RETURN(THH_CMD_STAT_EBADARG); }
    memset(inprm, 0, buf_size);

    THH_CMD_PRINT_CQ_CONTEXT(cq_context);

    cmd_desc.in_param = inprm;
    cmd_desc.in_param_size = buf_size;
    cmd_desc.in_trans = TRANS_MAILBOX;
    cmd_desc.input_modifier = cqn;
    cmd_desc.out_param = 0;
    cmd_desc.out_param_size = 0;
    cmd_desc.out_trans = TRANS_NA;
    cmd_desc.opcode = TAVOR_IF_CMD_SW2HW_CQ;
    cmd_desc.opcode_modifier = 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_SW2HW_CQ;

    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, st);

    INS_BOOL_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, oi);
    INS_BOOL_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, tr);
    
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, status);
    INS_FLD64(cq_context, inprm, tavorprm_completion_queue_context_st, start_address);
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, usr_page);
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, log_cq_size);
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, e_eqn);
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, c_eqn);
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, pd);
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, l_key);
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, last_notified_indx);
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, solicit_producer_indx);
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, consumer_indx);
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, producer_indx);
    INS_FLD(cq_context, inprm, tavorprm_completion_queue_context_st, cqn);
    THH_CMD_MAILBOX_PRINT(inprm, buf_size, __func__);
#if 1
    rc = cmd_invoke(cmd_if, &cmd_desc);
#else
    MTL_DEBUG4("THH_cmd_SW2HW_CG:  SKIPPING cmd_invoke !!!!!!!!!\n");
    rc = THH_CMD_STAT_OK;
#endif
    MT_RETURN(rc);
}

/*
 *  THH_cmd_HW2SW_EQ
 */ 
THH_cmd_status_t THH_cmd_HW2SW_CQ(THH_cmd_t cmd_if,  HH_cq_hndl_t cqn, THH_cqc_t *cq_context)
{
    command_fields_t cmd_desc;
    u_int8_t outprm[PSEUDO_MT_BYTE_SIZE(tavorprm_completion_queue_context_st)];
    THH_cmd_status_t rc;
    u_int32_t  buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_completion_queue_context_st);

    FUNC_IN;
    MTL_DEBUG4("THH_cmd_HW2SW_CQ:  cqn = 0x%x, cq_context = 0x%p\n", cqn, cq_context);
    memset(outprm, 0, buf_size);

    cmd_desc.in_param = 0;
    cmd_desc.in_param_size = 0;
    cmd_desc.in_trans = TRANS_NA;
    cmd_desc.input_modifier = cqn;
    cmd_desc.out_param = outprm;
    cmd_desc.out_param_size = buf_size;
    cmd_desc.out_trans = TRANS_MAILBOX;
    cmd_desc.opcode = TAVOR_IF_CMD_HW2SW_CQ;
    cmd_desc.opcode_modifier = 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_HW2SW_CQ;

    rc = cmd_invoke(cmd_if, &cmd_desc);
    if ( rc != THH_CMD_STAT_OK ) {
      MT_RETURN(rc);
    }
    

    if ( cq_context ) {
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, st);
      
      EX_BOOL_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, oi);
      EX_BOOL_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, tr);
      
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, status);
      EX_FLD64(cq_context, outprm, tavorprm_completion_queue_context_st, start_address);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, usr_page);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, log_cq_size);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, e_eqn);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, c_eqn);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, pd);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, l_key);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, last_notified_indx);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, solicit_producer_indx);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, consumer_indx);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, producer_indx);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, cqn);
      THH_CMD_PRINT_CQ_CONTEXT(cq_context);
    }
    MT_RETURN(rc);
}

/*
 *  THH_cmd_QUERY_CQ
 */ 
THH_cmd_status_t THH_cmd_QUERY_CQ(THH_cmd_t cmd_if, HH_cq_hndl_t cqn, THH_cqc_t *cq_context)
{
    command_fields_t cmd_desc;
    u_int8_t outprm[PSEUDO_MT_BYTE_SIZE(tavorprm_completion_queue_context_st)];
    THH_cmd_status_t rc;
    u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_completion_queue_context_st);

    FUNC_IN;
    memset(outprm, 0, buf_size);

    cmd_desc.in_param = 0;
    cmd_desc.in_param_size = 0;
    cmd_desc.in_trans = TRANS_NA;
    cmd_desc.input_modifier = cqn;
    cmd_desc.out_param = outprm;
    cmd_desc.out_param_size = buf_size;
    cmd_desc.out_trans = TRANS_MAILBOX;
    cmd_desc.opcode = TAVOR_IF_CMD_QUERY_CQ;
    cmd_desc.opcode_modifier = 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_QUERY_CQ;

    rc = cmd_invoke(cmd_if, &cmd_desc);
    if ( rc != THH_CMD_STAT_OK ) {
      MT_RETURN(rc);
    }
    
    if ( cq_context ) {
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, st);
      
      EX_BOOL_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, oi);
      EX_BOOL_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, tr);
      
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, status);
      EX_FLD64(cq_context, outprm, tavorprm_completion_queue_context_st, start_address);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, usr_page);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, log_cq_size);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, e_eqn);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, c_eqn);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, pd);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, l_key);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, last_notified_indx);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, solicit_producer_indx);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, consumer_indx);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, producer_indx);
      EX_FLD(cq_context, outprm, tavorprm_completion_queue_context_st, cqn);
      THH_CMD_PRINT_CQ_CONTEXT(cq_context);
    }

    MT_RETURN(rc);
}

/*
 *  THH_cmd_RESIZE_CQ
 */ 
THH_cmd_status_t THH_cmd_RESIZE_CQ(THH_cmd_t cmd_if, HH_cq_hndl_t cqn, 
                                   u_int64_t start_address, u_int32_t l_key, u_int8_t log_cq_size,
                                   u_int32_t *new_producer_index_p)
{
  command_fields_t cmd_desc;
  u_int32_t inprm[PSEUDO_MT_BYTE_SIZE(tavorprm_resize_cq_st)];
  THH_cmd_status_t rc;
  const u_int32_t  buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_resize_cq_st);
  u_int32_t out_param_tmp[2];

  FUNC_IN;
  memset(inprm, 0, buf_size);

  cmd_desc.in_param = (u_int8_t*)inprm;
  cmd_desc.in_param_size = buf_size;
  cmd_desc.in_trans = TRANS_MAILBOX;
  cmd_desc.input_modifier = cqn;
  cmd_desc.out_trans = TRANS_IMMEDIATE;
  cmd_desc.out_param = (u_int8_t*)out_param_tmp;
  cmd_desc.opcode = TAVOR_IF_CMD_RESIZE_CQ;
  cmd_desc.opcode_modifier = new_producer_index_p ? 0 /* legacy mode */: 
                                                    1 /* fixed resize: new_pi= old_pi % new_size */;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_RESIZE_CQ;

  memset(inprm,0,buf_size);
  inprm[MT_BYTE_OFFSET(tavorprm_resize_cq_st, start_addr_h) >> 2]= (u_int32_t)(start_address >> 32);
  inprm[MT_BYTE_OFFSET(tavorprm_resize_cq_st, start_addr_l) >> 2]= (u_int32_t)(start_address & 0xFFFFFFFF);
  inprm[MT_BYTE_OFFSET(tavorprm_resize_cq_st, l_key) >> 2]= l_key;
  MT_INSERT_ARRAY32(inprm, log_cq_size , 
                 MT_BIT_OFFSET(tavorprm_resize_cq_st, log_cq_size), MT_BIT_SIZE(tavorprm_resize_cq_st, log_cq_size)) ;
  //MTL_ERROR1(MT_FLFMT("RESIZE_CQ: mailbox[0-3]= 0x%X 0x%X 0x%X 0x%X"),inprm[0],inprm[1],inprm[2],inprm[3]);
  THH_CMD_MAILBOX_PRINT(inprm, buf_size, __func__);
  rc = cmd_invoke(cmd_if, &cmd_desc);
  if (new_producer_index_p) *new_producer_index_p= out_param_tmp[0]; /* new producer index is in out_param_h */
  MT_RETURN(rc);
}


/*
 *  GET_QP_CMD_EXEC_TIME -- returns QP command exec time in microseconds
 */ 
static u_int32_t get_qp_cmd_exec_time(THH_qpee_transition_t trans)
{
    switch(trans) {
    case QPEE_TRANS_RST2INIT:
        return TAVOR_IF_CMD_ETIME_RST2INIT_QPEE;
    case QPEE_TRANS_INIT2INIT:
        return TAVOR_IF_CMD_ETIME_INIT2INIT_QPEE;
    case QPEE_TRANS_INIT2RTR:
        return TAVOR_IF_CMD_ETIME_INIT2RTR_QPEE;
    case QPEE_TRANS_RTR2RTS:
        return TAVOR_IF_CMD_ETIME_RTR2RTS_QPEE;
    case QPEE_TRANS_RTS2RTS:
        return TAVOR_IF_CMD_ETIME_RTS2RTS_QPEE;
    case QPEE_TRANS_SQERR2RTS:
        return TAVOR_IF_CMD_ETIME_SQERR2RTS_QPEE;
    case QPEE_TRANS_SQD2RTS:
        return TAVOR_IF_CMD_ETIME_SQD2RTS_QPEE;
    case QPEE_TRANS_2ERR:
        return TAVOR_IF_CMD_ETIME_2ERR_QPEE;
    case QPEE_TRANS_RTS2SQD:
        return TAVOR_IF_CMD_ETIME_RTS2SQD;
    case QPEE_TRANS_ERR2RST:
        return TAVOR_IF_CMD_ETIME_ERR2RST_QPEE;
    default:
        MTL_ERROR1(MT_FLFMT("no such qp transition exists \n")); 
        return 0;
    }
}
/*
 *  THH_cmd_MODIFY_QPEE 
 *                   is_ee:  0 = QP, 1 = EE
 */ 
static THH_cmd_status_t THH_cmd_MODIFY_QPEE( THH_cmd_t cmd_if, MT_bool is_ee, u_int32_t qpn, THH_qpee_transition_t trans,
                                   THH_qpee_context_t *qp_context,u_int32_t  optparammask)
{
    command_fields_t cmd_desc;
    u_int8_t *inprm = NULL;
    THH_cmd_status_t rc;
    u_int32_t buf_size, sqd_event_req;
    u_int32_t  temp_u32;
    tavor_if_qp_state_t tavor_if_qp_state;

    FUNC_IN;
    
	/* we save the value of sqd_event bit in xaction field & clr it to receive a normal xaction value. */
	sqd_event_req 	= trans & THH_CMD_SQD_EVENT_REQ;
	trans			&= ~THH_CMD_SQD_EVENT_REQ; 
	
	CMDS_DBG("%s: TRANSACTION val = 0x%x\n", __func__, trans);

    /* see which transition was requested */
    switch(trans) {
    /* have input mailbox only */
	case QPEE_TRANS_RST2INIT:
	case QPEE_TRANS_INIT2INIT:
    case QPEE_TRANS_INIT2RTR:
    case QPEE_TRANS_RTR2RTS:
    case QPEE_TRANS_RTS2RTS:
    case QPEE_TRANS_SQERR2RTS:
    case QPEE_TRANS_SQD2RTS:
        if (qp_context == NULL) {MT_RETURN(THH_CMD_STAT_EBADARG); }
    
        inprm = (u_int8_t *)MALLOC(PSEUDO_MT_BYTE_SIZE(tavorprm_qp_ee_state_transitions_st));
        if (inprm == NULL) {
           MT_RETURN(THH_CMD_STAT_EAGAIN);
        }
        
        buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_qp_ee_state_transitions_st);
        memset(inprm, 0, buf_size);

        cmd_desc.in_param = inprm;
        cmd_desc.in_param_size = buf_size;
        cmd_desc.in_trans = TRANS_MAILBOX;
        cmd_desc.input_modifier = qpn | (is_ee ? 0x1000000 : 0);
        cmd_desc.out_param = 0;
        cmd_desc.out_param_size = 0;
        cmd_desc.out_trans = TRANS_NA;
        cmd_desc.opcode = (tavor_if_cmd_t)trans;
        cmd_desc.opcode_modifier = 0;
        cmd_desc.exec_time_micro = get_qp_cmd_exec_time(trans);

        THH_CMD_PRINT_QP_CONTEXT(qp_context);

        /* translate VAPI qp state to Tavor qp state, and fail if not valid*/
        if (!THH_vapi_qpstate_2_tavor_qpstate(qp_context->state, &tavor_if_qp_state)) {
            CMDS_DBG("%s: VAPI QP state (0x%x) is not valid\n", __func__, qp_context->state);
            rc = THH_CMD_STAT_EFATAL;
            goto retn;
        }

        MT_INSERT_ARRAY32(inprm, optparammask, MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, opt_param_mask), 
                       MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, opt_param_mask));

		// 'te' field has been removed.
        //QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, te);
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, de);

        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st,pm_state);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, st);

        MT_INSERT_ARRAY32(inprm, tavor_if_qp_state, MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.state), 
                       MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.state));
        
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, sched_queue);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, msg_max);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, mtu);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, usr_page);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, local_qpn_een);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, remote_qpn_een);

        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.pkey_index);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.port_number);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.rlid);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.my_lid_path_bits);
        
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.g);
        
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.rnr_retry);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.hop_limit);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.max_stat_rate);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.mgid_index);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.ack_timeout);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.flow_label);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.tclass);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.sl);

        memcpy(&temp_u32, &(qp_context->primary_address_path.rgid[0]), sizeof(u_int32_t));
        MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                       MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_127_96),
                       MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_127_96));
        
        memcpy(&temp_u32, &(qp_context->primary_address_path.rgid[4]), sizeof(u_int32_t));
        MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                       MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_95_64),
                       MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_95_64));

        memcpy(&temp_u32, &(qp_context->primary_address_path.rgid[8]), sizeof(u_int32_t));
        MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                       MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_63_32),
                       MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_63_32));

        memcpy(&temp_u32, &(qp_context->primary_address_path.rgid[12]), sizeof(u_int32_t));
        MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                       MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_31_0),
                       MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_31_0));

        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.pkey_index);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.port_number);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.rlid);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.my_lid_path_bits);
        
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.g);

        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.rnr_retry);

        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.hop_limit);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.max_stat_rate);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.mgid_index);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.ack_timeout);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.flow_label);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.tclass);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.sl);

        memcpy(&temp_u32, &(qp_context->alternative_address_path.rgid[0]), sizeof(u_int32_t));
        MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                       MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_127_96),
                       MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_127_96));
        
        memcpy(&temp_u32, &(qp_context->alternative_address_path.rgid[4]), sizeof(u_int32_t));
        MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                       MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_95_64),
                       MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_95_64));

        memcpy(&temp_u32, &(qp_context->alternative_address_path.rgid[8]), sizeof(u_int32_t));
        MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                       MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_63_32),
                       MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_63_32));

        memcpy(&temp_u32, &(qp_context->alternative_address_path.rgid[12]), sizeof(u_int32_t));
        MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                       MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_31_0),
                       MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_31_0));

        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, rdd);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, pd);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, wqe_base_adr);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, wqe_lkey);
        
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, ssc);
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, sic);
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, sae);
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, swe);
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, sre);
        
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, retry_count);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, sra_max);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, flight_lim);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, ack_req_freq);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, next_send_psn);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, cqn_snd);
        QP_INS_FLD64(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, next_snd_wqe);
        
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, rsc);
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, ric);
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, rae);
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, rwe);
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, rre);
        
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, rra_max);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, next_rcv_psn);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, min_rnr_nak);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, ra_buff_indx);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, cqn_rcv);
        QP_INS_FLD64(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, next_rcv_wqe);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, q_key);
        QP_INS_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, srqn);
        
        QP_INS_BOOL_FLD(qp_context, inprm, tavorprm_qp_ee_state_transitions_st, srq);

        rc = cmd_invoke(cmd_if, &cmd_desc);
        goto retn;

        /* No mailboxes, and no immed data */
    case QPEE_TRANS_2ERR:
    case QPEE_TRANS_RTS2SQD:
        cmd_desc.in_param = 0;
        cmd_desc.in_param_size = 0;
        cmd_desc.in_trans = TRANS_NA;
        cmd_desc.input_modifier = qpn | ( (sqd_event_req && (trans == QPEE_TRANS_RTS2SQD)) ? TAVOR_IF_SQD_EVENT_FLAG:0 );
        cmd_desc.input_modifier |= (is_ee ? 0x1000000 : 0);
        cmd_desc.out_param = 0;
        cmd_desc.out_param_size = 0;
        cmd_desc.out_trans = TRANS_NA;
        cmd_desc.opcode = (tavor_if_cmd_t)trans;
        cmd_desc.opcode_modifier = 0;
        cmd_desc.exec_time_micro = get_qp_cmd_exec_time(trans);

        rc = cmd_invoke(cmd_if, &cmd_desc);
        goto retn;

        /* output mailbox only */
    /* matan: this is now the ANY2RST xition. */
	case QPEE_TRANS_ERR2RST:

        cmd_desc.in_param = 0;
        cmd_desc.in_param_size = 0;
        cmd_desc.in_trans = TRANS_NA;
        cmd_desc.input_modifier = qpn | (is_ee ? 0x1000000 : 0);
        cmd_desc.out_param = 0;        /* not using outbox */
        cmd_desc.out_param_size = 0;   /* not using outbox */
        cmd_desc.out_trans = TRANS_MAILBOX;
        cmd_desc.opcode = (tavor_if_cmd_t)trans;
        /* matan: ANY2RST is always called with (opcode_modifier |= 2), meaning no need 
		       to move into ERR before RST. Also, set LSB so that no outbox will be generated */
		    cmd_desc.opcode_modifier = 3 ;  /* bits 0 and 1 set */
        cmd_desc.exec_time_micro = get_qp_cmd_exec_time(trans);

        rc = cmd_invoke(cmd_if, &cmd_desc);
        goto retn;
	default:
		MTL_ERROR1("%s: BAD TRANSACTION val = 0x%x\n", __func__, trans);
        CMDS_DBG("%s: BAD TRANSACTION val = 0x%x\n", __func__, trans);
        MT_RETURN(THH_CMD_STAT_EBADARG);
    }
    MT_RETURN(THH_CMD_STAT_EFATAL);  // ??? BAD_ARG ??

retn:
   if (inprm != NULL) {
       FREE(inprm);
   }
   MT_RETURN(rc);
}

/*
 *  THH_cmd_MODIFY_QP 
 */ 
THH_cmd_status_t THH_cmd_MODIFY_QP(THH_cmd_t cmd_if, IB_wqpn_t qpn,
                                   THH_qpee_transition_t trans,
                                   THH_qpee_context_t *qp_context,
                                   u_int32_t           optparammask)
{
  CMDS_DBG("%s: TRANSACTION val = 0x%x\n", __func__, trans);
  return (THH_cmd_MODIFY_QPEE(cmd_if,0,qpn,trans,qp_context,optparammask));
}

/*
 *  THH_cmd_MODIFY_EE 
 */ 
THH_cmd_status_t THH_cmd_MODIFY_EE(THH_cmd_t cmd_if, IB_eecn_t eecn,
                                   THH_qpee_transition_t trans,
                                   THH_qpee_context_t *ee_context,
                                   u_int32_t           optparammask)
{
  return (THH_cmd_MODIFY_QPEE(cmd_if,1,eecn,trans,ee_context,optparammask));
}


/*
 *  THH_cmd_QUERY_QPEE 
 */ 
static THH_cmd_status_t THH_cmd_QUERY_QPEE( THH_cmd_t cmd_if,  MT_bool is_ee, u_int32_t qpn,
                                   THH_qpee_context_t *qp_context)
{
    command_fields_t cmd_desc;
    u_int8_t *outprm;
    THH_cmd_status_t rc;
    u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_qp_ee_state_transitions_st);
    u_int32_t temp_u32;
    tavor_if_qp_state_t tavor_if_qp_state;
    	
    FUNC_IN;
    outprm = TNMALLOC(u_int8_t, buf_size);
    if ( !outprm ) {
      MT_RETURN(THH_CMD_STAT_EAGAIN);
    }
    memset(outprm, 0, buf_size);

    cmd_desc.in_param = 0;
    cmd_desc.in_param_size = 0;
    cmd_desc.in_trans = TRANS_NA;
    cmd_desc.input_modifier = qpn;
    cmd_desc.out_param = outprm;
    cmd_desc.out_param_size = buf_size;
    cmd_desc.out_trans = TRANS_MAILBOX;
    cmd_desc.opcode = TAVOR_IF_CMD_QUERY_QPEE;
    cmd_desc.opcode_modifier = is_ee ? 1 : 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_QUERY_QPEE;

    rc = cmd_invoke(cmd_if, &cmd_desc);
    if ( rc != THH_CMD_STAT_OK ) {
      FREE(outprm);
      MT_RETURN(rc);
    }

    if ( qp_context ) {
	  // 'te' field has been removed.
      //QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, te);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, de);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, pm_state);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, st);

      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, sched_queue);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, msg_max);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, mtu);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, usr_page);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, local_qpn_een);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, remote_qpn_een);

      tavor_if_qp_state = (tavor_if_qp_state_t) 
                    MT_EXTRACT_ARRAY32(outprm,  MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.state), 
                                      MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.state));
      if(!THH_tavor_qpstate_2_vapi_qpstate(tavor_if_qp_state, &(qp_context->state))){
          CMDS_DBG("%s: TAVOR QP state (0x%x) is not valid\n", __func__, tavor_if_qp_state);
          FREE(outprm);
          return THH_CMD_STAT_EFATAL;
      }
	  
	  /*T.D.(matan): change along with the rest of SQ_DRAINING improvements.*/
	  qp_context->sq_draining = (qp_context->state & THH_CMD_QP_DRAINING_FLAG) ? TRUE:FALSE;
	  qp_context->state = (VAPI_qp_state_t) (qp_context->state & ~(THH_CMD_QP_DRAINING_FLAG));

      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.pkey_index);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.port_number);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.rlid);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.my_lid_path_bits);

      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.g);

      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.rnr_retry);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.hop_limit);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.max_stat_rate);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.mgid_index);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.ack_timeout);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.flow_label);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.tclass);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, primary_address_path.sl);

      /* extract RGID.  Note that get the RGID from the command object as 4 double-words, each in CPU-endianness. */
      /* Need to take them one at a time, and convert each to big-endian before storing in the output RGID array */
      /* Note that need to memcpy each 4 bytes to a temporary u_int32_t variable, since there is no guarantee */
      /* that the RGID is 4-byte aligned (it is an array of unsigned chars) */
      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_127_96),
                     MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_127_96)));
      memcpy(&(qp_context->primary_address_path.rgid[0]), &temp_u32, sizeof(u_int32_t));

      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_95_64),
                     MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_95_64)));
      memcpy(&(qp_context->primary_address_path.rgid[4]), &temp_u32, sizeof(u_int32_t));

      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_63_32),
                     MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_63_32)));
      memcpy(&(qp_context->primary_address_path.rgid[8]), &temp_u32, sizeof(u_int32_t));

      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_31_0),
                     MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.primary_address_path.rgid_31_0)));
      memcpy(&(qp_context->primary_address_path.rgid[12]), &temp_u32, sizeof(u_int32_t));

      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.pkey_index);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.port_number);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.rlid);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.my_lid_path_bits);
      
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.g);
      
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.rnr_retry);

      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.hop_limit);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.max_stat_rate);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.mgid_index);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.ack_timeout);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.flow_label);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.tclass);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, alternative_address_path.sl);

      /* extract RGID.  Note that get the RGID from the command object as 4 double-words, each in CPU-endianness. */
      /* Need to take them one at a time, and convert each to big-endian before storing in the output RGID array */
      /* Note that need to memcpy each 4 bytes to a temporary u_int32_t variable, since there is no guarantee */
      /* that the RGID is 4-byte aligned (it is an array of unsigned chars) */
      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_127_96),
                     MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_127_96)));
      memcpy(&(qp_context->alternative_address_path.rgid[0]), &temp_u32, sizeof(u_int32_t));

      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_95_64),
                     MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_95_64)));
      memcpy(&(qp_context->alternative_address_path.rgid[4]), &temp_u32, sizeof(u_int32_t));

      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_63_32),
                     MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_63_32)));
      memcpy(&(qp_context->alternative_address_path.rgid[8]), &temp_u32, sizeof(u_int32_t));

      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_31_0),
                     MT_BIT_SIZE(tavorprm_qp_ee_state_transitions_st, qpc_eec_data.alternative_address_path.rgid_31_0)));
      memcpy(&(qp_context->alternative_address_path.rgid[12]), &temp_u32, sizeof(u_int32_t));

      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, rdd);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, pd);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, wqe_base_adr);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, wqe_lkey);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, ssc);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, sic);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, sae);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, swe);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, sre);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, retry_count);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, sra_max);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, flight_lim);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, ack_req_freq);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, next_send_psn);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, cqn_snd);
      QP_EX_FLD64(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, next_snd_wqe);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, rsc);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, ric);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, rae);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, rwe);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, rre);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, rra_max);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, next_rcv_psn);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, min_rnr_nak);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, ra_buff_indx);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, cqn_rcv);
      QP_EX_FLD64(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, next_rcv_wqe);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, q_key);
      QP_EX_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, srqn);
      QP_EX_BOOL_FLD(qp_context, outprm, tavorprm_qp_ee_state_transitions_st, srq);
      /* THH_CMD_PRINT_QP_CONTEXT(qp_context); */
    }

    FREE(outprm);
    MT_RETURN(rc);
}

/*
 *  THH_cmd_QUERY_QP 
 */ 
THH_cmd_status_t THH_cmd_QUERY_QP(THH_cmd_t cmd_if, IB_wqpn_t qpn,
                                  THH_qpee_context_t *qp_context)
{
    return (THH_cmd_QUERY_QPEE(cmd_if,0,qpn,qp_context));
}
/*
 *  THH_cmd_QUERY_EE 
 */ 
THH_cmd_status_t THH_cmd_QUERY_EE(THH_cmd_t cmd_if, IB_eecn_t eecn,
                                  THH_qpee_context_t *ee_context)
{
    return (THH_cmd_QUERY_QPEE(cmd_if,1,eecn,ee_context));
}

/*
 *  THH_cmd_CONF_SPECIAL_QP 
 */ 
THH_cmd_status_t THH_cmd_CONF_SPECIAL_QP(THH_cmd_t cmd_if, VAPI_special_qp_t qp_type,
                                         IB_wqpn_t base_qpn)
{
  command_fields_t cmd_desc;
  u_int8_t         op_modifier;
  THH_cmd_status_t rc;

  FUNC_IN;
  MTL_DEBUG4("%s: ENTERING \n", __func__);
  switch(qp_type) {
  case VAPI_SMI_QP:
      op_modifier = 0;
      break;
  case VAPI_GSI_QP:
      op_modifier = 1;
      break;
  case VAPI_RAW_IPV6_QP:
      op_modifier = 2;
      break;
  case VAPI_RAW_ETY_QP:
      op_modifier = 3;
      break;
  default:
      MT_RETURN (THH_CMD_STAT_EBADARG);
  }
  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = base_qpn;
  cmd_desc.out_param = 0;
  cmd_desc.out_param_size = 0;
  cmd_desc.out_trans = TRANS_NA;
  cmd_desc.opcode = TAVOR_IF_CMD_CONF_SPECIAL_QP;
  cmd_desc.opcode_modifier = op_modifier;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_CONF_SPECIAL_QP;

  rc = cmd_invoke(cmd_if, &cmd_desc);
  MT_RETURN(rc);
}

/*
 *  THH_cmd_MAD_IFC 
 */ 
THH_cmd_status_t THH_cmd_MAD_IFC(THH_cmd_t cmd_if, 
                                 MT_bool mkey_validate, 
                                 IB_lid_t slid, /* SLID is ignored if mkey_validate is false */
                                 IB_port_t port,
                                 void *mad_in, 
                                 void *mad_out)
{
    struct cmd_if_context_st *cmdif_p = (struct cmd_if_context_st *)cmd_if;
    command_fields_t cmd_desc;
    THH_cmd_status_t rc;
    u_int32_t       i, *int32_inbuf, *int32_outbuf, *orig_inbuf = NULL, *int32_temp_inbuf = NULL;
    
    /* support NULL mad_out */
    static u_int32_t dummy_mad_out[256/sizeof(u_int32_t)]; /* STATIC ! (not on stack) */
    

    FUNC_IN;
    if (mad_in == NULL)  return THH_CMD_STAT_EBADARG;
    int32_temp_inbuf = (u_int32_t*)MALLOC(256);
    if (int32_temp_inbuf == NULL) {
        MT_RETURN(THH_CMD_STAT_EAGAIN);
    }
    int32_inbuf = (u_int32_t *) mad_in;
    int32_outbuf = (u_int32_t *) (mad_out == NULL ? dummy_mad_out : mad_out);
    orig_inbuf = int32_temp_inbuf;

    cmd_desc.in_param = (u_int8_t *) int32_temp_inbuf;
    cmd_desc.in_param_size = 256;
    cmd_desc.in_trans = TRANS_MAILBOX;
    cmd_desc.input_modifier = (port & 3);
    /* For Mkey validation the MAD's source LID is required (upper 16 bits of input mod.) */
    if ((mkey_validate) &&
        (THH_FW_VER_VALUE(cmdif_p->fw_props.fw_rev_major,
                          cmdif_p->fw_props.fw_rev_minor,
                          cmdif_p->fw_props.fw_rev_subminor) > THH_FW_VER_VALUE(1,0x17,0) ) )  {
        /* SLID for MAD_IFC is supported only after 1.17.0000 */
      cmd_desc.input_modifier |= ( ((u_int32_t)slid) << 16 );
    }
    cmd_desc.out_param = (u_int8_t*)int32_outbuf;
    cmd_desc.out_param_size = 256;
    cmd_desc.out_trans = TRANS_MAILBOX;
    cmd_desc.opcode = TAVOR_IF_CMD_MAD_IFC;
    cmd_desc.opcode_modifier = mkey_validate ? 0 : 1;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_MAD_IFC;
    
    /* reverse endianness to CPU endian, since MAD frames are all BE*/
    for (i = 0; i < 256/(sizeof(u_int32_t)); i++) {
              *int32_temp_inbuf  = MOSAL_be32_to_cpu(*int32_inbuf);
              int32_inbuf++;
              int32_temp_inbuf++;
    }
    rc = cmd_invoke(cmd_if, &cmd_desc);
    /* reverse endianness to big endian, since information is gotten cpu-endian*/
    for (i = 0; i < 256/(sizeof(u_int32_t)); i++) {
              *int32_outbuf  = MOSAL_cpu_to_be32(*int32_outbuf);
              int32_outbuf++;
    }
    FREE(orig_inbuf);
    THH_CMD_MAILBOX_PRINT(mad_out, 256, __func__);
    MT_RETURN(rc);
}


THH_cmd_status_t THH_cmd_SW2HW_SRQ(THH_cmd_t cmd_if,
                                   u_int32_t srqn,         /* SRQ number/index */
                                   THH_srq_context_t *srqc_p) /* SRQ context      */
{
  command_fields_t cmd_desc;
  u_int8_t inprm[PSEUDO_MT_BYTE_SIZE(tavorprm_srq_context_st)];
  THH_cmd_status_t rc;
  const u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_srq_context_st);

  FUNC_IN;
  if (srqc_p == NULL) {MT_RETURN(THH_CMD_STAT_EBADARG); }
  memset(inprm, 0, buf_size);
  INS_FLD(srqc_p, inprm, tavorprm_srq_context_st, pd);
  INS_FLD(srqc_p, inprm, tavorprm_srq_context_st, uar);
  INS_FLD(srqc_p, inprm, tavorprm_srq_context_st, l_key);
  INS_FLD(srqc_p, inprm, tavorprm_srq_context_st, wqe_addr_h);
  INS_FLD(srqc_p, inprm, tavorprm_srq_context_st, next_wqe_addr_l);
  INS_FLD(srqc_p, inprm, tavorprm_srq_context_st, ds);

  cmd_desc.in_param = inprm;
  cmd_desc.in_param_size = buf_size;
  cmd_desc.in_trans = TRANS_MAILBOX;
  cmd_desc.input_modifier = srqn;
  cmd_desc.out_param = 0;
  cmd_desc.out_param_size = 0;
  cmd_desc.out_trans = TRANS_NA;
  cmd_desc.opcode = TAVOR_IF_CMD_SW2HW_SRQ;
  cmd_desc.opcode_modifier = 0;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_CLASS_C;

  rc = cmd_invoke(cmd_if, &cmd_desc);
  MT_RETURN(rc);
}

THH_cmd_status_t THH_cmd_HW2SW_SRQ(THH_cmd_t cmd_if,
                                   u_int32_t srqn,          /* SRQ number/index */
                                   THH_srq_context_t *srqc_p) /* SRQ context      */
{
  command_fields_t cmd_desc;
  u_int8_t outprm[PSEUDO_MT_BYTE_SIZE(tavorprm_srq_context_st)];
  THH_cmd_status_t rc;
  const u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_srq_context_st);

  FUNC_IN;
  memset(outprm, 0, buf_size);

  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = srqn;
  cmd_desc.out_param = srqc_p != NULL ? outprm : 0;
  cmd_desc.out_param_size = srqc_p != NULL ? buf_size : 0;
  cmd_desc.out_trans = TRANS_MAILBOX;
  cmd_desc.opcode = TAVOR_IF_CMD_HW2SW_SRQ;
  cmd_desc.opcode_modifier = srqc_p != NULL ? 1 : 0; /* No need for output if no *srqc_p */
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_CLASS_C;

  rc = cmd_invoke(cmd_if, &cmd_desc);
  
  if (srqc_p != NULL) {
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, pd);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, uar);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, l_key);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, wqe_addr_h);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, next_wqe_addr_l);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, ds);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, wqe_cnt);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, state);
  }
  MT_RETURN(rc);
}

THH_cmd_status_t THH_cmd_QUERY_SRQ(THH_cmd_t cmd_if,
                                   u_int32_t srqn,          /* SRQ number/index */
                                   THH_srq_context_t *srqc_p) /* SRQ context      */
{
  command_fields_t cmd_desc;
  u_int8_t outprm[PSEUDO_MT_BYTE_SIZE(tavorprm_srq_context_st)];
  THH_cmd_status_t rc;
  const u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_srq_context_st);

  FUNC_IN;
  if (srqc_p == NULL) {MT_RETURN(THH_CMD_STAT_EBADARG); }
  memset(outprm, 0, buf_size);

  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = srqn;
  cmd_desc.out_param = outprm;
  cmd_desc.out_param_size = buf_size;
  cmd_desc.out_trans = TRANS_MAILBOX;
  cmd_desc.opcode = TAVOR_IF_CMD_QUERY_SRQ;
  cmd_desc.opcode_modifier = 0; 
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_CLASS_C;

  rc = cmd_invoke(cmd_if, &cmd_desc);
  
  if (srqc_p != NULL){
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, pd);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, uar);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, l_key);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, wqe_addr_h);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, next_wqe_addr_l);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, ds);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, wqe_cnt);
    EX_FLD(srqc_p, outprm, tavorprm_srq_context_st, state);
  }
  MT_RETURN(rc);
}


/*
 *  THH_cmd_READ_MGM 
 */ 
THH_cmd_status_t THH_cmd_READ_MGM(THH_cmd_t cmd_if, u_int32_t mcg_index,
                                  MT_size_t  max_qp_per_mcg, THH_mcg_entry_t *mcg_entry)
{
    // need to add qps_per_mcg_entry field
    command_fields_t cmd_desc;
    u_int8_t *outprm;
    THH_cmd_status_t rc;
    IB_wqpn_t  /**qp_buf,*/ *qp_iterator;
    u_int32_t buf_size, i, num_active_qps_found, temp_u32;
    u_int8_t    valid;

    FUNC_IN;

    /* the default mcg_entry structure contains space for 8 qps per mcg */
    /* If HCA is configured for more than 8 qps per group, space for the extra qp entries */
    /* must be allocated as well */
    buf_size = (u_int32_t)(PSEUDO_MT_BYTE_SIZE(tavorprm_mgm_entry_st) + 
        ((max_qp_per_mcg - 8)*PSEUDO_MT_BYTE_SIZE(tavorprm_mgmqp_st)));

    outprm = (u_int8_t*)MALLOC(buf_size);
    if ( !outprm ) {
      MT_RETURN(THH_CMD_STAT_EAGAIN);
    }

    memset(outprm, 0, buf_size);

    cmd_desc.in_param = 0;
    cmd_desc.in_param_size = 0;
    cmd_desc.in_trans = TRANS_NA;
    cmd_desc.input_modifier = mcg_index;
    cmd_desc.out_param = outprm;
    cmd_desc.out_param_size = buf_size;
    cmd_desc.out_trans = TRANS_MAILBOX;
    cmd_desc.opcode = TAVOR_IF_CMD_READ_MGM;
    cmd_desc.opcode_modifier = 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_READ_MGM;

    rc = cmd_invoke(cmd_if, &cmd_desc);
    if ( rc != THH_CMD_STAT_OK ) {
       goto invoke_err;
    }
    
    if ( mcg_entry ) {
      /* allocate memory for the multicast QPs IB_wqpn_t */
      //qp_buf = (IB_wqpn_t  *)MALLOC(sizeof(IB_wqpn_t) * max_qp_per_mcg); 
      //if ( !qp_buf ) {
       // rc = THH_CMD_STAT_EAGAIN;
        //goto malloc_err;
      //}
      //memset(qp_buf, 0, sizeof(IB_wqpn_t) * max_qp_per_mcg);

      /* get fixed portion of reply */
      EX_FLD(mcg_entry, outprm, tavorprm_mgm_entry_st, next_gid_index);

      /* extract MGID.  Note that get the MGID from the command object as 4 double-words, each in CPU-endianness. */
      /* Need to take them one at a time, and convert each to big-endian before storing in the output MGID array */
      /* Note that need to memcpy each 4 bytes to a temporary u_int32_t variable, since there is no guarantee */
      /* that the MGID is 4-byte aligned (it is an array of unsigned chars) */
      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgid_128_96),
                     MT_BIT_SIZE(tavorprm_mgm_entry_st, mgid_128_96)));
      memcpy(&(mcg_entry->mgid[0]), &temp_u32, sizeof(u_int32_t));

      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgid_95_64),
                     MT_BIT_SIZE(tavorprm_mgm_entry_st, mgid_95_64)));
      memcpy(&(mcg_entry->mgid[4]), &temp_u32, sizeof(u_int32_t));

      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgid_63_32),
                     MT_BIT_SIZE(tavorprm_mgm_entry_st, mgid_63_32)));
      memcpy(&(mcg_entry->mgid[8]), &temp_u32, sizeof(u_int32_t));

      temp_u32 = MOSAL_cpu_to_be32(MT_EXTRACT_ARRAY32(outprm, 
                     MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgid_31_0),
                     MT_BIT_SIZE(tavorprm_mgm_entry_st, mgid_31_0)));
      memcpy(&(mcg_entry->mgid[12]), &temp_u32, sizeof(u_int32_t));

      /* Now, extract the QP entries in the group */
      for (i = 0, num_active_qps_found = 0, qp_iterator = mcg_entry->qps/*qp_buf*/; i < max_qp_per_mcg; i++ ) {
             /* extract VALID bit for each QP.  If valid is set, extract the QP number and insert in */
             /* the QP array returned */
             valid =  (u_int8_t)(MT_EXTRACT_ARRAY32(outprm, 
                         (MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgmqp_0.qi) + i*(MT_BIT_SIZE(tavorprm_mgm_entry_st, mgmqp_0))),
                          MT_BIT_SIZE(tavorprm_mgm_entry_st, mgmqp_0.qi) ));
             if (valid) {
                  /* NULL protection .. */
                  if (mcg_entry->qps) {
                      *((u_int32_t *) qp_iterator) = MT_EXTRACT_ARRAY32(outprm, 
                           (MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgmqp_0.qpn_i) + i*(MT_BIT_SIZE(tavorprm_mgm_entry_st, mgmqp_0))),
                            MT_BIT_SIZE(tavorprm_mgm_entry_st, mgmqp_0.qpn_i) );
                      qp_iterator++;
                  }
                  num_active_qps_found++;
             }
      }
      mcg_entry->valid_qps = num_active_qps_found;
      /* If valid QPs found, return the allocated QP number buffer and number found.  Otherwise */
      /* return 0 QPs and delete the buffer */
      //if (num_active_qps_found) {
        //  mcg_entry->qps = qp_buf;
      //} else {
       //   mcg_entry->qps = NULL;
      //    FREE(qp_buf);
      //}
    }

invoke_err:
    FREE(outprm);
    MT_RETURN(rc);
}

/*
 *  THH_cmd_WRITE_MGM 
 */ 
THH_cmd_status_t THH_cmd_WRITE_MGM(THH_cmd_t cmd_if, u_int32_t mcg_index,
                                   MT_size_t  max_qp_per_mcg, THH_mcg_entry_t *mcg_entry)
{
    command_fields_t cmd_desc;
    u_int8_t inprm[PSEUDO_MT_BYTE_SIZE(tavorprm_mgm_entry_st)];
    THH_cmd_status_t rc;
    u_int32_t buf_size = PSEUDO_MT_BYTE_SIZE(tavorprm_mgm_entry_st);
    IB_wqpn_t *qp_iterator;
    u_int32_t   i, temp_u32;

    FUNC_IN;
    
    CMDS_DBG("THH_cmd_WRITE_MGM: index=%u, max_qp_per_mcg = "SIZE_T_FMT"\n", mcg_index, max_qp_per_mcg);
    if (mcg_entry == NULL) {MT_RETURN(THH_CMD_STAT_EBADARG); }
    THH_CMD_PRINT_MGM_ENTRY(mcg_entry);
    memset(inprm, 0, buf_size);

    cmd_desc.in_param = inprm;
    cmd_desc.in_param_size = buf_size;
    cmd_desc.in_trans = TRANS_MAILBOX;
    cmd_desc.input_modifier = mcg_index;
    cmd_desc.out_param = 0;
    cmd_desc.out_param_size = 0;
    cmd_desc.out_trans = TRANS_NA;
    cmd_desc.opcode = TAVOR_IF_CMD_WRITE_MGM;
    cmd_desc.opcode_modifier = 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_WRITE_MGM;

    /* get fixed portion of reply */
    INS_FLD(mcg_entry, inprm, tavorprm_mgm_entry_st, next_gid_index);

    /* insert MGID.  Note that get the MGID from the command object as 4 double-words, each in CPU-endianness. */
    /* Need to take them one at a time, and convert each to big-endian before storing in the output MGID array */
    memcpy(&temp_u32, &(mcg_entry->mgid[0]), sizeof(u_int32_t));
    MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                   MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgid_128_96),
                   MT_BIT_SIZE(tavorprm_mgm_entry_st, mgid_128_96));
    
    memcpy(&temp_u32, &(mcg_entry->mgid[4]), sizeof(u_int32_t));
    MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                   MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgid_95_64),
                   MT_BIT_SIZE(tavorprm_mgm_entry_st, mgid_95_64));
    
    memcpy(&temp_u32, &(mcg_entry->mgid[8]), sizeof(u_int32_t));
    MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                   MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgid_63_32),
                   MT_BIT_SIZE(tavorprm_mgm_entry_st, mgid_63_32));

    memcpy(&temp_u32, &(mcg_entry->mgid[12]), sizeof(u_int32_t));
    MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32), 
                   MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgid_31_0),
                   MT_BIT_SIZE(tavorprm_mgm_entry_st, mgid_31_0));

    /* Now, insert the QP entries in the group */
    for (i = 0, qp_iterator = mcg_entry->qps; i < max_qp_per_mcg; i++, qp_iterator++ ) {
       /* Insert valid entries.  First, insert a VALID bit = 1 for each valid QP number, then insert */
       /* the QP number itself.  If there are no more valid entries, insert only a VALID bit = 0 for each */
       /* invalid entry, up to the maximum allowed QPs */
       if (i < mcg_entry->valid_qps) {
           MT_INSERT_ARRAY32(inprm, 1, 
                       (MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgmqp_0.qi) + i*(MT_BIT_SIZE(tavorprm_mgm_entry_st, mgmqp_0))),
                        MT_BIT_SIZE(tavorprm_mgm_entry_st, mgmqp_0.qi) );
           MT_INSERT_ARRAY32(inprm, *((u_int32_t *) qp_iterator) , 
                       (MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgmqp_0.qpn_i) + i*(MT_BIT_SIZE(tavorprm_mgm_entry_st, mgmqp_0))),
                        MT_BIT_SIZE(tavorprm_mgm_entry_st, mgmqp_0.qpn_i) );
       } else {
           MT_INSERT_ARRAY32(inprm, 0, 
                       (MT_BIT_OFFSET(tavorprm_mgm_entry_st, mgmqp_0.qi) + i*(MT_BIT_SIZE(tavorprm_mgm_entry_st, mgmqp_0))),
                        MT_BIT_SIZE(tavorprm_mgm_entry_st, mgmqp_0.qi) );
       }
    }
    THH_CMD_MAILBOX_PRINT(inprm, buf_size, __func__); 
    rc = cmd_invoke(cmd_if, &cmd_desc);
    MT_RETURN(rc);
}

/*
 *  THH_cmd_HASH 
 */ 
THH_cmd_status_t THH_cmd_MGID_HASH(THH_cmd_t cmd_if, IB_gid_t mgid, THH_mcg_hash_t *hash_val)
{
    command_fields_t cmd_desc;
    u_int8_t inprm[16];
    THH_cmd_status_t rc;
    u_int32_t   out_param[2], temp_u32;
    
    FUNC_IN;
    memset(inprm, 0, 16);
    memset(out_param, 0, sizeof(out_param));

    cmd_desc.in_param = inprm;
    cmd_desc.in_param_size = sizeof(IB_gid_t);
    cmd_desc.in_trans = TRANS_MAILBOX;
    cmd_desc.input_modifier = 0;
    cmd_desc.out_param = (u_int8_t *)&(out_param[0]);
    cmd_desc.out_param_size = sizeof(out_param);
    cmd_desc.out_trans = TRANS_IMMEDIATE;
    cmd_desc.opcode = TAVOR_IF_CMD_MGID_HASH;
    cmd_desc.opcode_modifier = 0;
    cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_MGID_HASH;

    /* insert GID into mailbox, 1 double-word at a time.  Modify byte ordering within doubl-words */
    /* to cpu-endian, so that lower layers will properly process the GID */
    memcpy(&temp_u32, &(mgid[0]), sizeof(u_int32_t));
    MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32),0,32);
    memcpy(&temp_u32, &(mgid[4]), sizeof(u_int32_t));
    MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32),32,32);
    memcpy(&temp_u32, &(mgid[8]), sizeof(u_int32_t));
    MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32),64,32); 
    memcpy(&temp_u32, &(mgid[12]), sizeof(u_int32_t));
    MT_INSERT_ARRAY32(inprm, MOSAL_be32_to_cpu(temp_u32),96,32); 

    rc = cmd_invoke(cmd_if, &cmd_desc);

    /* Note that output result is directly inserted into hash_val parameter, in cpu-endian order */
    /* Only the first of the two output double-words needs to be copied. */
    CMDS_DBG( "THH_cmd_MGID_HASH:  out_param[0] = 0x%x; out_param[1] = 0x%x\n", 
              out_param[0], out_param[1]);

    *hash_val = (THH_mcg_hash_t)MT_EXTRACT32(out_param[1],0,16);
    MT_RETURN(rc);
}

#if defined(MT_SUSPEND_QP)
THH_cmd_status_t THH_cmd_SUSPEND_QP(THH_cmd_t cmd_if,  u_int32_t qpn, MT_bool suspend_flag)
{
  command_fields_t cmd_desc;
  THH_cmd_status_t rc;
  FUNC_IN;
  
  MTL_DEBUG2(MT_FLFMT("%s: qpn = 0x%x, suspend_flag = %s"), 
             __func__, qpn, ((suspend_flag==TRUE) ? "TRUE" : "FALSE" ));
  cmd_desc.in_param = 0;
  cmd_desc.in_param_size = 0;
  cmd_desc.in_trans = TRANS_NA;
  cmd_desc.input_modifier = qpn & 0xFFFFFF;
  //cmd_desc.input_modifier |= (is_ee ? 0x1000000 : 0);
  cmd_desc.out_param = 0;
  cmd_desc.out_param_size = 0;
  cmd_desc.out_trans = TRANS_NA;
  cmd_desc.opcode = (suspend_flag == TRUE) ? TAVOR_IF_CMD_SUSPEND_QPEE : TAVOR_IF_CMD_UNSUSPEND_QPEE;
  cmd_desc.opcode_modifier = 1;
  cmd_desc.exec_time_micro = TAVOR_IF_CMD_ETIME_CLASS_C;
    
  rc = cmd_invoke(cmd_if, &cmd_desc);
  MTL_DEBUG2(MT_FLFMT("%s: qpn = 0x%x, command returned 0x%x"), __func__,qpn, rc);
  MT_RETURN(rc);
}
#endif
