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
#include "nMPGA.h"
#include "nMPGA_packet_append.h"
#include <mpga.h>

#undef MT_BIT_OFFSET
#define MT_BIT_OFFSET(struct_ancore,reg_path) \
    ((MT_offset_t) &( ((struct struct_ancore *)(0))-> reg_path ))
#undef MT_BIT_SIZE
#define MT_BIT_SIZE(struct_ancore,reg_path) \
    ((MT_size_t) sizeof( ((struct struct_ancore *)(0))-> reg_path ))
#undef MT_BIT_OFFSET_SIZE
#define MT_BIT_OFFSET_SIZE(struct_ancore,reg_path) \
            MT_BIT_OFFSET(struct_ancore,reg_path) , MT_BIT_SIZE(struct_ancore,reg_path)

call_result_t 
MPGA_get_headers_size(IB_opcode_t opcode,                                      
                      LNH_t LNH,                                               
                      u_int16_t payload_len,                                   
                      MT_bool icrc, /*if set - icrc exist*/                       
                      MT_bool vcrc, /*if set - vcrc exist*/                       
                      u_int16_t *packet_len) /*(OUT) packet length in bytes*/
{
  *packet_len=0;
  switch (LNH)
  {
    case RAW:           /* |LRH|... (Etertype)*/
      MTL_ERROR1("%s: Unsupported LNH (%d)\n", __func__, LNH);
      return(MT_ERROR);               
    case IP_NON_IBA_TRANS:  /* |LRH|GRH|...       */
      MTL_ERROR1("%s: Unsupported LNH (%d)\n", __func__, LNH);
      return(MT_ERROR);
    case IBA_LOCAL:         /* |LRH|BTH|...       */
      break;
    case IBA_GLOBAL:         /* |LRH|GRH|BTH|...   */
      *packet_len = GRH_LEN;
      break;
    default:
      MTL_ERROR1("%s: Invalid LNH (%d)\n", __func__, LNH);
      return(MT_ERROR);
      break;

  }

  *packet_len += payload_len + ( icrc ? ICRC_LEN : 0 ) + ( vcrc ? VCRC_LEN : 0 );

  switch (opcode)
  {

    /***********************************************/
    /* reliable Connection (RC)  	               */
    /***********************************************/
    
    case RC_SEND_FIRST_OP:
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK);

    case RC_SEND_MIDDLE_OP:
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK);

    case RC_SEND_LAST_OP:
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK);

    case RC_SEND_LAST_W_IM_OP:      
      *packet_len+=LRH_LEN+BTH_LEN+ImmDt_LEN;
      return(MT_OK);

    case RC_SEND_ONLY_OP:         
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK);

    case RC_SEND_ONLY_W_IM_OP:      
      *packet_len+=LRH_LEN+BTH_LEN+ImmDt_LEN;
      return(MT_OK);

    case RC_WRITE_FIRST_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RETH_LEN;
      return(MT_OK);

    case RC_WRITE_MIDDLE_OP:
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK);

    case RC_WRITE_LAST_OP:          
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK);

    case RC_WRITE_LAST_W_IM_OP:     
      *packet_len+=LRH_LEN+BTH_LEN+ImmDt_LEN;
      return(MT_OK);

    case RC_WRITE_ONLY_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RETH_LEN;
      return(MT_OK);

    case RC_WRITE_ONLY_W_IM_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RETH_LEN+ImmDt_LEN;
      return(MT_OK);

    case RC_READ_REQ_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RETH_LEN;
      return(MT_OK);

    case RC_READ_RESP_FIRST_OP:
      *packet_len+=LRH_LEN+BTH_LEN+AETH_LEN;
      return(MT_OK);

    case RC_READ_RESP_MIDDLE_OP:
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK);

    case RC_READ_RESP_LAST_OP:
      *packet_len+=LRH_LEN+BTH_LEN+AETH_LEN;
      return(MT_OK);

    case RC_READ_RESP_ONLY_OP:
      *packet_len+=LRH_LEN+BTH_LEN+AETH_LEN;
      return(MT_OK);

    case RC_ACKNOWLEDGE_OP:
      *packet_len+=LRH_LEN+BTH_LEN+AETH_LEN;
      return(MT_OK);

    case RC_ATOMIC_ACKNOWLEDGE_OP:
      *packet_len+=LRH_LEN+BTH_LEN+AETH_LEN+AtomAETH_LEN;
      return(MT_OK);

    case RC_CMP_SWAP_OP:
      *packet_len+=LRH_LEN+BTH_LEN+AtomETH_LEN;
      return(MT_OK);

    case RC_FETCH_ADD_OP:
      *packet_len+=LRH_LEN+BTH_LEN+AtomETH_LEN;
      return(MT_OK);

