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


/************************************/


#ifdef __LINUX__
#ifdef MT_KERNEL
#include <linux/module.h>
//#include <linux/modversions.h>
#endif
#endif


#include <mpga.h>
#include <ib_opcodes.h>
#include <packet_append.h>
#include <internal_functions.h>
#include <bit_ops.h>

/************************************/

#ifndef VXWORKS_OS
#ifdef __WIN__
#define MLOCK(__buff, __size) 0 
#else

#ifndef MT_KERNEL 
#include <sys/mman.h> /* for mlock function */
#define MLOCK(__buff, __size) mlock(__buff, __size)
#else
#define MLOCK(__buff, __size) 0 
#endif

#endif

#else /* VXWORKS_OS */
#define MLOCK(__buff, __size) 0 
#endif /* VXWORKS_OS */


/*********************************************************************************/
/*                             build  packet with lrh                            */
/*********************************************************************************/
call_result_t
MPGA_build_pkt_lrh (IB_LRH_st *lrh_st_p, u_int16_t t_packet_size, void *t_packet_buf_vp,
                    u_int16_t *packet_size_p, void **packet_buf_vp,LNH_t LNH)
{
 u_int8_t  *start_LRH_p;
 u_int16_t TCRC_packet_size = 0;/*This arg will be send to the allocate function*/
 u_int32_t ICRC = 0;              /*for making space for the crc fileds*/
 u_int16_t VCRC = 0;
 u_int8_t  *start_ICRC_p;
 u_int8_t  *start_VCRC_p;
 u_int16_t **packet_buf_p;
 u_int16_t *t_packet_buf_p;
 u_int8_t  align = 0;

 packet_buf_p = (u_int16_t**)packet_buf_vp; /*casting to u_int16_t */
 t_packet_buf_p = (u_int16_t*)t_packet_buf_vp;

 if(LNH == IBA_LOCAL) TCRC_packet_size = t_packet_size + ICRC_LEN + VCRC_LEN;
 else{
  if(LNH == RAW) align = (4 - (t_packet_size % IBWORD)) % IBWORD; /*should be RAW packet at this stage*/
  TCRC_packet_size = t_packet_size + VCRC_LEN + align;/*for sending to the allocate functiom*/
 }

 (*packet_size_p) = TCRC_packet_size + LRH_LEN; /*CRC fields are included*/


 if((allocate_packet_LRH(TCRC_packet_size, t_packet_size, t_packet_buf_p,
                         *packet_size_p, packet_buf_p)) != MT_OK) return(MT_EKMALLOC);
                         /*packet_bup_p is a p2p*/

/*Update the fields in the given lrh struct*/
 lrh_st_p->LNH    = LNH;
 lrh_st_p->PktLen  = (*packet_size_p - VCRC_LEN) / IBWORD;
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

 if(LNH == IBA_LOCAL){    /*appending the ICRC */
  start_ICRC_p = (u_int8_t*)start_LRH_p + LRH_LEN + t_packet_size;
  ICRC = fast_calc_ICRC(*packet_size_p, *packet_buf_p, LNH);
  append_ICRC((u_int16_t*)start_ICRC_p, ICRC);
 }

  start_VCRC_p = (u_int8_t*)start_LRH_p + LRH_LEN + TCRC_packet_size -VCRC_LEN;
  VCRC = fast_calc_VCRC(*packet_size_p, *packet_buf_p); /* appendinf the VCRC*/
  append_VCRC((u_int16_t*)start_VCRC_p, VCRC);

  return(MT_OK);
}


/*******************************************************************************/
/*                       reliable send                                         */
/*******************************************************************************/
call_result_t
MPGA_reliable_send(IB_BTH_st *bth_st_p, u_int16_t payload_size, void *payload_buf_vp,
                   u_int16_t *packet_size_p, void **packet_buf_vp, IB_pkt_place packet_place)
{
 return(MT_ENOSYS);
}

/*******************************************************************************/
/*                       reliable send only                                    */
/*******************************************************************************/
call_result_t
MPGA_rc_send_only(IB_BTH_st *bth_st_p, u_int16_t payload_size, void *payload_buf_vp,
                  u_int16_t *packet_size_p, void **packet_buf_vp)
{
 u_int16_t header_size;
 u_int16_t packet_size;
 u_int16_t *payload_buf_p;
 u_int16_t **packet_buf_p;

 packet_buf_p = (u_int16_t**)packet_buf_vp;
 payload_buf_p = (u_int16_t*)payload_buf_vp;/*casting the void to u_int16_t* ,data could be 4096B*/

 header_size = RC_SEND_ONLY_LEN;   /*init parameters*/
 packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD);

 /*Updating fields and given arguments*/
(*packet_size_p) = packet_size;
 bth_st_p->OpCode = RC_SEND_ONLY_OP; /*opcode is 00000100 */

 if((allocate_packet(payload_size, payload_buf_p, packet_size, packet_buf_p)) != MT_OK)
  return(MT_EKMALLOC);

 /*packet_bup_p is a p2p*/
  /*printf("\n in before append bth reliable packet_buf_p is %d",(*packet_buf_p));*/

 if((append_BTH (bth_st_p, packet_buf_p, payload_size)) != MT_OK) return(MT_ERROR);
 /*appending the bth field */

 return(MT_OK);
}

/***********************************************************************************/
/*                    reliable rdma write only                                     */
/***********************************************************************************/
call_result_t
MPGA_rc_rdma_w_only(IB_BTH_st *bth_st_p, IB_RETH_st *reth_st_p,
                    u_int16_t payload_size, void *payload_buf_vp,
		    u_int16_t *packet_size_p, void **packet_buf_vp)
{
 u_int16_t header_size;
 u_int16_t packet_size;
 u_int16_t *payload_buf_p;
 u_int16_t **packet_buf_p;

 packet_buf_p = (u_int16_t**)packet_buf_vp;
 payload_buf_p = (u_int16_t*)payload_buf_vp;/*casting the void to u_int16_t* ,data could be 4096B*/
 header_size = RC_WRITE_ONLY_LEN;    /*init parametrs */
 packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD);

 (*packet_size_p) = packet_size;/*Update given arg to the packet size with out LRH or GRH */
  bth_st_p->OpCode = RC_WRITE_ONLY_OP; /*opcode is 00001001 */

 if((allocate_packet(payload_size, payload_buf_p, packet_size, packet_buf_p)) != MT_OK)
  return(MT_EKMALLOC);/*packet_bup_p is a p2p*/

 /*appending the wanted fields*/
 if((append_RETH (reth_st_p, packet_buf_p)) != MT_OK) return(MT_ERROR);
 /*appending the reth field*/
 if((append_BTH (bth_st_p, packet_buf_p, payload_size)) != MT_OK) return(MT_ERROR);
 /*appending the bth field */

 return(MT_OK);
}



