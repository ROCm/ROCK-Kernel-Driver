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

#ifdef MT_KERNEL
 /* Taken care via  vapi_common.h -> mtl_common.h -> ... ->
  *                 -> /lib/modules/N.N.N/build/include/linux/string.h
  * extern int    strlen(const char*);
  * extern char* (char*, const char*);
  */
#else
# include <string.h>
#endif
#include "vapi_common.h"

const char* VAPI_strerror( VAPI_ret_t errnum)
{
  switch (errnum) {
#define VAPI_ERROR_INFO(A, B, C) case A: return C;
    VAPI_ERROR_LIST
#undef VAPI_ERROR_INFO
    default: return "VAPI_UNKNOWN_ERROR";
  }

}
const char* VAPI_strerror_sym( VAPI_ret_t errnum)
{
  switch (errnum) {
#define VAPI_ERROR_INFO(A, B, C) case A: return #A;
    VAPI_ERROR_LIST
#undef VAPI_ERROR_INFO
    default: return "VAPI_UNKNOWN_ERROR";
  }
}



static char*  safe_append(
  char*       cbuf,
  char*       buf_end,
  u_int32_t   mask,
  u_int32_t   flag,
  const char* flag_sym
)
{
  if (mask & flag)
  {
    int  l = (int)strlen(flag_sym);
    if (cbuf + l + 2 < buf_end)
    {
      strcpy(cbuf, flag_sym);
      cbuf += l;
      *cbuf++ = '+';
      *cbuf = '\0';
    }
    else
    {
      cbuf = NULL;
    }
  }
  return cbuf;
} /* safe_append */


static  void end_mask_sym(char* buf, char* cbuf, int bufsz)
{
  if (bufsz > 0)
  {
    if (buf == cbuf)
    {
      *cbuf = '\0'; /* empty string */
    }
    else if (cbuf == 0) /* was truncated */
    {
       int  l = (int)strlen(buf);
       buf[l - 1] = '>';
    }
  }
} /* end_mask_sym */


#define INIT_BUF_SKIP(skipped_pfx)     \
  int    skip = (int)strlen(skipped_pfx);   \
  char*  cbuf     = buf;               \
  char*  buf_end  = buf + bufsz;       \
  *buf = '\0';