/***********************************************/
/* Unreliable Connection (UC)                  */
/***********************************************/

    case UC_SEND_FIRST_OP:
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK);          

    case UC_SEND_MIDDLE_OP:         
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK);

    case UC_SEND_LAST_OP:           
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK);

    case UC_SEND_LAST_W_IM_OP:
      *packet_len+=LRH_LEN+BTH_LEN+ImmDt_LEN;
      return(MT_OK); 

    case UC_SEND_ONLY_OP:
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK); 

    case UC_SEND_ONLY_W_IM_OP:
      *packet_len+=LRH_LEN+BTH_LEN+ImmDt_LEN;
      return(MT_OK); 

    case UC_WRITE_FIRST_OP: 
      *packet_len+=LRH_LEN+BTH_LEN+RETH_LEN;
      return(MT_OK);

    case UC_WRITE_MIDDLE_OP:
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK); 

    case UC_WRITE_LAST_OP:
      *packet_len+=LRH_LEN+BTH_LEN;
      return(MT_OK);

    case UC_WRITE_LAST_W_IM_OP:
      *packet_len+=LRH_LEN+BTH_LEN+ImmDt_LEN;
      return(MT_OK); 

    case UC_WRITE_ONLY_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RETH_LEN;
      return(MT_OK); 

    case UC_WRITE_ONLY_W_IM_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RETH_LEN+ImmDt_LEN;
      return(MT_OK);

/***********************************************/
/* Reliable Datagram (RD)                      */
/***********************************************/

    case RD_SEND_FIRST_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN;
      return(MT_OK);

    case RD_SEND_MIDDLE_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN;
      return(MT_OK);

    case RD_SEND_LAST_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN;
      return(MT_OK);

    case RD_SEND_LAST_W_IM_OP:      
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+ImmDt_LEN;
      return(MT_OK);

    case RD_SEND_ONLY_OP:         
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN;
      return(MT_OK);

    case RD_SEND_ONLY_W_IM_OP:      
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+ImmDt_LEN;
      return(MT_OK);

    case RD_WRITE_FIRST_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+RETH_LEN;
      return(MT_OK);

    case RD_WRITE_MIDDLE_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN;
      return(MT_OK);

    case RD_WRITE_LAST_OP:          
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN;
      return(MT_OK);

    case RD_WRITE_LAST_W_IM_OP:     
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+ImmDt_LEN;
      return(MT_OK);

    case RD_WRITE_ONLY_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+RETH_LEN;
      return(MT_OK);

    case RD_WRITE_ONLY_W_IM_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+RETH_LEN+ImmDt_LEN;
      return(MT_OK);

    case RD_READ_REQ_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+RETH_LEN;
      return(MT_OK);

    case RD_READ_RESP_FIRST_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+AETH_LEN;
      return(MT_OK);

    case RD_READ_RESP_MIDDLE_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN;
      return(MT_OK);

    case RD_READ_RESP_LAST_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+AETH_LEN;
      return(MT_OK);

    case RD_READ_RESP_ONLY_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+AETH_LEN;
      return(MT_OK);

    case RD_ACKNOWLEDGE_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+AETH_LEN;
      return(MT_OK);

    case RD_ATOMIC_ACKNOWLEDGE_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+AETH_LEN+AtomAETH_LEN;
      return(MT_OK);

    case RD_CMP_SWAP_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+AtomETH_LEN;
      return(MT_OK);

    case RD_FETCH_ADD_OP:
      *packet_len+=LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+AtomETH_LEN;
      return(MT_OK);

/***********************************************/
/* Unreliable Datagram (UD)                    */
/***********************************************/

    case UD_SEND_ONLY_OP:
      *packet_len+=LRH_LEN+BTH_LEN+DETH_LEN;
      return(MT_OK);
    case UD_SEND_ONLY_W_IM_OP:
      *packet_len+=LRH_LEN+BTH_LEN+DETH_LEN+ImmDt_LEN;
      return(MT_OK);
    default:
      MTL_ERROR1("%s: Invalid Opcode (%d)\n", __func__, opcode);
      return(MT_ERROR);
      break;

  }
}

call_result_t
MPGA_make_fast(MPGA_headers_t *MPGA_headers_p,
               LNH_t LNH,
               u_int16_t payload_size,
               u_int8_t **packet_p_p)
{ IB_LRH_st *LRH=NULL;
  IB_BTH_st *BTH=NULL;
  u_int16_t packet_len;

  /* Calculating headers position*/
  LRH = (IB_LRH_st*)MPGA_headers_p;
  BTH = (IB_BTH_st*)MPGA_headers_p + (LNH == IBA_GLOBAL ? GRH_LEN : 0);

  LRH->LNH = LNH;
  /* Calculating pad_count*/
  BTH->PadCnt = (IBWORD-payload_size%IBWORD)%IBWORD;
  /* Calculating pkt_len*/
  MPGA_get_headers_size(BTH->OpCode,LNH,0,TRUE,FALSE,&packet_len);
  LRH->PktLen = (packet_len + payload_size + BTH->PadCnt) / IBWORD;

  return(MPGA_make_headers(MPGA_headers_p,BTH->OpCode,LNH,FALSE,FALSE,packet_p_p));

}                 
                                                

call_result_t 
MPGA_make_headers(MPGA_headers_t *MPGA_headers_p,   /*pointer to a headers union*/
                  IB_opcode_t opcode,
                  LNH_t LNH,
                  MT_bool CRC,
                  u_int16_t payload_size,
                  u_int8_t **packet_p_p)  /* pointer to packet buffer*/