/***********************************************************************************/
/*                    reliable rdma write first                                    */
/***********************************************************************************/
call_result_t
MPGA_rc_rdma_w_first(IB_BTH_st *bth_st_p, IB_RETH_st *reth_st_p,
                    u_int16_t payload_size, void *payload_buf_vp,
		    u_int16_t *packet_size_p, void **packet_buf_vp)
{
 u_int16_t header_size;
 u_int16_t packet_size;
 u_int16_t *payload_buf_p;
 u_int16_t **packet_buf_p;

 packet_buf_p = (u_int16_t**)packet_buf_vp;
 payload_buf_p = (u_int16_t*)payload_buf_vp;/*casting the void to u_int16_t* ,data could be 4096B*/
 header_size = RC_WRITE_FIRST_LEN;    /*init parametrs */
 packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD);

 (*packet_size_p) = packet_size;/*Update given arg to the packet size with out LRH or GRH */
  bth_st_p->OpCode = RC_WRITE_FIRST_OP; /*opcode is 00001001 */

 if((allocate_packet(payload_size, payload_buf_p, packet_size, packet_buf_p)) != MT_OK)
  return(MT_EKMALLOC);/*packet_bup_p is a p2p*/

 /*appending the wanted fields*/
 if((append_RETH (reth_st_p, packet_buf_p)) != MT_OK) return(MT_ERROR);
 /*appending the reth field*/
 if((append_BTH (bth_st_p, packet_buf_p, payload_size)) != MT_OK) return(MT_ERROR);
 /*appending the bth field */

 return(MT_OK);
}




/***********************************************************************************/
/*                    reliable rdma write middle                                   */
/***********************************************************************************/
call_result_t
MPGA_rc_rdma_w_middle(IB_BTH_st *bth_st_p, u_int16_t payload_size,
                      void *payload_buf_vp, u_int16_t *packet_size_p,
                      void **packet_buf_vp)
{
 u_int16_t header_size;
 u_int16_t packet_size;
 u_int16_t *payload_buf_p;
 u_int16_t **packet_buf_p;

 packet_buf_p = (u_int16_t**)packet_buf_vp;
 payload_buf_p = (u_int16_t*)payload_buf_vp;/*casting the void to u_int16_t* ,data could be 4096B*/
 header_size = RC_WRITE_MIDDLE_LEN;    /*init parametrs */
 packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD);

 (*packet_size_p) = packet_size;/*Update given arg to the packet size with out LRH or GRH */
  bth_st_p->OpCode = RC_WRITE_MIDDLE_OP; /*opcode is 00001001 */

 if((allocate_packet(payload_size, payload_buf_p, packet_size, packet_buf_p)) != MT_OK)
  return(MT_EKMALLOC);/*packet_bup_p is a p2p*/

 /*appending the bth field */
 if((append_BTH (bth_st_p, packet_buf_p, payload_size)) != MT_OK) return(MT_ERROR);
 

 return(MT_OK);
}


/***********************************************************************************/
/*                    reliable rdma write last                                     */
/***********************************************************************************/
call_result_t
MPGA_rc_rdma_w_last(IB_BTH_st *bth_st_p, u_int16_t payload_size,
                    void *payload_buf_vp, u_int16_t *packet_size_p,
                    void **packet_buf_vp)
{
 u_int16_t header_size;
 u_int16_t packet_size;
 u_int16_t *payload_buf_p;
 u_int16_t **packet_buf_p;

 packet_buf_p = (u_int16_t**)packet_buf_vp;
 payload_buf_p = (u_int16_t*)payload_buf_vp;/*casting the void to u_int16_t* ,data could be 4096B*/
 header_size = RC_WRITE_LAST_LEN;    /*init parametrs */
 packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD);

 (*packet_size_p) = packet_size;/*Update given arg to the packet size with out LRH or GRH */
  bth_st_p->OpCode = RC_WRITE_LAST_OP; /*opcode is 00001001 */

 if((allocate_packet(payload_size, payload_buf_p, packet_size, packet_buf_p)) != MT_OK)
  return(MT_EKMALLOC);/*packet_bup_p is a p2p*/

 /*appending the bth field */
 if((append_BTH (bth_st_p, packet_buf_p, payload_size)) != MT_OK) return(MT_ERROR);
 

 return(MT_OK);
}


/***********************************************************************************/
/*                    reliable rdma read request only                              */
/***********************************************************************************/
call_result_t
MPGA_rc_rdma_r_req(IB_BTH_st *bth_st_p, IB_RETH_st *reth_st_p,
                   u_int16_t *packet_size_p, void **packet_buf_vp)
{
 u_int16_t header_size;
 u_int16_t packet_size;
 u_int16_t payload_size = 0;/*For passing on to the functions*/
 u_int16_t **packet_buf_p;

 packet_buf_p = (u_int16_t**)packet_buf_vp;
 header_size = RC_READ_REQ_LEN;    /*init parametrs */
 packet_size = header_size;          /*No payload in this packet*/

 (*packet_size_p) = packet_size;/*Update given arg to the packet size with out LRH or GRH */
  bth_st_p->OpCode = RC_READ_REQ_OP; /*opcode is 00001100 (overwrite)*/

 if((allocate_packet(payload_size, NULL, packet_size, packet_buf_p)) != MT_OK)
  return(MT_EKMALLOC); /*packet_bup_p is a p2p*/

 if((append_RETH (reth_st_p, packet_buf_p)) != MT_OK) return(MT_ERROR);
 /*appending the reth field*/
 if((append_BTH (bth_st_p, packet_buf_p, payload_size)) != MT_OK) return(MT_ERROR);
 /*appending the bth field */
 return(MT_OK);
}

