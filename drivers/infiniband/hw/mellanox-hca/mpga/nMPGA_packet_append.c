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



#include <bit_ops.h>

/* Layers Include */
#include <mtl_common.h>
#ifdef __WIN__
#include <mosal.h>
#endif
#include <ib_defs.h>

/* MPGA Includes */ 
#include "nMPGA_packet_append.h"
#include <packet_append.h>
#include <internal_functions.h>



/*************************************************************************/
/*                            nMPGA_append lrh                           */
/*************************************************************************/
call_result_t
nMPGA_append_LRH (IB_LRH_st *lrh_st_p, 
	        u_int8_t *start_LRH_p)
{
 INSERTF(start_LRH_p[0],4,lrh_st_p->VL,0,4);
 INSERTF(start_LRH_p[0],0,lrh_st_p->LVer,0,4);
 INSERTF(start_LRH_p[1],4,lrh_st_p->SL,0,4);
 INSERTF(start_LRH_p[1],2,lrh_st_p->reserved1,0,2);
 INSERTF(start_LRH_p[1],0,lrh_st_p->LNH,0,2);
 INSERTF(start_LRH_p[2],0,lrh_st_p->DLID,8,8);
 INSERTF(start_LRH_p[3],0,lrh_st_p->DLID,0,8);
 INSERTF(start_LRH_p[4],3,lrh_st_p->reserved2,0,5);
 INSERTF(start_LRH_p[4],0,lrh_st_p->PktLen,8,3);
 INSERTF(start_LRH_p[5],0,lrh_st_p->PktLen,0,8);
 INSERTF(start_LRH_p[6],0,lrh_st_p->SLID,8,8);
 INSERTF(start_LRH_p[7],0,lrh_st_p->SLID,0,8);
 return(MT_OK);
}

/*************************************************************************/
/*                            nMPGA_append grh                           */
/*************************************************************************/
call_result_t
nMPGA_append_GRH (IB_GRH_st *grh_st_p,
	        u_int8_t *start_GRH_p)
{
 INSERTF(start_GRH_p[0],4,grh_st_p->IPVer,0,4);
 INSERTF(start_GRH_p[0],0,grh_st_p->TClass,4,4);
 INSERTF(start_GRH_p[1],4,grh_st_p->TClass,0,4);
 INSERTF(start_GRH_p[1],0,grh_st_p->FlowLabel,16,4);
 INSERTF(start_GRH_p[2],0,grh_st_p->FlowLabel,8,8);
 INSERTF(start_GRH_p[3],0,grh_st_p->FlowLabel,0,8);
 INSERTF(start_GRH_p[4],0,grh_st_p->PayLen,8,8);
 INSERTF(start_GRH_p[5],0,grh_st_p->PayLen,0,8);
 start_GRH_p[6]  =  grh_st_p->NxtHdr;
 start_GRH_p[7]  =  grh_st_p->HopLmt;
 memcpy(&(start_GRH_p[8]), grh_st_p->SGID,  sizeof(IB_gid_t));
 memcpy(&(start_GRH_p[24]), grh_st_p->DGID, sizeof(IB_gid_t));
 return(MT_OK);
}
/*********************************************************************************/
/*                               nMPGA_append BTH                                */
/*********************************************************************************/
call_result_t
nMPGA_append_BTH (IB_BTH_st *bth_st_p, u_int8_t *start_BTH_p)
{
 INSERTF(start_BTH_p[0],0,bth_st_p->OpCode,0,8);
 INSERTF(start_BTH_p[1],7,bth_st_p->SE,0,1);
 INSERTF(start_BTH_p[1],6,bth_st_p->M,0,1);
 INSERTF(start_BTH_p[1],4,bth_st_p->PadCnt,0,2);
 INSERTF(start_BTH_p[1],0,bth_st_p->TVer,0,4);
 INSERTF(start_BTH_p[2],0,bth_st_p->P_KEY,8,8);
 INSERTF(start_BTH_p[3],0,bth_st_p->P_KEY,0,8);
 INSERTF(start_BTH_p[4],0,bth_st_p->reserved1,0,8);
 INSERTF(start_BTH_p[5],0,bth_st_p->DestQP,16,8);
 INSERTF(start_BTH_p[6],0,bth_st_p->DestQP,8,8);
 INSERTF(start_BTH_p[7],0,bth_st_p->DestQP,0,8);
 INSERTF(start_BTH_p[8],7,bth_st_p->A,0,1);
 INSERTF(start_BTH_p[8],0,bth_st_p->reserved2,0,7);
 INSERTF(start_BTH_p[9],0,bth_st_p->PSN,16,8);
 INSERTF(start_BTH_p[10],0,bth_st_p->PSN,8,8);
 INSERTF(start_BTH_p[11],0,bth_st_p->PSN,0,8);
 return(MT_OK);
}