{ 
  u_int8_t *start_ICRC;
  u_int16_t packet_len;
  u_int8_t *packet_p;

  packet_p=*packet_p_p;
  if ((LNH!=IBA_LOCAL) && (LNH!=IBA_GLOBAL)) return(MT_ERROR);  /*only IBA_LOCAL and IBA_GLOBAL are supported by now*/
  if (CRC && (!payload_size)) return(MT_ERROR); /*payload_size must be provided if CRC append is asked*/
  if (CRC && (LNH!=IBA_LOCAL && LNH!=IBA_GLOBAL)) return(MT_ERROR); /*CRC calculation is supported only with IBA_LOCAL or IBA_GLOBAL*/
  else start_ICRC = packet_p + payload_size;

  switch (LNH)
  {
    case IBA_LOCAL:
      switch (opcode)
      {

        /***********************************************/
        /* reliable Connection (RC)  	               */
        /***********************************************/
        
        case RC_SEND_FIRST_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_send_first.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_send_first.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case RC_SEND_MIDDLE_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_send_middle.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_send_middle.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case RC_SEND_LAST_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_send_last.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_send_last.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case RC_SEND_LAST_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_rc_send_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_send_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_send_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case RC_SEND_ONLY_OP:         
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_send_only.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_send_only.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case RC_SEND_ONLY_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_rc_send_only_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_send_only_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_send_only_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case RC_WRITE_FIRST_OP:
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_rc_write_first.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_write_first.IB_BTH),packet_p-RETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_write_first.IB_LRH),packet_p-RETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case RC_WRITE_MIDDLE_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_write_middle.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_write_middle.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case RC_WRITE_LAST_OP:          
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_write_last.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_write_last.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case RC_WRITE_LAST_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_rc_write_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_write_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_write_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case RC_WRITE_ONLY_OP:
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_rc_write_only.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_write_only.IB_BTH),packet_p-RETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_write_only.IB_LRH),packet_p-RETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case RC_WRITE_ONLY_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_rc_write_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_rc_write_only.IB_RETH),packet_p-ImmDt_LEN-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_write_only.IB_BTH),packet_p-ImmDt_LEN-RETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_write_only.IB_LRH),packet_p-ImmDt_LEN-RETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RETH_LEN+ImmDt_LEN);
          break;

        case RC_READ_REQ_OP:            
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_rc_read_req.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_read_req.IB_BTH),packet_p-RETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_read_req.IB_LRH),packet_p-RETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case RC_READ_RESP_FIRST_OP:
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_rc_read_res_first.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_read_res_first.IB_BTH),packet_p-AETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_read_res_first.IB_LRH),packet_p-AETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RC_READ_RESP_MIDDLE_OP:   
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_read_res_middle.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_read_res_middle.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RC_READ_RESP_LAST_OP:     
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_rc_read_res_last.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_read_res_last.IB_BTH),packet_p-AETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_read_res_last.IB_LRH),packet_p-AETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RC_READ_RESP_ONLY_OP:      
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_rc_read_res_only.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_read_res_only.IB_BTH),packet_p-AETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_read_res_only.IB_LRH),packet_p-AETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RC_ACKNOWLEDGE_OP:         
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_rc_ack.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_ack.IB_BTH),packet_p-AETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_ack.IB_LRH),packet_p-AETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+AETH_LEN);
          break;


        case RC_ATOMIC_ACKNOWLEDGE_OP:  
          MTL_ERROR1("%s: Unsupported packet type(opcode) (%d)\n", __func__, opcode);
          return(MT_ERROR);

        case RC_CMP_SWAP_OP:            
          MTL_ERROR1("%s: Unsupported packet type(opcode) (%d)\n", __func__, opcode);
          return(MT_ERROR);

        case RC_FETCH_ADD_OP:           
          MTL_ERROR1("%s: Unsupported packet type(opcode) (%d)\n", __func__, opcode);
          return(MT_ERROR);

/***********************************************/
/* Unreliable Connection (UC)                  */
/***********************************************/
        case UC_SEND_FIRST_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_send_first.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_send_first.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case UC_SEND_MIDDLE_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_send_middle.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_send_middle.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case UC_SEND_LAST_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_send_last.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_send_last.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case UC_SEND_LAST_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_uc_send_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_send_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_send_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case UC_SEND_ONLY_OP:         
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_send_only.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_send_only.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case UC_SEND_ONLY_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_uc_send_only_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_send_only_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_send_only_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case UC_WRITE_FIRST_OP:
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_uc_write_first.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_write_first.IB_BTH),packet_p-RETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_write_first.IB_LRH),packet_p-RETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case UC_WRITE_MIDDLE_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_write_middle.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_write_middle.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case UC_WRITE_LAST_OP:          
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_write_last.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_write_last.IB_LRH),packet_p-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN);
          break;

        case UC_WRITE_LAST_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_uc_write_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_write_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_write_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case UC_WRITE_ONLY_OP:
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_uc_write_only.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_write_only.IB_BTH),packet_p-RETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_write_only.IB_LRH),packet_p-RETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case UC_WRITE_ONLY_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_uc_write_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_uc_write_only.IB_RETH),packet_p-ImmDt_LEN-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_uc_write_only.IB_BTH),packet_p-ImmDt_LEN-RETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_uc_write_only.IB_LRH),packet_p-ImmDt_LEN-RETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RETH_LEN+ImmDt_LEN);
          break;