/***********************************************************************************/
/*                    reliable rdma read response (First Middle or Last)           */
/***********************************************************************************/
call_result_t
MPGA_rc_rdma_r_resp(IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p,
		    u_int16_t payload_size, void *payload_buf_vp, u_int16_t *packet_size_p, 
		    void **packet_buf_vp, IB_pkt_place packet_place)  
{
	 u_int16_t header_size = 0;
	 u_int16_t packet_size = 0;
	 u_int16_t *payload_buf_p;
	 u_int16_t **packet_buf_p;
	     
	 packet_buf_p = (u_int16_t**)packet_buf_vp;
	 payload_buf_p = (u_int16_t*)payload_buf_vp;/*casting the void to u_int16_t* ,data could be 4096B*/
	 
	 switch(packet_place){
		 case FIRST_PACKET: bth_st_p->OpCode = RC_READ_RESP_FIRST_OP;
				    header_size = RC_READ_RESP_FIRST_LEN;    /*init parametrs */  
				    break;
		 case MIDDLE_PACKET: bth_st_p->OpCode = RC_READ_RESP_MIDDLE_OP;
				     header_size = RC_READ_RESP_MIDDLE_LEN;    /*init parametrs */  
				     break;
		 case LAST_PACKET:  bth_st_p->OpCode = RC_READ_RESP_LAST_OP;
				    header_size = RC_READ_RESP_LAST_LEN;    /*init parametrs */  
	                 	    break;
		 default: MTL_ERROR('1', "\nERROR (PLACE)  IN rdma r resp\n");
	 };
	 
	   packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD);	 
          (*packet_size_p) = packet_size;/*Update given arg to the packet size with out LRH or GRH */ 
	  
	 if((allocate_packet(payload_size, payload_buf_p, packet_size, packet_buf_p)) != MT_OK)
	 return(MT_EKMALLOC);/*packet_bup_p is a p2p*/
		     
	 /*appending the wanted fields*/
	 if(packet_place != MIDDLE_PACKET){
	 	if((append_AETH (aeth_st_p, packet_buf_p)) != MT_OK) return(MT_ERROR);
	 }	
	 /*appending the reth field*/
	 if((append_BTH (bth_st_p, packet_buf_p, payload_size)) != MT_OK) return(MT_ERROR);
	 /*appending the bth field */
	 return(MT_OK);
}
                                  
/***********************************************************************************/
/*                    reliable rdma read response only                             */
/***********************************************************************************/
call_result_t
MPGA_rc_rdma_r_resp_only(IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p,
                         u_int16_t payload_size, void *payload_buf_vp,
	                 u_int16_t *packet_size_p, void **packet_buf_vp)
{
 u_int16_t header_size;
 u_int16_t packet_size;
 u_int16_t *payload_buf_p;
 u_int16_t **packet_buf_p;

 packet_buf_p = (u_int16_t**)packet_buf_vp;
 payload_buf_p = (u_int16_t*)payload_buf_vp;/*casting the void to u_int16_t* ,data could be 4096B*/
 header_size = RC_READ_RESP_ONLY_LEN;    /*init parametrs */
 packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD);

 (*packet_size_p) = packet_size;/*Update given arg to the packet size with out LRH or GRH */
  bth_st_p->OpCode = RC_READ_RESP_ONLY_OP; /*opcode is 00001001 */

 if((allocate_packet(payload_size, payload_buf_p, packet_size, packet_buf_p)) != MT_OK)
  return(MT_EKMALLOC);/*packet_bup_p is a p2p*/

 /*appending the wanted fields*/
 if((append_AETH (aeth_st_p, packet_buf_p)) != MT_OK) return(MT_ERROR);
 /*appending the reth field*/
 if((append_BTH (bth_st_p, packet_buf_p, payload_size)) != MT_OK) return(MT_ERROR);
 /*appending the bth field */
 return(MT_OK);
}

/***********************************************************************************/
/*                    unreliable Datagram send only                                */
/***********************************************************************************/
call_result_t
MPGA_ud_send_only(IB_BTH_st *bth_st_p, IB_DETH_st *deth_st_p,
                  u_int16_t payload_size, void *payload_buf_vp,
                  u_int16_t *packet_size_p, void **packet_buf_vp)	
{
 u_int16_t header_size;
 u_int16_t packet_size;
 u_int16_t *payload_buf_p;
 u_int16_t **packet_buf_p;

 packet_buf_p = (u_int16_t**)packet_buf_vp;
 payload_buf_p = (u_int16_t*)payload_buf_vp;/*casting the void to u_int16_t* ,data could be 4096B*/
 header_size = UD_SEND_ONLY_LEN;    /*init parametrs */
 packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD);

 (*packet_size_p) = packet_size;/*Update given arg to the packet size with out LRH or GRH */
  bth_st_p->OpCode = UD_SEND_ONLY_OP; /*opcode is 01100100 */

 if((allocate_packet(payload_size, payload_buf_p, packet_size, packet_buf_p)) != MT_OK)
  return(MT_EKMALLOC);/*packet_bup_p is a p2p*/

 /*appending the wanted fields*/
 if((append_DETH (deth_st_p, packet_buf_p)) != MT_OK) return(MT_ERROR);
 /*appending the reth field*/
 if((append_BTH (bth_st_p, packet_buf_p, payload_size)) != MT_OK) return(MT_ERROR);
 /*appending the bth field */
 return(MT_OK);
}



/************************************************************************/
/*                           Bulding headers only                       */
/************************************************************************/