/*********************************************************************************/
/*                               nMPGA_append RETH                               */
/*********************************************************************************/
call_result_t
nMPGA_append_RETH (IB_RETH_st *reth_st_p, u_int8_t *start_RETH_p)
{
 u_int8_t *start_VA_p;
 u_int8_t *start_R_Key_p;
 u_int8_t *start_DMALen_p;

 start_VA_p     = start_RETH_p;/*The first field*/
 start_R_Key_p  = start_RETH_p + 8; /*1st field 8 byte long*/
 start_DMALen_p = start_RETH_p + 12;/*2nd fiels 4 byte + 12 1st*/

 (*((u_int64_t*)start_VA_p))      = MOSAL_cpu_to_be64(reth_st_p->VA); /*64bit field (big endian)*/
 (*((u_int32_t*)start_R_Key_p))   = MOSAL_cpu_to_be32(reth_st_p->R_Key);/*32bit field (big endian)*/
 (*((u_int32_t*)start_DMALen_p))  = MOSAL_cpu_to_be32(reth_st_p->DMALen);/*32bit field (big e)*/
 return(MT_OK);
}

/*********************************************************************************/
/*                               nMPGA_append AETH                               */
/*********************************************************************************/
call_result_t
nMPGA_append_AETH (IB_AETH_st *aeth_st_p, u_int8_t *start_AETH_p)
{
 INSERTF(start_AETH_p[0],0,aeth_st_p->Syndrome,0,8);/*8 bitf (big endain)*/
 INSERTF(start_AETH_p[1],0,aeth_st_p->MSN,16,8);/*24 bitf (big endain)*/
 INSERTF(start_AETH_p[2],0,aeth_st_p->MSN,8,8); /*it is dangerus to use bm*/
 INSERTF(start_AETH_p[3],0,aeth_st_p->MSN,0,8);
 return(MT_OK);
}

/*********************************************************************************/
/*                               nMPGA_append RDETH                               */
/*********************************************************************************/
call_result_t
nMPGA_append_RDETH (IB_RDETH_st *rdeth_st_p, u_int8_t *start_RDETH_p)
{
 start_RDETH_p[0] = rdeth_st_p->reserved1;/*8bit field (big endian)*/
 INSERTF(start_RDETH_p[1],0,rdeth_st_p->EECnxt,16,8);/*24bit field (big endian)*/
 INSERTF(start_RDETH_p[2],0,rdeth_st_p->EECnxt,8,8);
 INSERTF(start_RDETH_p[3],0,rdeth_st_p->EECnxt,0,8);
 return(MT_OK);
}

/*********************************************************************************/
/*                               nMPGA_append DETH                               */
/*********************************************************************************/
call_result_t
nMPGA_append_DETH (IB_DETH_st *deth_st_p, u_int8_t *start_DETH_p)
{
 u_int8_t *start_Q_Key_p;

 start_Q_Key_p = start_DETH_p;

 (*((u_int32_t*)start_Q_Key_p)) = MOSAL_cpu_to_be32(deth_st_p->Q_Key); /*32bit field (big endian)*/
 start_DETH_p[4] = deth_st_p->reserved1;/*8bit field (big endian)*/
 INSERTF(start_DETH_p[5],0,deth_st_p->SrcQP,16,8);/*24bit field (big endian)*/
 INSERTF(start_DETH_p[6],0,deth_st_p->SrcQP,8,8);
 INSERTF(start_DETH_p[7],0,deth_st_p->SrcQP,0,8);
 return(MT_OK);
}
/*********************************************************************************/
/*                               nMPGA_append ImmDt                              */
/*********************************************************************************/
call_result_t
nMPGA_append_ImmDt (IB_ImmDt_st *ImmDt_st_p, u_int8_t *start_ImmDt_p)
{
 (*((u_int32_t*)start_ImmDt_p)) = MOSAL_cpu_to_be32(ImmDt_st_p->ImmDt); /*32bit field (big endian)*/
 return(MT_OK);
}
 
/*********************************************************************************/
/*                               nMPGA_append ICRC                               */
/*********************************************************************************/
call_result_t
nMPGA_append_ICRC(u_int16_t *start_ICRC, u_int32_t ICRC)
{
 *((u_int32_t*)start_ICRC) = MOSAL_cpu_to_le32(ICRC);
  return(MT_OK);
}

/*********************************************************************************/
/*                               nMPGA_append VCRC                               */
/*********************************************************************************/
call_result_t
nMPGA_append_VCRC(u_int16_t *start_VCRC, u_int16_t VCRC)
{
 *((u_int16_t*)start_VCRC) = MOSAL_cpu_to_le16(VCRC);
  return(MT_OK);
}