/***********************************************/
/* Reliable Datagram (RD)                      */
/***********************************************/

        case RD_SEND_FIRST_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_send_first.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_send_first.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_send_first.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_send_first.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_SEND_MIDDLE_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_send_middle.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_send_middle.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_send_middle.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_send_middle.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_SEND_LAST_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_send_last.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_send_last.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_send_last.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_send_last.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_SEND_LAST_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_rd_send_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_send_last_ImmDt.IB_DETH),packet_p-ImmDt_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_send_last_ImmDt.IB_RDETH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_send_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_send_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+ImmDt_LEN);
          break;

        case RD_SEND_ONLY_OP:         
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_send_only.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_send_only.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_send_only.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_send_only.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_SEND_ONLY_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_rd_send_only_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_send_only_ImmDt.IB_DETH),packet_p-ImmDt_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_send_only_ImmDt.IB_RDETH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_send_only_ImmDt.IB_BTH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_send_only_ImmDt.IB_LRH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+ImmDt_LEN);
          break;

        case RD_WRITE_FIRST_OP:
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_rd_write_first.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_write_first.IB_DETH),packet_p-RETH_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_write_first.IB_RDETH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_write_first.IB_BTH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_write_first.IB_LRH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+RETH_LEN);
          break;

        case RD_WRITE_MIDDLE_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_write_middle.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_write_middle.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_write_middle.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_write_middle.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_WRITE_LAST_OP:          
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_write_last.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_write_last.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_write_last.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_write_last.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_WRITE_LAST_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_rd_write_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_write_last_ImmDt.IB_DETH),packet_p-ImmDt_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_write_last_ImmDt.IB_RDETH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_write_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_write_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+ImmDt_LEN);
          break;

        case RD_WRITE_ONLY_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_write_only.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_write_only.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_write_only.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-RETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_write_only.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-RETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+RETH_LEN);
          break;

        case RD_WRITE_ONLY_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_rd_write_only_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_rd_write_only_ImmDt.IB_RETH),packet_p-ImmDt_LEN-RETH_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_write_only_ImmDt.IB_DETH),packet_p-ImmDt_LEN-RETH_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_write_only_ImmDt.IB_RDETH),packet_p-ImmDt_LEN-RETH_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_write_only_ImmDt.IB_BTH),packet_p-ImmDt_LEN-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_write_only_ImmDt.IB_LRH),packet_p-ImmDt_LEN-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+RETH_LEN+ImmDt_LEN);
          break;

        case RD_READ_REQ_OP:            
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_rd_read_req.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_rd_read_req.IB_DETH),packet_p-RETH_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_read_req.IB_RDETH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_read_req.IB_BTH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_read_req.IB_LRH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case RD_READ_RESP_FIRST_OP:
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_rd_read_res_first.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_read_res_first.IB_RDETH),packet_p-AETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_read_res_first.IB_BTH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_read_res_first.IB_LRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RD_READ_RESP_MIDDLE_OP:   
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_read_res_first.IB_RDETH),packet_p-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_read_res_middle.IB_BTH),packet_p-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_read_res_middle.IB_LRH),packet_p-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RD_READ_RESP_LAST_OP:     
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_rc_read_res_last.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_read_res_first.IB_RDETH),packet_p-AETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_read_res_last.IB_BTH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_read_res_last.IB_LRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RD_READ_RESP_ONLY_OP:      
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_rc_read_res_only.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_read_res_first.IB_RDETH),packet_p-AETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rc_read_res_only.IB_BTH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rc_read_res_only.IB_LRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RD_ACKNOWLEDGE_OP:
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_rd_ack.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_rd_ack.IB_RDETH),packet_p-AETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_rd_ack.IB_BTH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_rd_ack.IB_LRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+RDETH_LEN+AETH_LEN);
          break;
        case RD_ATOMIC_ACKNOWLEDGE_OP:
        case RD_CMP_SWAP_OP:
        case RD_FETCH_ADD_OP:
          MTL_ERROR1("%s: Unsupported packet type(opcode) (%d)\n", __func__, opcode);
          return(MT_ERROR);