/*************************************************************************/
/*                            fast RC send first                         */
/*************************************************************************/
call_result_t 
MPGA_fast_rc_send_first(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                        IB_BTH_st *bth_st_p, LNH_t LNH, u_int16_t payload_size, 
                        u_int16_t *header_size_p, void **header_buf_p)
{
    u_int16_t header_size = 0, packet_size;
    u_int8_t* temp_header_buff;

    if (LNH != IBA_LOCAL) return(MT_ENOSYS);

    header_size = RC_SEND_FIRST_LEN + LRH_LEN;    
    packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD) + ICRC_LEN + VCRC_LEN;

    (*header_size_p) = header_size;/*Update given arg to the packet size with out LRH or GRH */
    bth_st_p->OpCode = RC_SEND_FIRST_OP; /*opcode is 01100100 */

    if ((temp_header_buff = ALLOCATE(u_int8_t,(header_size)))== NULL)
    { /* Allocting size in bytes*/
        MTL_TRACE('5', "\nfailed to allocate temp_buffer_p");
        return(MT_ENOMEM);
    };
    
    if(MLOCK(temp_header_buff, header_size)){
	MTL_TRACE('5', "\nfailed to lock temp_head_buff");
        return(MT_ENOMEM);
    };
  

    temp_header_buff += header_size; /* Building the header from end to start */ 

    /*************appending the wanted fields**********************/
    if ((append_BTH (bth_st_p, (u_int16_t**)&temp_header_buff, payload_size)) != MT_OK) return(MT_ERROR);
    /*appending the bth field */

    if ((append_LRH (lrh_st_p, packet_size, (u_int16_t**)&temp_header_buff, LNH)) != MT_OK) return(MT_ERROR);
    /*appending the lrh field */

    *header_buf_p = temp_header_buff;

    return(MT_OK);
}
/*************************************************************************/
/*                            fast RC send middle                        */
/*************************************************************************/
call_result_t 
MPGA_fast_rc_send_middle(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                         IB_BTH_st *bth_st_p, LNH_t LNH, u_int16_t payload_size, 
                         u_int16_t *header_size_p, void **header_buf_p)
{
    u_int16_t header_size = 0, packet_size;
    u_int8_t* temp_header_buff;

    if (LNH != IBA_LOCAL) return(MT_ENOSYS);

    header_size = RC_SEND_MIDDLE_LEN + LRH_LEN;    
    packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD) + ICRC_LEN + VCRC_LEN;

    (*header_size_p) = header_size;/*Update given arg to the packet size with out LRH or GRH */
    bth_st_p->OpCode = RC_SEND_MIDDLE_OP; 

    if ((temp_header_buff = ALLOCATE(u_int8_t,(header_size)))== NULL)
    { /* Allocting size in bytes*/
        MTL_TRACE('5', "\nfailed to allocate temp_buffer_p");
        return(MT_ENOSYS);
    };

    if(MLOCK(temp_header_buff, header_size)){
	MTL_TRACE('5', "\nfailed to lock temp_head_buff");
        return(MT_ENOMEM);
    };

    temp_header_buff += header_size; /* Building the header from end to start */ 

    /*************appending the wanted fields**********************/
    if ((append_BTH (bth_st_p, (u_int16_t**)&temp_header_buff, payload_size)) != MT_OK) return(MT_ERROR);
    /*appending the bth field */

    if ((append_LRH (lrh_st_p, packet_size, (u_int16_t**)&temp_header_buff, LNH)) != MT_OK) return(MT_ERROR);
    /*appending the lrh field */

    *header_buf_p = temp_header_buff;

    return(MT_OK);
}
/*************************************************************************/
/*                            fast RC send last                          */
/*************************************************************************/
call_result_t 
MPGA_fast_rc_send_last(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                       IB_BTH_st *bth_st_p, LNH_t LNH, u_int16_t payload_size, 
                       u_int16_t *header_size_p, void **header_buf_p)
{
    u_int16_t header_size = 0, packet_size;
    u_int8_t* temp_header_buff;

    if (LNH != IBA_LOCAL) return(MT_ENOSYS);

    header_size = RC_SEND_LAST_LEN + LRH_LEN;    
    packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD) + ICRC_LEN + VCRC_LEN;

    (*header_size_p) = header_size;/*Update given arg to the packet size with out LRH or GRH */
    bth_st_p->OpCode = RC_SEND_LAST_OP; 

    if ((temp_header_buff = ALLOCATE(u_int8_t,(header_size)))== NULL)
    { /* Allocting size in bytes*/
        MTL_TRACE('5', "\nfailed to allocate temp_buffer_p");
        return(MT_EAGAIN);
    };

    if(MLOCK(temp_header_buff, header_size)){
	MTL_TRACE('5', "\nfailed to lock temp_head_buff");
        return(MT_ENOMEM);
    };


    temp_header_buff += header_size; /* Building the header from end to start */ 

    /*************appending the wanted fields**********************/
    if ((append_BTH (bth_st_p, (u_int16_t**)&temp_header_buff, payload_size)) != MT_OK) return(MT_ERROR);
    /*appending the bth field */

    if ((append_LRH (lrh_st_p, packet_size, (u_int16_t**)&temp_header_buff, LNH)) != MT_OK) return(MT_ERROR);
    /*appending the lrh field */

    *header_buf_p = temp_header_buff;

    return(MT_OK);
}
/*************************************************************************/
/*                            fast RC send only                          */
/*************************************************************************/
call_result_t 
MPGA_fast_rc_send_only(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                       IB_BTH_st *bth_st_p, LNH_t LNH, u_int16_t payload_size, 
                       u_int16_t *header_size_p, void **header_buf_p)
{
    u_int16_t header_size = 0, packet_size;
    u_int8_t* temp_header_buff;

    if (LNH != IBA_LOCAL) return(MT_EAGAIN);

    header_size = RC_SEND_ONLY_LEN + LRH_LEN;    
    packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD) + ICRC_LEN + VCRC_LEN;

    (*header_size_p) = header_size;/*Update given arg to the packet size with out LRH or GRH */
    bth_st_p->OpCode = RC_SEND_ONLY_OP; /*opcode is 01100100 */

    if ((temp_header_buff = ALLOCATE(u_int8_t,(header_size)))== NULL)
    { /* Allocting size in bytes*/
        MTL_TRACE('5', "\nfailed to allocate temp_buffer_p");
        return(MT_ENOMEM);
    };

    if(MLOCK(temp_header_buff, header_size)){
	MTL_TRACE('5', "\nfailed to lock temp_head_buff");
        return(MT_ENOMEM);
    };

    temp_header_buff += header_size; /* Building the header from end to start */ 

    /*************appending the wanted fields**********************/
    if ((append_BTH (bth_st_p, (u_int16_t**)&temp_header_buff, payload_size)) != MT_OK) return(MT_ERROR);
    /*appending the bth field */

    if ((append_LRH (lrh_st_p, packet_size, (u_int16_t**)&temp_header_buff, LNH)) != MT_OK) return(MT_ERROR);
    /*appending the lrh field */

    *header_buf_p = temp_header_buff;

    return(MT_OK);
}
/*************************************************************************/
/*                            fast RC RDMA read response first           */
/*************************************************************************/
call_result_t 
MPGA_fast_rc_read_resp_first(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                      	     IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p, LNH_t LNH, 
			     u_int16_t payload_size, u_int16_t *header_size_p, 
			     void **header_buf_p)
{
    u_int16_t header_size = 0, packet_size;
    u_int8_t* temp_header_buff;

    if (LNH != IBA_LOCAL) return(MT_ENOSYS);

    header_size = RC_READ_RESP_FIRST_LEN + LRH_LEN;    
    packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD) + ICRC_LEN + VCRC_LEN;

    (*header_size_p) = header_size;/*Update given arg to the packet size with out LRH or GRH */
    bth_st_p->OpCode = RC_READ_RESP_FIRST_OP; 

    if ((temp_header_buff = ALLOCATE(u_int8_t,(header_size)))== NULL)
    { /* Allocting size in bytes*/
        MTL_TRACE('5', "\nfailed to allocate temp_buffer_p in rc read resp first");
        return(MT_ENOMEM);
    };

    if(MLOCK(temp_header_buff, header_size)){
	MTL_TRACE('5', "\nfailed to lock temp_head_buff");
        return(MT_ENOMEM);
    };

    temp_header_buff += header_size; /* Building the header from end to start */ 

    /*************appending the wanted fields**********************/
    if ((append_AETH (aeth_st_p, (u_int16_t**)&temp_header_buff)) != MT_OK) return(MT_ERROR);
    /*appending the aeth field */


    if ((append_BTH (bth_st_p, (u_int16_t**)&temp_header_buff, payload_size)) != MT_OK) return(MT_ERROR);
    /*appending the bth field */

    if ((append_LRH (lrh_st_p, packet_size, (u_int16_t**)&temp_header_buff, LNH)) != MT_OK) return(MT_ERROR);
    /*appending the lrh field */

    *header_buf_p = temp_header_buff;

    return(MT_OK);
}
/*************************************************************************/
/*                            fast RC RDMA read response middle          */
/*************************************************************************/
call_result_t 
MPGA_fast_rc_read_resp_middle(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                      	      IB_BTH_st *bth_st_p, LNH_t LNH, u_int16_t payload_size, 
			      u_int16_t *header_size_p, void **header_buf_p)
{
    u_int16_t header_size = 0, packet_size;
    u_int8_t* temp_header_buff;

    if (LNH != IBA_LOCAL) return(MT_ENOSYS);

    header_size = RC_READ_RESP_MIDDLE_LEN + LRH_LEN;    
    packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD) + ICRC_LEN + VCRC_LEN;

    (*header_size_p) = header_size;/*Update given arg to the packet size with out LRH or GRH */
    bth_st_p->OpCode = RC_READ_RESP_MIDDLE_OP; 

    if ((temp_header_buff = ALLOCATE(u_int8_t,(header_size)))== NULL)
    { /* Allocting size in bytes*/
        MTL_TRACE('5', "\nfailed to allocate temp_buffer_p in rc read resp middle");
        return(MT_ENOMEM);
    };

    if(MLOCK(temp_header_buff, header_size)){
	MTL_TRACE('5', "\nfailed to lock temp_head_buff");
        return(MT_ENOMEM);
    };


    temp_header_buff += header_size; /* Building the header from end to start */ 

    /*************appending the wanted fields**********************/
    
    if ((append_BTH (bth_st_p, (u_int16_t**)&temp_header_buff, payload_size)) != MT_OK) return(MT_ERROR);
    /*appending the bth field */

    if ((append_LRH (lrh_st_p, packet_size, (u_int16_t**)&temp_header_buff, LNH)) != MT_OK) return(MT_ERROR);
    /*appending the lrh field */

    *header_buf_p = temp_header_buff;

    return(MT_OK);
}
/*************************************************************************/
/*                            fast RC RDMA read response last            */
/*************************************************************************/
call_result_t 
MPGA_fast_rc_read_resp_last(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                      	    IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p, LNH_t LNH, 
			    u_int16_t payload_size, u_int16_t *header_size_p, 
			    void **header_buf_p)
{
    u_int16_t header_size = 0, packet_size;
    u_int8_t* temp_header_buff;

    if (LNH != IBA_LOCAL) return(MT_ENOSYS);

    header_size = RC_READ_RESP_LAST_LEN + LRH_LEN;    
    packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD) + ICRC_LEN + VCRC_LEN;

    (*header_size_p) = header_size;/*Update given arg to the packet size with out LRH or GRH */
    bth_st_p->OpCode = RC_READ_RESP_LAST_OP; 

    if ((temp_header_buff = ALLOCATE(u_int8_t,(header_size)))== NULL)
    { /* Allocting size in bytes*/
        MTL_TRACE('5', "\nfailed to allocate temp_buffer_p in rc read resp last");
        return(MT_ENOMEM);
    };

    if(MLOCK(temp_header_buff, header_size)){
	MTL_TRACE('5', "\nfailed to lock temp_head_buff");
        return(MT_ENOMEM);
    };

    temp_header_buff += header_size; /* Building the header from end to start */ 

    /*************appending the wanted fields**********************/
    if ((append_AETH (aeth_st_p, (u_int16_t**)&temp_header_buff)) != MT_OK) return(MT_ERROR);
    /*appending the aeth field */


    if ((append_BTH (bth_st_p, (u_int16_t**)&temp_header_buff, payload_size)) != MT_OK) return(MT_ERROR);
    /*appending the bth field */

    if ((append_LRH (lrh_st_p, packet_size, (u_int16_t**)&temp_header_buff, LNH)) != MT_OK) return(MT_ERROR);
    /*appending the lrh field */

    *header_buf_p = temp_header_buff;

    return(MT_OK);
}
/*************************************************************************/
/*                            fast RC RDMA read response only            */
/*************************************************************************/
call_result_t 
MPGA_fast_rc_read_resp_only(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                      	    IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p, LNH_t LNH, 
			    u_int16_t payload_size, u_int16_t *header_size_p, 
			    void **header_buf_p)
{
    u_int16_t header_size = 0, packet_size;
    u_int8_t* temp_header_buff;

    if (LNH != IBA_LOCAL) return(MT_ENOSYS);

    header_size = RC_READ_RESP_ONLY_LEN + LRH_LEN;    
    packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD) + ICRC_LEN + VCRC_LEN;

    (*header_size_p) = header_size;/*Update given arg to the packet size with out LRH or GRH */
    bth_st_p->OpCode = RC_READ_RESP_ONLY_OP; 

    if ((temp_header_buff = ALLOCATE(u_int8_t,(header_size)))== NULL)
    { /* Allocting size in bytes*/
        MTL_TRACE('5', "\nfailed to allocate temp_buffer_p in rc read resp only");
        return(MT_ENOMEM);
    };

    if(MLOCK(temp_header_buff, header_size)){
	MTL_TRACE('5', "\nfailed to lock temp_head_buff");
        return(MT_ENOMEM);
    };


    temp_header_buff += header_size; /* Building the header from end to start */ 

    /*************appending the wanted fields**********************/
    if ((append_AETH (aeth_st_p, (u_int16_t**)&temp_header_buff)) != MT_OK) return(MT_ERROR);
    /*appending the aeth field */


    if ((append_BTH (bth_st_p, (u_int16_t**)&temp_header_buff, payload_size)) != MT_OK) return(MT_ERROR);
    /*appending the bth field */

    if ((append_LRH (lrh_st_p, packet_size, (u_int16_t**)&temp_header_buff, LNH)) != MT_OK) return(MT_ERROR);
    /*appending the lrh field */

    *header_buf_p = temp_header_buff;

    return(MT_OK);
}

