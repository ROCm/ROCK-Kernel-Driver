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
#include <packet_append.h>
#include <internal_functions.h>


/*************************************************************************/
/*                            append lrh                                 */
/*************************************************************************/
call_result_t
append_LRH (IB_LRH_st *lrh_st_p, u_int16_t packet_size,
	    u_int16_t **packet_buf_vp,LNH_t LNH)
{
 u_int8_t  *start_LRH_p;
 u_int16_t **packet_buf_p;

 packet_buf_p = (u_int16_t**)packet_buf_vp; /*casting to u_int16_t */

/*Update the fields in the given lrh struct*/
 lrh_st_p->LNH    = LNH;
 lrh_st_p->PktLen  = (packet_size - VCRC_LEN) / IBWORD;
 /*from the firest byte of the LRH till the VCRC in 4 byte word*/
 lrh_st_p->reserved1 = 0;
 lrh_st_p->reserved2 = 0;

 start_LRH_p = (u_int8_t*)(*packet_buf_p) - LRH_LEN;

 start_LRH_p[0]  =  INSERTF(start_LRH_p[0],4,lrh_st_p->VL,0,4);
 start_LRH_p[0]  =  INSERTF(start_LRH_p[0],0,lrh_st_p->LVer,0,4);
 start_LRH_p[1]  =  INSERTF(start_LRH_p[1],4,lrh_st_p->SL,0,4);
 start_LRH_p[1]  =  INSERTF(start_LRH_p[1],2,lrh_st_p->reserved1,0,2);
 start_LRH_p[1]  =  INSERTF(start_LRH_p[1],0,lrh_st_p->LNH,0,2);
 start_LRH_p[2]  =  INSERTF(start_LRH_p[2],0,lrh_st_p->DLID,8,8);
 start_LRH_p[3]  =  INSERTF(start_LRH_p[3],0,lrh_st_p->DLID,0,8);
 start_LRH_p[4]  =  INSERTF(start_LRH_p[4],3,lrh_st_p->reserved2,0,5);
 start_LRH_p[4]  =  INSERTF(start_LRH_p[4],0,lrh_st_p->PktLen,8,3);
 start_LRH_p[5]  =  INSERTF(start_LRH_p[5],0,lrh_st_p->PktLen,0,8);
 start_LRH_p[6]  =  INSERTF(start_LRH_p[6],0,lrh_st_p->SLID,8,8);
 start_LRH_p[7]  =  INSERTF(start_LRH_p[7],0,lrh_st_p->SLID,0,8);

 (*packet_buf_p) = (u_int16_t*)start_LRH_p;

  return(MT_OK);
}

/*********************************************************************************/
/*                               Extract LRH                                     */
/*********************************************************************************/
call_result_t
extract_LRH(IB_LRH_st *lrh_st_p, u_int16_t **packet_buf_p)
{
 u_int8_t *start_LRH_p;
 u_int8_t *end_LRH_p;


 memset(lrh_st_p, 0, sizeof(IB_LRH_st));

 start_LRH_p = (u_int8_t*)(*packet_buf_p);
 end_LRH_p =   (u_int8_t*)(*packet_buf_p) + LRH_LEN;



   lrh_st_p->VL        =  INSERTF(lrh_st_p->VL,0,start_LRH_p[0],4,4);
   lrh_st_p->LVer      =  INSERTF(lrh_st_p->LVer,0,start_LRH_p[0],0,4);
   lrh_st_p->SL        =  INSERTF(lrh_st_p->SL,0,start_LRH_p[1],4,4);
   lrh_st_p->reserved1 =  INSERTF(lrh_st_p->reserved1,0,start_LRH_p[1],2,2);
   lrh_st_p->LNH       =  INSERTF(lrh_st_p->LNH,0,start_LRH_p[1],0,2);
   lrh_st_p->DLID      =  INSERTF(lrh_st_p->DLID,8,start_LRH_p[2],0,8);
   lrh_st_p->DLID      =  INSERTF(lrh_st_p->DLID,0,start_LRH_p[3],0,8);
   lrh_st_p->reserved2 =  INSERTF(lrh_st_p->reserved2,0,start_LRH_p[4],3,5);
   lrh_st_p->PktLen    =  INSERTF(lrh_st_p->PktLen,8,start_LRH_p[4],0,3);
   lrh_st_p->PktLen    =  INSERTF(lrh_st_p->PktLen,0,start_LRH_p[5],0,8);
   lrh_st_p->SLID      =  INSERTF(lrh_st_p->SLID,8,start_LRH_p[6],0,8);
   lrh_st_p->SLID      =  INSERTF(lrh_st_p->SLID,0,start_LRH_p[7],0,8);

 (*packet_buf_p) = (u_int16_t *)end_LRH_p;
 /*Updating  The packet_puf_p to be at the end of the LRH field*/
 return(MT_OK);
}