#define SAFE_APPEND(e) \
  if (cbuf) { cbuf = safe_append(cbuf, buf_end, mask, e, #e + skip); }

const char* VAPI_hca_cap_sym(char* buf, int bufsz, u_int32_t mask)
{
  INIT_BUF_SKIP("VAPI_")

  SAFE_APPEND(VAPI_RESIZE_OUS_WQE_CAP)
  SAFE_APPEND(VAPI_BAD_PKEY_COUNT_CAP)
  SAFE_APPEND(VAPI_BAD_QKEY_COUNT_CAP)
  SAFE_APPEND(VAPI_RAW_MULTI_CAP)
  SAFE_APPEND(VAPI_AUTO_PATH_MIG_CAP)
  SAFE_APPEND(VAPI_CHANGE_PHY_PORT_CAP)
  SAFE_APPEND(VAPI_UD_AV_PORT_ENFORCE_CAP)
  SAFE_APPEND(VAPI_CURR_QP_STATE_MOD_CAP)
  SAFE_APPEND(VAPI_SHUTDOWN_PORT_CAP)
  SAFE_APPEND(VAPI_INIT_TYPE_CAP)
  SAFE_APPEND(VAPI_PORT_ACTIVE_EV_CAP)
  SAFE_APPEND(VAPI_SYS_IMG_GUID_CAP)
  SAFE_APPEND(VAPI_RC_RNR_NAK_GEN_CAP)

  end_mask_sym(buf, cbuf, bufsz);
  return buf;
} /* VAPI_hca_cap_sym */


const char* VAPI_hca_attr_mask_sym(char* buf, int bufsz, u_int32_t mask)
{
  INIT_BUF_SKIP("HCA_ATTR_")
  SAFE_APPEND(HCA_ATTR_IS_SM)
  SAFE_APPEND(HCA_ATTR_IS_SNMP_TUN_SUP)
  SAFE_APPEND(HCA_ATTR_IS_DEV_MGT_SUP)
  SAFE_APPEND(HCA_ATTR_IS_VENDOR_CLS_SUP)
  SAFE_APPEND(HCA_ATTR_MAX)
  end_mask_sym(buf, cbuf, bufsz);
  return buf;
} /* VAPI_hca_attr_mask_sym */


const char* VAPI_qp_attr_mask_sym(char* buf, int bufsz, u_int32_t mask)
{
  INIT_BUF_SKIP("QP_ATTR_")

  SAFE_APPEND(QP_ATTR_QP_STATE)
  SAFE_APPEND(QP_ATTR_EN_SQD_ASYN_NOTIF)
  SAFE_APPEND(QP_ATTR_QP_NUM)
  SAFE_APPEND(QP_ATTR_REMOTE_ATOMIC_FLAGS)
  SAFE_APPEND(QP_ATTR_PKEY_IX)
  SAFE_APPEND(QP_ATTR_PORT)
  SAFE_APPEND(QP_ATTR_QKEY)
  SAFE_APPEND(QP_ATTR_AV)
  SAFE_APPEND(QP_ATTR_PATH_MTU)
  SAFE_APPEND(QP_ATTR_TIMEOUT)
  SAFE_APPEND(QP_ATTR_RETRY_COUNT)
  SAFE_APPEND(QP_ATTR_RNR_RETRY)
  SAFE_APPEND(QP_ATTR_RQ_PSN)
  SAFE_APPEND(QP_ATTR_QP_OUS_RD_ATOM)
  SAFE_APPEND(QP_ATTR_ALT_PATH)
  SAFE_APPEND(QP_ATTR_RSRV_1)
  SAFE_APPEND(QP_ATTR_RSRV_2)
  SAFE_APPEND(QP_ATTR_RSRV_3)
  SAFE_APPEND(QP_ATTR_RSRV_4)
  SAFE_APPEND(QP_ATTR_RSRV_5)
  SAFE_APPEND(QP_ATTR_RSRV_6)
  //SAFE_APPEND(QP_ATTR_ALT_TIMEOUT)
  //SAFE_APPEND(QP_ATTR_ALT_RETRY_COUNT)
  //SAFE_APPEND(QP_ATTR_ALT_RNR_RETRY)
  //SAFE_APPEND(QP_ATTR_ALT_PKEY_IX)
  //SAFE_APPEND(QP_ATTR_ALT_PORT)
  SAFE_APPEND(QP_ATTR_MIN_RNR_TIMER)
  SAFE_APPEND(QP_ATTR_SQ_PSN)
  SAFE_APPEND(QP_ATTR_OUS_DST_RD_ATOM)
  SAFE_APPEND(QP_ATTR_PATH_MIG_STATE)
  SAFE_APPEND(QP_ATTR_CAP)
  SAFE_APPEND(QP_ATTR_DEST_QP_NUM)
  end_mask_sym(buf, cbuf, bufsz);
  return buf;
} /* VAPI_qp_attr_mask_sym */


const char* VAPI_mrw_acl_mask_sym(char* buf, int bufsz, u_int32_t mask)
{
  INIT_BUF_SKIP("VAPI_EN_")

  SAFE_APPEND(VAPI_EN_LOCAL_WRITE)
  SAFE_APPEND(VAPI_EN_REMOTE_WRITE)
  SAFE_APPEND(VAPI_EN_REMOTE_READ)
  SAFE_APPEND(VAPI_EN_REMOTE_ATOM)
  SAFE_APPEND(VAPI_EN_MEMREG_BIND)
  end_mask_sym(buf, cbuf, bufsz);
  return buf;
} /* VAPI_mrw_acl_mask_sym */


const char* VAPI_mr_change_mask_sym(char* buf, int bufsz, u_int32_t mask)
{
  INIT_BUF_SKIP("VAPI_MR_")

  SAFE_APPEND(VAPI_MR_CHANGE_TRANS)
  SAFE_APPEND(VAPI_MR_CHANGE_PD)
  SAFE_APPEND(VAPI_MR_CHANGE_ACL)
  end_mask_sym(buf, cbuf, bufsz);
  return buf;
} /* VAPI_mr_change_mask_sym */


const char* VAPI_rdma_atom_acl_mask_sym(char* buf, int bufsz, u_int32_t mask)
{
  INIT_BUF_SKIP("VAPI_EN_REM_")
  SAFE_APPEND(VAPI_EN_REM_WRITE)
  SAFE_APPEND(VAPI_EN_REM_READ)
  SAFE_APPEND(VAPI_EN_REM_ATOMIC_OP)
  end_mask_sym(buf, cbuf, bufsz);
  return buf;
} /* VAPI_rdma_atom_acl_sym */


#define CASE_SETSTR(e)  case e: s = #e; break;
static  const char*  UnKnown = "UnKnown";

const char* VAPI_atomic_cap_sym(VAPI_atomic_cap_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_ATOMIC_CAP_NONE)
    CASE_SETSTR(VAPI_ATOMIC_CAP_HCA)
    CASE_SETSTR(VAPI_ATOMIC_CAP_GLOB)
    default: ;
  }
  return s;
} /* VAPI_atomic_cap_sym */