/*************************************************************************/
/*                            fast RC ACKNOW                             */
/*************************************************************************/
call_result_t 
MPGA_fast_rc_acknowledge(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                         IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p, LNH_t LNH,
                         u_int16_t *header_size_p, void **header_buf_p)
{
    u_int16_t header_size = 0, packet_size;
    u_int8_t* temp_header_buff;
    u_int16_t payload_size = 0; /* NO payload in Acknowledge packet */ 

    if (LNH != IBA_LOCAL) return(MT_ENOSYS);

    header_size = RC_ACKNOWLEDGE_LEN + LRH_LEN;    /*UD is for transport only init parametrs */
    packet_size = header_size + ICRC_LEN + VCRC_LEN;

    (*header_size_p) = header_size;/*Update given arg to the packet size with out LRH or GRH */
    bth_st_p->OpCode = RC_ACKNOWLEDGE_OP; /*opcode is 01100100 */

    if ((temp_header_buff = ALLOCATE(u_int8_t,(header_size)))== NULL)
    { /* Allocting size in bytes*/
        MTL_TRACE('5', "\nfailed to allocate temp_buffer_p");
        return(MT_ENOMEM);
    };

    if(MLOCK(temp_header_buff, header_size)){
	MTL_TRACE('5', "\nfailed to lock temp_head_buff");
        return(MT_ENOMEM);
    };

    temp_header_buff += header_size; 

    /*************appending the wanted fields**********************/
    if ((append_AETH (aeth_st_p, (u_int16_t**)&temp_header_buff)) != MT_OK) return(MT_ERROR);
    /*appending the aeth field*/

    if ((append_BTH (bth_st_p, (u_int16_t**)&temp_header_buff, payload_size)) != MT_OK) return(MT_ERROR);
    /*appending the bth field */

    if ((append_LRH (lrh_st_p, packet_size, (u_int16_t**)&temp_header_buff, LNH)) != MT_OK) return(MT_ERROR);
    /*appending the lrh field */

    *header_buf_p = temp_header_buff;

    return(MT_OK);
}