/*************************************************************************/
/*                            append grh                                 */
/*************************************************************************/
call_result_t
append_GRH (IB_GRH_st *grh_st_p, u_int16_t packet_size,
	        u_int16_t **packet_buf_vp)
{
 u_int8_t  *start_GRH_p;
 u_int16_t **packet_buf_p;

 packet_buf_p = (u_int16_t**)packet_buf_vp; /*casting to u_int16_t */

/*Update the fields in the given grh struct*/
 grh_st_p->NxtHdr  = NON_RAW_IBA; /* it is static for now */
 grh_st_p->PayLen  = packet_size - LRH_LEN - GRH_LEN - VCRC_LEN; 
 /*from the firest byte of the end of the GRH till the VCRC in bytes*/
  
 start_GRH_p = (u_int8_t*)(*packet_buf_p) - GRH_LEN;

 start_GRH_p[0]  =  INSERTF(start_GRH_p[0],4,grh_st_p->IPVer,0,4);
 start_GRH_p[0]  =  INSERTF(start_GRH_p[0],0,grh_st_p->TClass,4,4);
 start_GRH_p[1]  =  INSERTF(start_GRH_p[1],4,grh_st_p->TClass,0,4);
 start_GRH_p[1]  =  INSERTF(start_GRH_p[1],0,grh_st_p->FlowLabel,16,4);
 start_GRH_p[2]  =  INSERTF(start_GRH_p[2],0,grh_st_p->FlowLabel,8,8);
 start_GRH_p[3]  =  INSERTF(start_GRH_p[3],0,grh_st_p->FlowLabel,0,8);


 start_GRH_p[4]  =  INSERTF(start_GRH_p[4],0,grh_st_p->PayLen,8,8);
 start_GRH_p[5]  =  INSERTF(start_GRH_p[5],0,grh_st_p->PayLen,0,8);

 start_GRH_p[6]  =  grh_st_p->NxtHdr;
 start_GRH_p[7]  =  grh_st_p->HopLmt;

 memcpy(&(start_GRH_p[8]), grh_st_p->SGID,  sizeof(IB_gid_t));
 memcpy(&(start_GRH_p[24]), grh_st_p->DGID, sizeof(IB_gid_t));
 
 (*packet_buf_p) = (u_int16_t*)start_GRH_p;

  return(MT_OK);
}

/*********************************************************************************/
/*                               Extract GRH                                     */
/*********************************************************************************/
call_result_t
extract_GRH(IB_GRH_st *grh_st_p, u_int16_t **packet_buf_p)
{
 u_int8_t *start_GRH_p;
 u_int8_t *end_GRH_p;


 memset(grh_st_p, 0, sizeof(IB_GRH_st));

 start_GRH_p = (u_int8_t*)(*packet_buf_p);
 end_GRH_p =   (u_int8_t*)(*packet_buf_p) + GRH_LEN;

   grh_st_p->IPVer     =  INSERTF(grh_st_p->IPVer,0,start_GRH_p[0],4,4);
   grh_st_p->TClass    =  INSERTF(grh_st_p->TClass,4,start_GRH_p[0],0,4);
   grh_st_p->TClass    =  INSERTF(grh_st_p->TClass,0,start_GRH_p[1],4,4);
   grh_st_p->FlowLabel =  INSERTF(grh_st_p->FlowLabel,16,start_GRH_p[1],0,4);
   grh_st_p->FlowLabel =  INSERTF(grh_st_p->FlowLabel,8,start_GRH_p[2],0,8);
   grh_st_p->FlowLabel =  INSERTF(grh_st_p->FlowLabel,0,start_GRH_p[3],0,8);

   grh_st_p->PayLen    =  INSERTF(grh_st_p->PayLen,8,start_GRH_p[4],0,8);
   grh_st_p->PayLen    =  INSERTF(grh_st_p->PayLen,0,start_GRH_p[5],0,8);

   grh_st_p->NxtHdr = start_GRH_p[6];
   grh_st_p->HopLmt = start_GRH_p[7];  

   memcpy(grh_st_p->SGID, &(start_GRH_p[8]),   sizeof(IB_gid_t));
   memcpy(grh_st_p->DGID, &(start_GRH_p[24]),  sizeof(IB_gid_t));

   
   (*packet_buf_p) = (u_int16_t *)end_GRH_p;
 /*Updating  The packet_puf_p to be at the end of the GRH field*/
 return(MT_OK);
}