const char* VAPI_sig_type_sym(VAPI_sig_type_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_SIGNAL_ALL_WR)
    CASE_SETSTR(VAPI_SIGNAL_REQ_WR)
    default: ;
  }
  return s;
} /* VAPI_sig_type_sym */


const char* VAPI_ts_type_sym(VAPI_ts_type_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_TS_RC)
    CASE_SETSTR(VAPI_TS_RD)
    CASE_SETSTR(VAPI_TS_UC)
    CASE_SETSTR(VAPI_TS_UD)
    CASE_SETSTR(VAPI_TS_RAW)
    default: ;
  }
  return s;
} /* VAPI_ts_type_sym */


const char* VAPI_qp_state_sym(VAPI_qp_state_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_RESET)
    CASE_SETSTR(VAPI_INIT)
    CASE_SETSTR(VAPI_RTR)
    CASE_SETSTR(VAPI_RTS)
    CASE_SETSTR(VAPI_SQD)
    CASE_SETSTR(VAPI_SQE)
    CASE_SETSTR(VAPI_ERR)
    default: ;
  }
  return s;
} /* VAPI_qp_state_sym */


const char* VAPI_mig_state_sym(VAPI_mig_state_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_MIGRATED)
    CASE_SETSTR(VAPI_REARM)
    CASE_SETSTR(VAPI_ARMED)
    default: ;
  }
  return s;
} /* VAPI_mig_state_sym */


const char* VAPI_special_qp_sym(VAPI_special_qp_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_REGULAR_QP)
    CASE_SETSTR(VAPI_SMI_QP)
    CASE_SETSTR(VAPI_GSI_QP)
    CASE_SETSTR(VAPI_RAW_IPV6_QP)
    CASE_SETSTR(VAPI_RAW_ETY_QP)
    default: ;
  }
  return s;
} /* VAPI_special_qp_sym */


const char* VAPI_mrw_type_sym(VAPI_mrw_type_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_MR)
    CASE_SETSTR(VAPI_MW)
    CASE_SETSTR(VAPI_MPR)
    CASE_SETSTR(VAPI_MSHAR)
    default: ;
  }
  return s;
} /* VAPI_mrw_type_sym */


const char* VAPI_remote_node_addr_sym(VAPI_remote_node_addr_type_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_RNA_RD)
    CASE_SETSTR(VAPI_RNA_UD)
    CASE_SETSTR(VAPI_RNA_RAW_ETY)
    CASE_SETSTR(VAPI_RNA_RAW_IPV6)
    default: ;
  }
  return s;
} /* VAPI_remote_node_addr_sym */