/*************************************************************************/
/*                            fast  UD packet send only                  */
/*************************************************************************/
call_result_t
MPGA_fast_ud_send_only(IB_LRH_st *lrh_st_p, IB_BTH_st *bth_st_p,
                       IB_DETH_st *deth_st_p, u_int16_t payload_size,
                       u_int16_t *header_size_p, void **header_buf_p)
{
    u_int16_t header_size, packet_size;
    u_int8_t* temp_header_buff;
    LNH_t LNH;   

    header_size = UD_SEND_ONLY_LEN + LRH_LEN;    /*UD is for transport only init parametrs */
    packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD) + ICRC_LEN + VCRC_LEN;

    (*header_size_p) = header_size;
    bth_st_p->OpCode = UD_SEND_ONLY_OP; 

    if ((temp_header_buff = ALLOCATE(u_int8_t,(header_size)))== NULL)
    { /* Allocting size in bytes*/
        MTL_TRACE('5', "\nfailed to allocate temp_buffer_p");
        return(MT_ENOMEM);
    };

    if(MLOCK(temp_header_buff, header_size)){
	MTL_TRACE('5', "\nfailed to lock temp_head_buff");
        return(MT_ENOMEM);
    };


    temp_header_buff += header_size; 

    /*************appending the wanted fields**********************/
    if ((append_DETH (deth_st_p, (u_int16_t**)&temp_header_buff)) != MT_OK) return(MT_ERROR);
    /*appending the deth field*/

    if ((append_BTH (bth_st_p, (u_int16_t**)&temp_header_buff, payload_size)) != MT_OK) return(MT_ERROR);
    /*appending the bth field */
    LNH = IBA_LOCAL;
    if ((append_LRH (lrh_st_p, packet_size, (u_int16_t**)&temp_header_buff, LNH)) != MT_OK) return(MT_ERROR);
    /*appending the lrh field */

    *header_buf_p = temp_header_buff;

    
    return(MT_OK);
}