/*********************************************************************************/
/*                               Append BTH                                      */
/*********************************************************************************/
call_result_t
append_BTH (IB_BTH_st *bth_st_p, u_int16_t **packet_buf_p,u_int16_t payload_size)
{
 u_int8_t *start_BTH_p;

 start_BTH_p = (u_int8_t*)(*packet_buf_p) - BTH_LEN;
 /*Assuming that the pointer is BTH_LEN (12) bytes ahead*/
 bth_st_p->PadCnt = (IBWORD - (payload_size % IBWORD)) % IBWORD; /*To align to 4 byte boundary*/
 bth_st_p->reserved1 = 0 ;
 bth_st_p->reserved2 = 0 ;
 bth_st_p->TVer = IBA_TRANSPORT_HEADER_VERSION;

 start_BTH_p[0]  =  INSERTF(start_BTH_p[0],0,bth_st_p->OpCode,0,8);
 start_BTH_p[1]  =  INSERTF(start_BTH_p[1],7,bth_st_p->SE,0,1);
 start_BTH_p[1]  =  INSERTF(start_BTH_p[1],6,bth_st_p->M,0,1);
 start_BTH_p[1]  =  INSERTF(start_BTH_p[1],4,bth_st_p->PadCnt,0,2);
 start_BTH_p[1]  =  INSERTF(start_BTH_p[1],0,bth_st_p->TVer,0,4);
 start_BTH_p[2]  =  INSERTF(start_BTH_p[2],0,bth_st_p->P_KEY,8,8);
 start_BTH_p[3]  =  INSERTF(start_BTH_p[3],0,bth_st_p->P_KEY,0,8);
 start_BTH_p[4]  =  INSERTF(start_BTH_p[4],0,bth_st_p->reserved1,0,8);
 start_BTH_p[5]  =  INSERTF(start_BTH_p[5],0,bth_st_p->DestQP,16,8);
 start_BTH_p[6]  =  INSERTF(start_BTH_p[6],0,bth_st_p->DestQP,8,8);
 start_BTH_p[7]  =  INSERTF(start_BTH_p[7],0,bth_st_p->DestQP,0,8);
 start_BTH_p[8]  =  INSERTF(start_BTH_p[8],7,bth_st_p->A,0,1);
 start_BTH_p[8]  =  INSERTF(start_BTH_p[8],0,bth_st_p->reserved2,0,7);
 start_BTH_p[9]  =  INSERTF(start_BTH_p[9],0,bth_st_p->PSN,16,8);
 start_BTH_p[10] =  INSERTF(start_BTH_p[10],0,bth_st_p->PSN,8,8);
 start_BTH_p[11] =  INSERTF(start_BTH_p[11],0,bth_st_p->PSN,0,8);
 (*packet_buf_p)  = (u_int16_t*)start_BTH_p;/*Update the pointer to the start of the BTH field*/
 return(MT_OK);
}