/***********************************************/
/* Unreliable Datagram (UD)                    */
/***********************************************/

        case UD_SEND_ONLY_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_ud_send_only.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_ud_send_only.IB_BTH),packet_p-DETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_ud_send_only.IB_LRH),packet_p-DETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+DETH_LEN);
          break;

        case UD_SEND_ONLY_W_IM_OP:                           
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_ud_send_only_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_ud_send_only_ImmDt.IB_DETH),packet_p-ImmDt_LEN-DETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_ud_send_only_ImmDt.IB_BTH),packet_p-ImmDt_LEN-DETH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_ud_send_only_ImmDt.IB_LRH),packet_p-ImmDt_LEN-DETH_LEN-BTH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+BTH_LEN+DETH_LEN+ImmDt_LEN);
          break;

        default:
          MTL_ERROR1("%s: Invalid Opcode (%d)\n", __func__, opcode);
          return(MT_ERROR);
          break;

      }
      break;
    case IBA_GLOBAL:
      switch (opcode)
      {

        /***********************************************/
        /* reliable Connection (RC)  	               */
        /***********************************************/
        
        case RC_SEND_FIRST_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_send_first.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_send_first.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_send_first.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case RC_SEND_MIDDLE_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_send_middle.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_send_middle.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_send_middle.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case RC_SEND_LAST_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_send_last.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_send_last.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_send_last.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case RC_SEND_LAST_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_rc_send_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_send_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_send_last_ImmDt.IB_GRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_send_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case RC_SEND_ONLY_OP:         
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_send_only.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_send_only.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_send_only.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case RC_SEND_ONLY_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_rc_send_only_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_send_only_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_send_only_ImmDt.IB_GRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_send_only_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case RC_WRITE_FIRST_OP:
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_G_rc_write_first.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_write_first.IB_BTH),packet_p-RETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_write_first.IB_GRH),packet_p-RETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_write_first.IB_LRH),packet_p-RETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case RC_WRITE_MIDDLE_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_write_middle.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_write_middle.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_write_middle.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case RC_WRITE_LAST_OP:          
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_write_last.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_write_last.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_write_last.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case RC_WRITE_LAST_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_rc_write_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_write_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_write_last_ImmDt.IB_GRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_write_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case RC_WRITE_ONLY_OP:
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_G_rc_write_only.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_write_only.IB_BTH),packet_p-RETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_write_only.IB_GRH),packet_p-RETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_write_only.IB_LRH),packet_p-RETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case RC_WRITE_ONLY_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_rc_write_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_G_rc_write_only.IB_RETH),packet_p-ImmDt_LEN-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_write_only.IB_BTH),packet_p-ImmDt_LEN-RETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_write_only.IB_GRH),packet_p-ImmDt_LEN-RETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_write_only.IB_LRH),packet_p-ImmDt_LEN-RETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RETH_LEN+ImmDt_LEN);
          break;

        case RC_READ_REQ_OP:            
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_G_rc_read_req.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_read_req.IB_BTH),packet_p-RETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_read_req.IB_GRH),packet_p-RETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_read_req.IB_LRH),packet_p-RETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case RC_READ_RESP_FIRST_OP:
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_G_rc_read_res_first.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_read_res_first.IB_BTH),packet_p-AETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_read_res_first.IB_GRH),packet_p-AETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_read_res_first.IB_LRH),packet_p-AETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RC_READ_RESP_MIDDLE_OP:   
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_read_res_middle.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_read_res_middle.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_read_res_middle.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RC_READ_RESP_LAST_OP:     
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_G_rc_read_res_last.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_read_res_last.IB_BTH),packet_p-AETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_read_res_last.IB_GRH),packet_p-AETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_read_res_last.IB_LRH),packet_p-AETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RC_READ_RESP_ONLY_OP:      
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_G_rc_read_res_only.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_read_res_only.IB_BTH),packet_p-AETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_read_res_only.IB_GRH),packet_p-AETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_read_res_only.IB_LRH),packet_p-AETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RC_ACKNOWLEDGE_OP:         
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_G_rc_ack.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_ack.IB_BTH),packet_p-AETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_ack.IB_GRH),packet_p-AETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_ack.IB_LRH),packet_p-AETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+AETH_LEN);
          break;


        case RC_ATOMIC_ACKNOWLEDGE_OP:  
          MTL_ERROR1("%s: Unsupported packet type(opcode) (%d)\n", __func__, opcode);
          return(MT_ERROR);

        case RC_CMP_SWAP_OP:            
          MTL_ERROR1("%s: Unsupported packet type(opcode) (%d)\n", __func__, opcode);
          return(MT_ERROR);

        case RC_FETCH_ADD_OP:           
          MTL_ERROR1("%s: Unsupported packet type(opcode) (%d)\n", __func__, opcode);
          return(MT_ERROR);

/***********************************************/
/* Unreliable Connection (UC)                  */
/***********************************************/
        case UC_SEND_FIRST_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_send_first.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_send_first.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_send_first.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case UC_SEND_MIDDLE_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_send_middle.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_send_middle.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_send_middle.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case UC_SEND_LAST_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_send_last.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_send_last.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_send_last.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case UC_SEND_LAST_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_uc_send_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_send_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_send_last_ImmDt.IB_GRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_send_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case UC_SEND_ONLY_OP:         
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_send_only.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_send_only.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_send_only.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case UC_SEND_ONLY_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_uc_send_only_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_send_only_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_send_only_ImmDt.IB_GRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_send_only_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case UC_WRITE_FIRST_OP:
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_G_uc_write_first.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_write_first.IB_BTH),packet_p-RETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_write_first.IB_GRH),packet_p-RETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_write_first.IB_LRH),packet_p-RETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case UC_WRITE_MIDDLE_OP:
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_write_middle.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_write_middle.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_write_middle.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case UC_WRITE_LAST_OP:          
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_write_last.IB_BTH),packet_p-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_write_last.IB_GRH),packet_p-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_write_last.IB_LRH),packet_p-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN);
          break;

        case UC_WRITE_LAST_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_uc_write_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_write_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_write_last_ImmDt.IB_GRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_write_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+ImmDt_LEN);
          break;

        case UC_WRITE_ONLY_OP:
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_G_uc_write_only.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_write_only.IB_BTH),packet_p-RETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_write_only.IB_GRH),packet_p-RETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_write_only.IB_LRH),packet_p-RETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case UC_WRITE_ONLY_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_uc_write_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_G_uc_write_only.IB_RETH),packet_p-ImmDt_LEN-RETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_uc_write_only.IB_BTH),packet_p-ImmDt_LEN-RETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_uc_write_only.IB_GRH),packet_p-ImmDt_LEN-RETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_uc_write_only.IB_LRH),packet_p-ImmDt_LEN-RETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RETH_LEN+ImmDt_LEN);
          break;