/*************************************************************************/
/*                            fast  UD packet send only with grh         */
/*************************************************************************/
call_result_t
MPGA_fast_ud_send_grh(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p, 
                      IB_BTH_st *bth_st_p, IB_DETH_st *deth_st_p, 
                      u_int16_t payload_size, u_int16_t *header_size_p, 
                      void **header_buf_p)
{
    u_int16_t header_size, packet_size;
    u_int8_t* temp_header_buff;
    LNH_t LNH;
    
    header_size = UD_SEND_ONLY_LEN + LRH_LEN + GRH_LEN;  /*UD is for transport only init parametrs */
    packet_size = header_size + payload_size + ((IBWORD - (payload_size % IBWORD)) % IBWORD) + ICRC_LEN + VCRC_LEN;

    (*header_size_p) = header_size;
    bth_st_p->OpCode = UD_SEND_ONLY_OP; /*opcode is 01100100 */

    if ((temp_header_buff = ALLOCATE(u_int8_t,(header_size)))== NULL)
    { /* Allocting size in bytes*/
        MTL_TRACE('5', "\nfailed to allocate temp_buffer_p");
        return(MT_ENOMEM);
    };

    if(MLOCK(temp_header_buff, header_size)){
	MTL_TRACE('5', "\nfailed to lock temp_head_buff");
        return(MT_ENOMEM);
    };


    temp_header_buff += header_size; 

    /*************appending the wanted fields**********************/
    if ((append_DETH (deth_st_p, (u_int16_t**)&temp_header_buff)) != MT_OK) return(MT_ERROR);
    /*appending the deth field*/

    if ((append_BTH (bth_st_p, (u_int16_t**)&temp_header_buff, payload_size)) != MT_OK) return(MT_ERROR);
    /*appending the bth field */

    if ((append_GRH (grh_st_p, packet_size, (u_int16_t**)&temp_header_buff )) != MT_OK) return(MT_ERROR);
    
    LNH = IBA_GLOBAL;
    if ((append_LRH (lrh_st_p, packet_size, (u_int16_t**)&temp_header_buff, LNH)) != MT_OK) return(MT_ERROR);
    /*appending the lrh field */

    *header_buf_p = temp_header_buff;

    return(MT_OK);
}

/***********************************************************************************/
/*                             Analyze Packet                                      */
/***********************************************************************************/
call_result_t
MPGA_analyze_packet(IB_PKT_st *pkt_st_p, void *packet_buf_vp)
{
 u_int16_t *packet_buf_p;
 call_result_t return_val = MT_OK;

 packet_buf_p = (u_int16_t*)packet_buf_vp;
 init_pkt_st(pkt_st_p);/*inisilize the given struct all the poiters to Null size 0*/

 if((pkt_st_p->lrh_st_p = ALLOCATE(IB_LRH_st,1)) == NULL){ /* Allocting size in bytes*/
   MTL_ERROR('1', "\n** ERROR failed to allocate pkt_st_p->lrh_st");
   return(MT_EKMALLOC);
  };
 
 MTL_TRACE('5', "\n Extracting lrh field");
 extract_LRH((pkt_st_p->lrh_st_p),&packet_buf_p);
/*Sendind start_packet_p and not packet_buf_p */

 /*Init the pkt_st_p parameterers */
 pkt_st_p->packet_size  = ((pkt_st_p->lrh_st_p)->PktLen * 4) + VCRC_LEN;
 pkt_st_p->payload_size = ((pkt_st_p->lrh_st_p)->PktLen * 4) - ICRC_LEN - LRH_LEN; 

switch((pkt_st_p->lrh_st_p)->LNH){

  case RAW:                      /* 0x0  |LRH|... (Etertype)*/
        MTL_TRACE('5', "\n Analayze RAW packet");
       (pkt_st_p->payload_buf) = packet_buf_p;/*Updating the pointer to the payload point NO GRH*/
       (pkt_st_p->payload_size) += ICRC_LEN;  
       if(check_VCRC(pkt_st_p, (u_int16_t*)packet_buf_vp)== MT_ERROR){
	 return_val = MT_ERROR;/*Checking the VCRC*/
       }
       break;
  case IP_NON_IBA_TRANS:         /* 0x1  |LRH|GRH|...       */
       	MTL_TRACE('5', "\n Analayze NON IBA packet");
        break;
  case IBA_LOCAL:                /* 0x2  |LRH|BTH|...       */
       MTL_TRACE('5', "\n Analayze LOCAL packet");
       if(check_ICRC(pkt_st_p, (u_int16_t*)packet_buf_vp)== MT_ERROR){
	 return_val = MT_ERROR;/*Checking the ICRC*/
       }
       if(check_VCRC(pkt_st_p, (u_int16_t*)packet_buf_vp)== MT_ERROR){
	 return_val = MT_ERROR;/*Checking the VCRC*/
       }
       if((analyze_trans_packet(pkt_st_p, &packet_buf_p)) == MT_ERROR){
	 return_val = MT_ERROR;
       }
       break;
  case IBA_GLOBAL:               /* 0x3  |LRH|GRH|BTH|...   */
       MTL_TRACE('5', "\n Analayze GLOBAL packet");
       return_val = MT_ERROR; 
       break;
  default:
       MTL_ERROR('1', "\n ERROR case in analyze packet\n");
         return_val = MT_ERROR;
         break;
 }

 return(return_val);
}