/*********************************************************************************/
/*                               Extract BTH                                     */
/*********************************************************************************/
call_result_t
extract_BTH(IB_BTH_st *bth_st_p, u_int16_t **packet_buf_p)
{
 u_int8_t *start_BTH_p;
 u_int8_t *end_BTH_p;


 memset(bth_st_p, 0, sizeof(IB_BTH_st));

 start_BTH_p = (u_int8_t*)(*packet_buf_p);
 end_BTH_p =   (u_int8_t*)(*packet_buf_p) + BTH_LEN;

   bth_st_p->OpCode    =  INSERTF(bth_st_p->OpCode,0,start_BTH_p[0],0,8);
   bth_st_p->SE        =  INSERTF(bth_st_p->SE,0,start_BTH_p[1],7,1);
   bth_st_p->M         =  INSERTF(bth_st_p->M,0,start_BTH_p[1],6,1);
   bth_st_p->PadCnt    =  INSERTF(bth_st_p->PadCnt,0,start_BTH_p[1],4,2);
   bth_st_p->TVer      =  INSERTF(bth_st_p->TVer,0,start_BTH_p[1],0,4);
   bth_st_p->P_KEY     =  INSERTF(bth_st_p->P_KEY,8,start_BTH_p[2],0,8);
   bth_st_p->P_KEY     =  INSERTF(bth_st_p->P_KEY,0,start_BTH_p[3],0,8);
   bth_st_p->reserved1 =  INSERTF(bth_st_p->reserved1,0,start_BTH_p[4],0,8);
   bth_st_p->DestQP    =  INSERTF(bth_st_p->DestQP,16,start_BTH_p[5],0,8);
   bth_st_p->DestQP    =  INSERTF(bth_st_p->DestQP,8,start_BTH_p[6],0,8);
   bth_st_p->DestQP    =  INSERTF(bth_st_p->DestQP,0,start_BTH_p[7],0,8);
   bth_st_p->A         =  INSERTF(bth_st_p->A,0,start_BTH_p[8],7,1);
   bth_st_p->reserved2 =  INSERTF(bth_st_p->reserved2,0,start_BTH_p[8],0,7);
   bth_st_p->PSN       =  INSERTF(bth_st_p->PSN,16,start_BTH_p[9],0,8);
   bth_st_p->PSN       =  INSERTF(bth_st_p->PSN,8,start_BTH_p[10],0,8);
   bth_st_p->PSN       =  INSERTF(bth_st_p->PSN,0,start_BTH_p[11],0,8);

 (*packet_buf_p) = (u_int16_t *)end_BTH_p;
 /*Updating  The packet_puf_p to be at the end of the DETH field*/
 return(MT_OK);
}


/*********************************************************************************/
/*                               Append RETH                                     */
/*********************************************************************************/
call_result_t
append_RETH (IB_RETH_st *reth_st_p, u_int16_t **packet_buf_p)
{
 u_int8_t *start_RETH_p;
 u_int8_t *start_VA_p;
 u_int8_t *start_R_Key_p;
 u_int8_t *start_DMALen_p;

 start_RETH_p =  (u_int8_t*)(*packet_buf_p) - RETH_LEN;

 start_VA_p     = start_RETH_p;/*The first field*/
 start_R_Key_p  = start_RETH_p + 8; /*1st field 8 byte long*/
 start_DMALen_p = start_RETH_p + 12;/*2nd fiels 4 byte + 12 1st*/

 (*((u_int64_t*)start_VA_p))      = MOSAL_cpu_to_be64(reth_st_p->VA); /*64bit field (big endian)*/
 (*((u_int32_t*)start_R_Key_p))   = MOSAL_cpu_to_be32(reth_st_p->R_Key);/*32bit field (big endian)*/
 (*((u_int32_t*)start_DMALen_p))  = MOSAL_cpu_to_be32(reth_st_p->DMALen);/*32bit field (big e)*/

 (*packet_buf_p) = (u_int16_t *)start_RETH_p;
 /*Updating  The packet_puf_p to be at the start of the RETH field*/
 return(MT_OK);
}