const char* VAPI_wr_opcode_sym(VAPI_wr_opcode_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_RDMA_WRITE)
    CASE_SETSTR(VAPI_RDMA_WRITE_WITH_IMM)
    CASE_SETSTR(VAPI_SEND)
    CASE_SETSTR(VAPI_SEND_WITH_IMM)
    CASE_SETSTR(VAPI_RDMA_READ)
    CASE_SETSTR(VAPI_ATOMIC_CMP_AND_SWP)
    CASE_SETSTR(VAPI_ATOMIC_FETCH_AND_ADD)
    CASE_SETSTR(VAPI_RECEIVE)
    default: ;
  }
  return s;
} /* VAPI_wr_opcode_sym */


const char* VAPI_cqe_opcode_sym(VAPI_cqe_opcode_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_CQE_SQ_SEND_DATA)
    CASE_SETSTR(VAPI_CQE_SQ_RDMA_WRITE)
    CASE_SETSTR(VAPI_CQE_SQ_RDMA_READ)
    CASE_SETSTR(VAPI_CQE_SQ_COMP_SWAP)
    CASE_SETSTR(VAPI_CQE_SQ_FETCH_ADD)
    CASE_SETSTR(VAPI_CQE_SQ_BIND_MRW)
    CASE_SETSTR(VAPI_CQE_RQ_SEND_DATA)
    CASE_SETSTR(VAPI_CQE_RQ_RDMA_WITH_IMM)
    default: ;
  }
  return s;
} /* VAPI_cqe_opcode_sym */


const char* VAPI_wc_status_sym(VAPI_wc_status_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_SUCCESS)
    CASE_SETSTR(VAPI_LOC_LEN_ERR)
    CASE_SETSTR(VAPI_LOC_QP_OP_ERR)
    CASE_SETSTR(VAPI_LOC_EE_OP_ERR)
    CASE_SETSTR(VAPI_LOC_PROT_ERR)
    CASE_SETSTR(VAPI_WR_FLUSH_ERR)
    CASE_SETSTR(VAPI_MW_BIND_ERR)
    CASE_SETSTR(VAPI_BAD_RESP_ERR)
    CASE_SETSTR(VAPI_LOC_ACCS_ERR)
    CASE_SETSTR(VAPI_REM_INV_REQ_ERR)
    CASE_SETSTR(VAPI_REM_ACCESS_ERR)
    CASE_SETSTR(VAPI_REM_OP_ERR)
    CASE_SETSTR(VAPI_RETRY_EXC_ERR)
    CASE_SETSTR(VAPI_RNR_RETRY_EXC_ERR)
    CASE_SETSTR(VAPI_LOC_RDD_VIOL_ERR)
    CASE_SETSTR(VAPI_REM_INV_RD_REQ_ERR)
    CASE_SETSTR(VAPI_REM_ABORT_ERR)
    CASE_SETSTR(VAPI_INV_EECN_ERR)
    CASE_SETSTR(VAPI_INV_EEC_STATE_ERR)
    CASE_SETSTR(VAPI_COMP_FATAL_ERR)
    CASE_SETSTR(VAPI_COMP_GENERAL_ERR)
    default: ;
  }
  return s;
} /* VAPI_wc_status_sym */


const char* VAPI_comp_type_sym(VAPI_comp_type_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_SIGNALED)
    CASE_SETSTR(VAPI_UNSIGNALED)
    default: ;
  }
  return s;
} /* VAPI_comp_type_sym */


const char* VAPI_cq_notif_sym(VAPI_cq_notif_type_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_NOTIF_NONE)
    CASE_SETSTR(VAPI_SOLIC_COMP)
    CASE_SETSTR(VAPI_NEXT_COMP)
    default: ;
  }
  return s;
} /* VAPI_cq_notif_sym */