/***********************************************************************************/
/*                             Analyze Transport Packet                            */
/***********************************************************************************/
call_result_t
analyze_trans_packet(IB_PKT_st *pkt_st_p, u_int16_t **packet_p)
{

 if((pkt_st_p->bth_st_p = ALLOCATE(IB_BTH_st,1)) == NULL){ /* Allocting size in bytes*/
   MTL_ERROR('1', "\nfailed to allocate pkt_st_p->bth_st");
   return(MT_EKMALLOC);
  };
 MTL_TRACE('5', "\n Extracting the BTH field");
 extract_BTH((pkt_st_p->bth_st_p), packet_p);
 (pkt_st_p->payload_size) -= BTH_LEN;

 switch((pkt_st_p->bth_st_p)->OpCode){

    case RC_SEND_FIRST_OP:
    case RC_SEND_MIDDLE_OP:
    case RC_SEND_LAST_OP:
    case RC_SEND_ONLY_OP:        /*0x4  /BTH/pyload/ */
         (pkt_st_p->payload_buf) = *packet_p;/*Updating the pointer to the packet buf*/
         break;
    case RC_WRITE_ONLY_OP:      /*0xa  /BTH/RETH/pyload/ */
    case RC_WRITE_FIRST_OP:
           if((pkt_st_p->reth_st_p = ALLOCATE(IB_RETH_st,1)) == NULL){ /* Allocting size in bytes*/
             MTL_ERROR('1', "\nfailed to allocate pkt_st_p->reth_st");
             return(MT_EKMALLOC);
           };
           extract_RETH((pkt_st_p->reth_st_p),packet_p);
           (pkt_st_p->payload_size) -= RETH_LEN;
           (pkt_st_p->payload_buf) = *packet_p;
           break;

    case RC_WRITE_LAST_W_IM_OP:
    case RC_WRITE_ONLY_W_IM_OP:
	   if((pkt_st_p->reth_st_p = ALLOCATE(IB_RETH_st,1)) == NULL){ /* Allocting size in bytes*/
             MTL_ERROR('1', "\nfailed to allocate pkt_st_p->reth_st");
             return(MT_EKMALLOC);
           };
           extract_RETH((pkt_st_p->reth_st_p),packet_p);
           (pkt_st_p->payload_size) -= RETH_LEN;
           (pkt_st_p->payload_buf) = *packet_p;
		
	   if((pkt_st_p->immdt_st_p = ALLOCATE(IB_ImmDt_st,1)) == NULL){ /* Allocting size in bytes*/
             MTL_ERROR('1', "\nfailed to allocate pkt_st_p->IB_ImmDt_st");
             return(MT_EKMALLOC);
           };
           extract_ImmDt((pkt_st_p->immdt_st_p),packet_p);
           (pkt_st_p->payload_size) -= ImmDt_LEN;
           (pkt_st_p->payload_buf) = *packet_p;
           break;

    case RC_WRITE_LAST_OP: /* BTH */
    case RC_WRITE_MIDDLE_OP:
           break;
    case RC_SEND_ONLY_W_IM_OP:
    case RC_SEND_LAST_W_IM_OP:
	 if((pkt_st_p->immdt_st_p = ALLOCATE(IB_ImmDt_st,1)) == NULL){ /* Allocting size in bytes*/
             MTL_ERROR('1', "\nfailed to allocate pkt_st_p->IB_ImmDt_st");
             return(MT_EKMALLOC);
           };
           extract_ImmDt((pkt_st_p->immdt_st_p),packet_p);
           (pkt_st_p->payload_size) -= ImmDt_LEN;
           (pkt_st_p->payload_buf) = *packet_p;
           break;

    case RC_READ_REQ_OP:       /*0xc  /BTH/RETH/ */
         if((pkt_st_p->reth_st_p = ALLOCATE(IB_RETH_st,1)) == NULL){ /* Allocting size in bytes*/
           MTL_ERROR('1', "\nfailed to allocate pkt_st_p->reth_st");
           return(MT_EKMALLOC);
         };
         extract_RETH((pkt_st_p->reth_st_p),packet_p);
         (pkt_st_p->payload_size) -= RETH_LEN; /* should be zero */
         /*No payload to this packet*/
         break;
	 
    case RC_READ_RESP_FIRST_OP:
	 if((pkt_st_p->aeth_st_p = ALLOCATE(IB_AETH_st,1)) == NULL){ /* Allocting size in bytes*/
	   MTL_ERROR('1', "\nfailed to allocate pkt_st_p->aeth_st");
	   return(MT_EKMALLOC);
	 };
	 extract_AETH((pkt_st_p->aeth_st_p),packet_p); 
         (pkt_st_p->payload_size) -= AETH_LEN;
	 (pkt_st_p->payload_buf) = *packet_p; 
	 break;
	 
    case RC_READ_RESP_MIDDLE_OP:
         (pkt_st_p->payload_buf) = *packet_p;
         break;
	 
    case RC_READ_RESP_LAST_OP: /*BTH/AETH/pyload*/
         if((pkt_st_p->aeth_st_p = ALLOCATE(IB_AETH_st,1)) == NULL){ /* Allocting size in bytes*/
	   MTL_ERROR('1', "\nfailed to allocate pkt_st_p->aeth_st");
	   return(MT_EKMALLOC);
	 };
         extract_AETH((pkt_st_p->aeth_st_p),packet_p);
         (pkt_st_p->payload_size) -= AETH_LEN;
         (pkt_st_p->payload_buf) = *packet_p;
         break;          
	 
    case RC_READ_RESP_ONLY_OP: /*0x10 /BTH/AETH/payload/ */
         if((pkt_st_p->aeth_st_p = ALLOCATE(IB_AETH_st,1)) == NULL){ /* Allocting size in bytes*/
           MTL_ERROR('1', "\nfailed to allocate pkt_st_p->aeth_st");
           return(MT_EKMALLOC);
         };
         extract_AETH((pkt_st_p->aeth_st_p),packet_p);
         (pkt_st_p->payload_size) -= AETH_LEN;
         (pkt_st_p->payload_buf) = *packet_p;
         break;

	case RC_ACKNOWLEDGE_OP:
 
      if ((pkt_st_p->aeth_st_p = ALLOCATE(IB_AETH_st,1)) == NULL)
      { /* Allocting size in bytes*/
        MTL_ERROR('1', "\nfailed to allocate pkt_st_p->aeth_st");
        return(MT_EKMALLOC);
      };
      extract_AETH((pkt_st_p->aeth_st_p),packet_p);
      pkt_st_p->payload_size -= AETH_LEN;
	  (pkt_st_p->payload_buf) = *packet_p;	   
      MTL_TRACE('5', "\n this is a ack packet PSN is 0x%X MSN is 0x%X\n",pkt_st_p->bth_st_p->PSN,pkt_st_p->aeth_st_p->MSN);
      break;

		 /****************************************************/
		 /*     unreliable data Gram        UD               */
		 /****************************************************/
    case UD_SEND_ONLY_OP:        /*0x64 /BTH/DETH/payload/ */
         if((pkt_st_p->deth_st_p = ALLOCATE(IB_DETH_st,1)) == NULL){ /* Allocting size in bytes*/
           MTL_ERROR('1', "\nfailed to allocate pkt_st_p->deth_st");
           return(MT_EKMALLOC);
         }
         extract_DETH((pkt_st_p->deth_st_p),packet_p);
         (pkt_st_p->payload_size) -= DETH_LEN;
         (pkt_st_p->payload_buf) = *packet_p;
         break;

		  /****************************************************/
		 /*     unreliable connection        UC               */
		 /*****************************************************/


    default:
         MTL_ERROR('1', "\n The Function does not support this kind of a packet\n");
         return(MT_ERROR);
         break;
 }

 return(MT_OK);
}


/* Remove this later on */
#ifndef OBUILD_FLAG

#ifdef MT_KERNEL

int init_module(void)
{
	MTL_TRACE('1', "MPGA: loading module\n");
	return(0);
}


void cleanup_module(void)
{
	MTL_TRACE('1', "MPGA: removing module\n");
	return;
}

#endif /* MT_KERNEL */

#endif /* OBUILD_FLAG */