/*********************************************************************************/
/*                               Extract RETH                                    */
/*********************************************************************************/
call_result_t
extract_RETH(IB_RETH_st *reth_st_p, u_int16_t **packet_buf_p)
{
 u_int8_t *start_RETH_p;
 u_int8_t *start_VA_p;
 u_int8_t *start_R_Key_p;
 u_int8_t *start_DMALen_p;
 u_int8_t *end_RETH_p;

 memset(reth_st_p, 0, sizeof(IB_RETH_st));

 start_RETH_p   = (u_int8_t*)(*packet_buf_p);
 end_RETH_p     = (u_int8_t*)(*packet_buf_p) + RETH_LEN;
 start_VA_p     = start_RETH_p;/*The first field*/
 start_R_Key_p  = start_RETH_p + 8; /*1st field 8 byte long*/
 start_DMALen_p = start_RETH_p + 12;/*2nd fiels 4 byte + 12 1st*/

 reth_st_p->VA      = MOSAL_be64_to_cpu(*((u_int64_t*)start_VA_p));     /*64bit field(big endain)*/
 reth_st_p->R_Key   = MOSAL_be32_to_cpu(*((u_int32_t*)start_R_Key_p));  /*32bit field(big endain)*/
 reth_st_p->DMALen  = MOSAL_be32_to_cpu(*((u_int32_t*)start_DMALen_p)); /*32bit field(big endain)*/

 (*packet_buf_p) = (u_int16_t *)end_RETH_p;
 /*Updating  The packet_puf_p to be at the end of the DETH field*/
 return(MT_OK);
}
/*********************************************************************************/
/*                               Append AETH                                     */
/*********************************************************************************/
call_result_t
append_AETH (IB_AETH_st *aeth_st_p, u_int16_t **packet_buf_p)
{
 u_int8_t *start_AETH_p;

 start_AETH_p =  (u_int8_t*)(*packet_buf_p) - AETH_LEN;

 start_AETH_p[0]  =  INSERTF(start_AETH_p[0],0,aeth_st_p->Syndrome,0,8);/*8 bitf (big endain)*/

 start_AETH_p[1]  =  INSERTF(start_AETH_p[1],0,aeth_st_p->MSN,16,8);/*24 bitf (big endain)*/
 start_AETH_p[2]  =  INSERTF(start_AETH_p[2],0,aeth_st_p->MSN,8,8); /*it is dangerus to use bm*/
 start_AETH_p[3]  =  INSERTF(start_AETH_p[3],0,aeth_st_p->MSN,0,8);

 (*packet_buf_p) = (u_int16_t *)start_AETH_p;
 /*Updating  The packet_puf_p to be at the start of the AETH field*/
 return(MT_OK);
}

/*********************************************************************************/
/*                               Extract AETH                                    */
/*********************************************************************************/
call_result_t
extract_AETH (IB_AETH_st *aeth_st_p, u_int16_t **packet_buf_p)
{
 u_int8_t *start_AETH_p;
 u_int8_t *end_AETH_p;
 u_int8_t *start_MSN_p;
 u_int32_t temp32;

 memset(aeth_st_p, 0, sizeof(IB_AETH_st)); 
 
 start_AETH_p = (u_int8_t*)(*packet_buf_p);
 start_MSN_p  = start_AETH_p + 1;/*2nd field 1 byte after Syndrome*/
 end_AETH_p   = (u_int8_t*)(*packet_buf_p) + AETH_LEN;

 aeth_st_p->Syndrome  = start_AETH_p[0];/*8bit field (big endian)*/

 temp32 = *((u_int32_t*)start_MSN_p);/*24bit field (big endian)*/

#ifdef MT_LITTLE_ENDIAN
 temp32 <<= 8;
 
#else
 temp32 >>= 8;
#endif

 aeth_st_p->MSN = MOSAL_be32_to_cpu(temp32);

 (*packet_buf_p) = (u_int16_t *)end_AETH_p;
 /*Updating  The packet_puf_p to be at the start of the AETH field*/
 return(MT_OK);
}

/*********************************************************************************/
/*                               Append DETH                                     */
/*********************************************************************************/
call_result_t
append_DETH (IB_DETH_st *deth_st_p, u_int16_t **packet_buf_p)
{
 u_int8_t *start_DETH_p;
 u_int8_t *start_Q_Key_p;

 start_DETH_p =  (u_int8_t*)(*packet_buf_p) - DETH_LEN;
 start_Q_Key_p = start_DETH_p;

 (*((u_int32_t*)start_Q_Key_p)) = MOSAL_cpu_to_be32(deth_st_p->Q_Key); /*32bit field (big endian)*/

 start_DETH_p[4] = deth_st_p->reserved1;/*8bit field (big endian)*/

 start_DETH_p[5]  =  INSERTF(start_DETH_p[5],0,deth_st_p->SrcQP,16,8);/*24bit field (big endian)*/
 start_DETH_p[6]  =  INSERTF(start_DETH_p[6],0,deth_st_p->SrcQP,8,8);
 start_DETH_p[7]  =  INSERTF(start_DETH_p[7],0,deth_st_p->SrcQP,0,8);

 (*packet_buf_p) = (u_int16_t *)start_DETH_p;
 /*Updating  The packet_puf_p to be at the start of the DETH field*/
 return(MT_OK);
}