const char* VAPI_event_record_sym(VAPI_event_record_type_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
    CASE_SETSTR(VAPI_QP_PATH_MIGRATED)
    CASE_SETSTR(VAPI_EEC_PATH_MIGRATED)
    CASE_SETSTR(VAPI_QP_COMM_ESTABLISHED)
    CASE_SETSTR(VAPI_EEC_COMM_ESTABLISHED)
    CASE_SETSTR(VAPI_SEND_QUEUE_DRAINED)
    CASE_SETSTR(VAPI_CQ_ERROR)
    CASE_SETSTR(VAPI_LOCAL_WQ_INV_REQUEST_ERROR)
    CASE_SETSTR(VAPI_LOCAL_WQ_ACCESS_VIOL_ERROR)
    CASE_SETSTR(VAPI_LOCAL_WQ_CATASTROPHIC_ERROR)
    CASE_SETSTR(VAPI_PATH_MIG_REQ_ERROR)
    CASE_SETSTR(VAPI_LOCAL_EEC_CATASTROPHIC_ERROR)
    CASE_SETSTR(VAPI_LOCAL_CATASTROPHIC_ERROR)
    CASE_SETSTR(VAPI_PORT_ERROR)
    CASE_SETSTR(VAPI_PORT_ACTIVE)
    CASE_SETSTR(VAPI_RECEIVE_QUEUE_DRAINED)
    CASE_SETSTR(VAPI_SRQ_LIMIT_REACHED)
    CASE_SETSTR(VAPI_SRQ_CATASTROPHIC_ERROR)
    default: ;
  }
  return s;
} /* VAPI_event_record_sym */

const char* VAPI_event_syndrome_sym(VAPI_event_syndrome_t e)
{
  const char*  s = UnKnown;
  switch (e)
  {
      CASE_SETSTR(VAPI_EV_SYNDROME_NONE)
      CASE_SETSTR(VAPI_CATAS_ERR_FW_INTERNAL)
      CASE_SETSTR(VAPI_CATAS_ERR_EQ_OVERFLOW)
      CASE_SETSTR(VAPI_CATAS_ERR_MISBEHAVED_UAR_PAGE)
      CASE_SETSTR(VAPI_CATAS_ERR_UPLINK_BUS_ERR)
      CASE_SETSTR(VAPI_CATAS_ERR_HCA_DDR_DATA_ERR)
      CASE_SETSTR(VAPI_CATAS_ERR_INTERNAL_PARITY_ERR)
      CASE_SETSTR(VAPI_CATAS_ERR_MASTER_ABORT)
      CASE_SETSTR(VAPI_CATAS_ERR_GO_BIT)
      CASE_SETSTR(VAPI_CATAS_ERR_CMD_TIMEOUT)
      CASE_SETSTR(VAPI_CATAS_ERR_FATAL_CR)
      CASE_SETSTR(VAPI_CATAS_ERR_FATAL_TOKEN)
      CASE_SETSTR(VAPI_CATAS_ERR_GENERAL)
      CASE_SETSTR(VAPI_CQ_ERR_OVERRUN)
      CASE_SETSTR(VAPI_CQ_ERR_ACCESS_VIOL)
    default: ;
  }
  return s;
}


#if defined(TEST_VAPI_COMMON)
/* compile via:
  gcc -g -DTEST_VAPI_COMMON -I.. -I$MTHOME/include -o /tmp/x vapi_common.c
 */
int main(int argc, char** argv)
{
  char    buffer[100];
  char*   cbuffer = &buffer[0];
  u_int32_t m = VAPI_BAD_PKEY_COUNT_CAP | VAPI_AUTO_PATH_MIG_CAP;
  printf("m=%s\n", VAPI_hca_cap_sym(buffer, sizeof(buffer), m));
  printf("trunc1: m=%s\n", VAPI_hca_cap_sym(buffer, 1, m));
  printf("trunc10: m=%s\n", VAPI_hca_cap_sym(buffer, 10, m));

  return 0;
}
#endif