/***********************************************/
/* Reliable Datagram (RD)                      */
/***********************************************/

        case RD_SEND_FIRST_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_send_first.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_send_first.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_send_first.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_send_first.IB_GRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_send_first.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_SEND_MIDDLE_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_send_middle.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_send_middle.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_send_middle.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_send_middle.IB_GRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_send_middle.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_SEND_LAST_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_send_last.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_send_last.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_send_last.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_send_last.IB_GRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_send_last.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_SEND_LAST_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_rd_send_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_send_last_ImmDt.IB_DETH),packet_p-ImmDt_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_send_last_ImmDt.IB_RDETH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_send_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_send_last_ImmDt.IB_GRH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_send_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+ImmDt_LEN);
          break;

        case RD_SEND_ONLY_OP:         
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_send_only.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_send_only.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_send_only.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_send_only.IB_GRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_send_only.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_SEND_ONLY_W_IM_OP:      
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_rd_send_only_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_send_only_ImmDt.IB_DETH),packet_p-ImmDt_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_send_only_ImmDt.IB_RDETH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_send_only_ImmDt.IB_BTH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_send_only_ImmDt.IB_GRH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_send_only_ImmDt.IB_LRH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+ImmDt_LEN);
          break;

        case RD_WRITE_FIRST_OP:
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_G_rd_write_first.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_write_first.IB_DETH),packet_p-RETH_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_write_first.IB_RDETH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_write_first.IB_BTH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_write_first.IB_GRH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_write_first.IB_LRH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+RETH_LEN);
          break;

        case RD_WRITE_MIDDLE_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_write_middle.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_write_middle.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_write_middle.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_write_middle.IB_GRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_write_middle.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_WRITE_LAST_OP:          
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_write_last.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_write_last.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_write_last.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_write_last.IB_GRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_write_last.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN);
          break;

        case RD_WRITE_LAST_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_rd_write_last_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_write_last_ImmDt.IB_DETH),packet_p-ImmDt_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_write_last_ImmDt.IB_RDETH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_write_last_ImmDt.IB_BTH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_write_last_ImmDt.IB_GRH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_write_last_ImmDt.IB_LRH),packet_p-ImmDt_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+ImmDt_LEN);
          break;

        case RD_WRITE_ONLY_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_write_only.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_write_only.IB_RDETH),packet_p-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_write_only.IB_BTH),packet_p-DETH_LEN-RDETH_LEN-RETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_write_only.IB_GRH),packet_p-DETH_LEN-RDETH_LEN-RETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_write_only.IB_LRH),packet_p-DETH_LEN-RDETH_LEN-RETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+RETH_LEN);
          break;

        case RD_WRITE_ONLY_W_IM_OP:     
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_rd_write_only_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_G_rd_write_only_ImmDt.IB_RETH),packet_p-ImmDt_LEN-RETH_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_write_only_ImmDt.IB_DETH),packet_p-ImmDt_LEN-RETH_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_write_only_ImmDt.IB_RDETH),packet_p-ImmDt_LEN-RETH_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_write_only_ImmDt.IB_BTH),packet_p-ImmDt_LEN-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_write_only_ImmDt.IB_GRH),packet_p-ImmDt_LEN-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-BTH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_write_only_ImmDt.IB_LRH),packet_p-ImmDt_LEN-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+DETH_LEN+RETH_LEN+ImmDt_LEN);
          break;

        case RD_READ_REQ_OP:            
          nMPGA_append_RETH(&(MPGA_headers_p->MPGA_G_rd_read_req.IB_RETH),packet_p-RETH_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_rd_read_req.IB_DETH),packet_p-RETH_LEN-DETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_read_req.IB_RDETH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_read_req.IB_BTH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_read_req.IB_GRH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_read_req.IB_LRH),packet_p-RETH_LEN-DETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RETH_LEN);
          break;

        case RD_READ_RESP_FIRST_OP:
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_G_rd_read_res_first.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_read_res_first.IB_RDETH),packet_p-AETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_read_res_first.IB_BTH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_read_res_first.IB_GRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_read_res_first.IB_LRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RD_READ_RESP_MIDDLE_OP:   
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_read_res_first.IB_RDETH),packet_p-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_read_res_middle.IB_BTH),packet_p-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_read_res_middle.IB_GRH),packet_p-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_read_res_middle.IB_LRH),packet_p-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RD_READ_RESP_LAST_OP:     
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_G_rc_read_res_last.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_read_res_first.IB_RDETH),packet_p-AETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_read_res_last.IB_BTH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_read_res_last.IB_GRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_read_res_last.IB_LRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RD_READ_RESP_ONLY_OP:      
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_G_rc_read_res_only.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_read_res_first.IB_RDETH),packet_p-AETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rc_read_res_only.IB_BTH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rc_read_res_only.IB_GRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rc_read_res_only.IB_LRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+AETH_LEN);
          break;

        case RD_ACKNOWLEDGE_OP:
          nMPGA_append_AETH(&(MPGA_headers_p->MPGA_G_rd_ack.IB_AETH),packet_p-AETH_LEN);
          nMPGA_append_RDETH(&(MPGA_headers_p->MPGA_G_rd_ack.IB_RDETH),packet_p-AETH_LEN-RDETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_rd_ack.IB_BTH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_rd_ack.IB_GRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_rd_ack.IB_LRH),packet_p-AETH_LEN-RDETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+RDETH_LEN+AETH_LEN);
          break;
        case RD_ATOMIC_ACKNOWLEDGE_OP:
        case RD_CMP_SWAP_OP:
        case RD_FETCH_ADD_OP:
          MTL_ERROR1("%s: Unsupported packet type(opcode) (%d)\n", __func__, opcode);
          return(MT_ERROR);