/*********************************************************************************/
/*                               Extract DETH                                    */
/*********************************************************************************/
call_result_t
extract_DETH(IB_DETH_st *deth_st_p, u_int16_t **packet_buf_p)
{
 u_int8_t  *start_DETH_p;
 u_int8_t  *end_DETH_p;
 u_int8_t  *start_SrcQP_p;
 u_int32_t temp32;

 memset(deth_st_p, 0, sizeof(IB_DETH_st)); 


 start_DETH_p = (u_int8_t*)(*packet_buf_p);
 start_SrcQP_p = start_DETH_p + 5;  /*The start of the SrcQP field*/
 end_DETH_p =   start_DETH_p + DETH_LEN;

 deth_st_p->Q_Key =  MOSAL_be32_to_cpu(*((u_int32_t*)start_DETH_p));/*32bit field (big endian)*/
 deth_st_p->reserved1 =  *((u_int8_t*)(start_DETH_p + 4));/*8bit field (big endian)*/

 temp32 = *((u_int32_t*)start_SrcQP_p); /*24bit field (big endian)*/

#ifdef MT_LITTLE_ENDIAN
 MTL_TRACE('5', "\nLittle Endian\n"); 
 temp32 <<= 8;
#else
 temp32 >>= 8;
 MTL_TRACE('5', "\nBig Endian \n"); 
#endif

 deth_st_p->SrcQP =  MOSAL_be32_to_cpu(temp32);

 (*packet_buf_p) = (u_int16_t *)end_DETH_p;
 /*Updating  The packet_puf_p to be at the end of the DETH field*/
 return(MT_OK);
}

/*********************************************************************************/
/*                               Append ImmDt                                    */
/*********************************************************************************/
call_result_t
append_ImmDt (IB_ImmDt_st *ImmDt_st_p, u_int16_t **packet_buf_p)
{
 u_int8_t *start_ImmDt_p;
 
 start_ImmDt_p =  (u_int8_t*)(*packet_buf_p) - ImmDt_LEN;
 (*((u_int32_t*)start_ImmDt_p)) = MOSAL_cpu_to_be32(ImmDt_st_p->ImmDt); /*32bit field (big endian)*/
 
 (*packet_buf_p) = (u_int16_t *)start_ImmDt_p;
 /*Updating  The packet_puf_p to be at the start of the ImmDt field*/
 return(MT_OK);
}
 
/*********************************************************************************/
/*                               Extract ImmDt                                   */
/*********************************************************************************/
call_result_t
extract_ImmDt(IB_ImmDt_st *ImmDt_st_p, u_int16_t **packet_buf_p)
{
 u_int8_t  *start_ImmDt_p;
 u_int8_t  *end_ImmDt_p;

  memset(ImmDt_st_p, 0, sizeof(IB_ImmDt_st)); 
 
 start_ImmDt_p = (u_int8_t*)(*packet_buf_p);
 end_ImmDt_p =   start_ImmDt_p + ImmDt_LEN;
 
 ImmDt_st_p->ImmDt =  MOSAL_be32_to_cpu(*((u_int32_t*)start_ImmDt_p));/*32bit field (big endian)*/
 
 (*packet_buf_p) = (u_int16_t *)end_ImmDt_p;
 /*Updating  The packet_puf_p to be at the end of the ImmDt field*/
 return(MT_OK);
}                                                                         

/*********************************************************************************/
/*                               Append ICRC                                     */
/*********************************************************************************/
call_result_t
append_ICRC(u_int16_t *start_ICRC, u_int32_t ICRC)
{
 *((u_int32_t*)start_ICRC) = MOSAL_cpu_to_le32(ICRC);
  return(MT_OK);
}

/*********************************************************************************/
/*                               Extract ICRC                                    */
/*********************************************************************************/
call_result_t
extract_ICRC(u_int16_t *start_ICRC, u_int32_t *ICRC)
{
  *ICRC = MOSAL_le32_to_cpu(*((u_int32_t*)start_ICRC));
  return(MT_OK);
}

/*********************************************************************************/
/*                               Append VCRC                                     */
/*********************************************************************************/
call_result_t
append_VCRC(u_int16_t *start_VCRC, u_int16_t VCRC)
{
 *((u_int16_t*)start_VCRC) = MOSAL_cpu_to_le16(VCRC);
  return(MT_OK);
}

/*********************************************************************************/
/*                               Extract VCRC                                    */
/*********************************************************************************/
call_result_t
extract_VCRC(u_int16_t *start_VCRC, u_int16_t *VCRC)
{
  *VCRC = MOSAL_le16_to_cpu(*((u_int16_t*)start_VCRC));
  return(MT_OK);
}