/***********************************************/
/* Unreliable Datagram (UD)                    */
/***********************************************/

        case UD_SEND_ONLY_OP:
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_ud_send_only.IB_DETH),packet_p-DETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_ud_send_only.IB_BTH),packet_p-DETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_ud_send_only.IB_GRH),packet_p-DETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_ud_send_only.IB_LRH),packet_p-DETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+DETH_LEN);
          break;

        case UD_SEND_ONLY_W_IM_OP:                           
          nMPGA_append_ImmDt(&(MPGA_headers_p->MPGA_G_ud_send_only_ImmDt.IB_ImmDt),packet_p-ImmDt_LEN);
          nMPGA_append_DETH(&(MPGA_headers_p->MPGA_G_ud_send_only_ImmDt.IB_DETH),packet_p-ImmDt_LEN-DETH_LEN);
          nMPGA_append_BTH(&(MPGA_headers_p->MPGA_G_ud_send_only_ImmDt.IB_BTH),packet_p-ImmDt_LEN-DETH_LEN-BTH_LEN);
          nMPGA_append_GRH(&(MPGA_headers_p->MPGA_G_ud_send_only_ImmDt.IB_GRH),packet_p-ImmDt_LEN-DETH_LEN-BTH_LEN-GRH_LEN);
          nMPGA_append_LRH(&(MPGA_headers_p->MPGA_G_ud_send_only_ImmDt.IB_LRH),packet_p-ImmDt_LEN-DETH_LEN-BTH_LEN-GRH_LEN-LRH_LEN);
          packet_p-=(LRH_LEN+GRH_LEN+BTH_LEN+DETH_LEN+ImmDt_LEN);
          break;

        default:
          MTL_ERROR1("%s: Invalid Opcode (%d)\n", __func__, opcode);
          return(MT_ERROR);
          break;

      }
    default:
      break;
  }
  if (CRC)
  {
    MPGA_get_headers_size(opcode,LNH,payload_size,FALSE,FALSE,&packet_len);
    append_ICRC((u_int16_t*)start_ICRC,fast_calc_ICRC(packet_len,(u_int16_t*)packet_p,LNH));
    append_VCRC((u_int16_t*)start_ICRC + ICRC_LEN,fast_calc_VCRC(packet_len,(u_int16_t*)packet_p));
  }
  *packet_p_p=packet_p;
  return(MT_OK);
}




//call_result_t MPGA_set_field(u_int8_t *packet, /*pointer to packet buffer*/
//                             MT_offset_t bit_offset,/*bit offset*/
//                             MT_size_t bit_size, /*bit size*/
//                             u_int32_t data)
//{
//  /* dividing into 3 parts:                             */
//  /* :01234567:01234567:01234567:01234567:              */
//  /* :   *****:        :        :        :   First part */
//  /* :        :********:********:        :   Second part*/
//  /* :        :        :        :**      :   Last part  */
//
//  u_int8_t length1,length2,length3;
//  u_int8_t offset1;
//  u_int8_t *address1,*address2,*address3;
//
//  /* Part One*/
//  length1 = (8-bit_offset%8)%8 <  bit_size ? (8-bit_offset%8)%8 : bit_size;
//  offset1 = bit_offset%8;
//  address1 = &(packet[bit_offset/8]);
//  if (length1>0) INSERTF(*address1,offset1,data,0,length1);
//  if (length1>=bit_size) return MT_OK; /*finished*/
//  data=data >> length1;
//  /* Part Two*/
//  length3 = (bit_size - length1) % 8;
//  length2 = bit_size - length1 - length3;
//  address2 = &(packet[(bit_offset/8)+ (length1&&1)]);
//  if(length2>0) memcpy(address2, &data,  length2/8);
//  if (length1+length2>=bit_size) return MT_OK; /*finished*/
//  data=data >> length2;
//  /* Part Three */
//  address3 = length2 > 0 ? (address2 + 1) : (address1 + 1);
//  INSERTF(*address3,0,data , 0,length3);
//  return MT_OK;
//               
//}

call_result_t 
MPGA_set_field(u_int8_t *packet, /*pointer to packet buffer*/
               MT_offset_t bit_offset,/*bit offset*/         
               MT_size_t bit_size, /*bit size*/              
               u_int32_t data)
{
  u_int32_t temp=0;
  u_int32_t bit_offset2;
  if ( (bit_size+bit_offset/32) > 31) return MT_ERROR;
  bit_offset2=(u_int32_t)(32-bit_offset%32-bit_size);

  temp=0;    /*this is done in order to avoid compile error of unused variable*/
#ifdef MT_LITTLE_ENDIAN
  temp=((u_int32_t*)packet)[bit_offset/32];
  temp=mswab32(temp);
  MT_INSERT32(temp,data,bit_offset2%32,bit_size);
  temp=mswab32(temp);
  ((u_int32_t*)packet)[bit_offset/32]=temp;
  return MT_OK;
#else
  MT_INSERT32(((u_int32_t*)packet)[bit_offset/32],data,bit_offset%32,bit_size);
  return MT_OK;
#endif
}


call_result_t 
MPGA_read_field(u_int8_t *packet, /*pointer to packet buffer*/
                MT_offset_t bit_offset,/*bit offset*/
                MT_size_t bit_size, /*bit size*/
                u_int32_t *data)
{
#ifdef MT_LITTLE_ENDIAN
  u_int32_t temp;
  if ( (bit_size+bit_offset/32) > 31) return MT_ERROR;
  temp=((u_int32_t*)packet)[bit_offset/32];
  temp=mswab32(temp);
  bit_offset=32-bit_offset%32-bit_size;
  *data=MT_EXTRACT32(temp,bit_offset%32,bit_size);
  return MT_OK;
#else
  if ( (bit_size+bit_offset/32) > 31) return MT_ERROR;
  *data=MT_EXTRACT32((packet[bit_offset/32]),bit_offset%32,bit_size);
  return MT_OK;
#endif
}

call_result_t 
MPGA_extract_LNH(u_int8_t *packet, /*pointer to packet buffer*/
                 LNH_t *LNH)
{
  if (packet) MPGA_read_field(packet,MT_BIT_OFFSET_SIZE(IB_LRH_p_t,LNH),LNH);
  else return(MT_ERROR);
  return(MT_OK);
}

call_result_t 
MPGA_get_BTH_offset(u_int8_t *packet,
                    u_int32_t *offset)
{
  LNH_t LNH;
  if (!packet) return(MT_ERROR);
  MPGA_extract_LNH(packet,&LNH);
  switch (LNH)
  {
    case RAW:           /* |LRH|... (Etertype)*/
      MTL_ERROR1("%s: Unsupported LNH (%d)\n", __func__, LNH);
      return(MT_ERROR);               
    case IP_NON_IBA_TRANS:  /* |LRH|GRH|...       */
      MTL_ERROR1("%s: Unsupported LNH (%d)\n", __func__, LNH);
      return(MT_ERROR);
    case IBA_LOCAL:         /* |LRH|BTH|...       */
      *offset = LRH_LEN*8;
      break;
    case IBA_GLOBAL:         /* |LRH|GRH|BTH|...   */
      *offset = LRH_LEN*8 + GRH_LEN*8;
      break;
    default:
      MTL_ERROR1("%s: Invalid LNH (%d)\n", __func__, LNH);
      return(MT_ERROR);
      break;
  }
  return(MT_OK); 
}

call_result_t 
MPGA_extract_opcode(u_int8_t *packet,
                    IB_opcode_t *opcode)
{
  u_int32_t BTH_offset;
  u_int32_t data=0;
  if (!packet) return(MT_ERROR);
  MPGA_get_BTH_offset(packet,&BTH_offset);
  MPGA_read_field(packet,MT_BIT_OFFSET(IB_BTH_p_t,OpCode)+BTH_offset,MT_BIT_SIZE(IB_BTH_p_t,OpCode),&data);
  *opcode=(IB_opcode_t)data;
  return(MT_OK); 
}

call_result_t 
MPGA_extract_PadCnt(u_int8_t *packet,
                    u_int8_t *PadCnt)
{
  u_int32_t BTH_offset;
  u_int32_t data=0;
  if (!packet) return(MT_ERROR);
  MPGA_get_BTH_offset(packet,&BTH_offset);
  MPGA_read_field(packet,MT_BIT_OFFSET(IB_BTH_p_t,OpCode)+BTH_offset,MT_BIT_SIZE(IB_BTH_p_t,OpCode),&data);
  *PadCnt=(u_int8_t)data;
  return(MT_OK); 
}

call_result_t 
MPGA_new_from_old(u_int8_t *old_packet,  /*pointer to the buffer where the old headers are*/
                  u_int8_t *new_packet,  /*pointer to the buffer where the new headers should be*/
                  u_int16_t buffer_size) /*total byte size allocated for headers starting from packet_p*/
/*will be used to avoid illegal memory access*/
{
  u_int16_t headers_size;
  IB_opcode_t opcode;
  LNH_t LNH;

  MPGA_extract_LNH(old_packet,&LNH);
  MPGA_extract_opcode(old_packet,&opcode);
  MPGA_get_headers_size(opcode,LNH,0,0,0,&headers_size);
  if (buffer_size<headers_size)
  {
    MTL_ERROR1("%s: buffer size is not sufficiant\n", __func__);
    return(MT_ERROR);  /*Not enough memory*/
  }
  memcpy(new_packet,old_packet,headers_size);
  return(MT_OK);
}                          
