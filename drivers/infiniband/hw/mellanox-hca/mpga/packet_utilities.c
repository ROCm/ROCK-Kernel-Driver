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


/***************************/
#ifndef MT_KERNEL

    #include <stdio.h>
    #include <stdlib.h>

#endif  /* MT_KERNEL */ 

/* MPGA Includes */ 
#include <packet_utilities.h>
#include <mpga.h>
#include <packet_append.h>
#include <internal_functions.h>
/***************************/

#define FREE_PKT_FIELD(_parm)  if ( _parm != NULL ) FREE(_parm) 
#define BYTES_IN_LINE 4

/*************************************************************************************/
/*                             print   packet                                        */
/*************************************************************************************/
void
MPGA_print_pkt( u_int8_t *packet_buf_p, u_int16_t packet_size)
{
  int index = 0;
  int pad = 0;
  int gap = 0;
  pad = packet_size % BYTES_IN_LINE ;
  gap = (packet_size / BYTES_IN_LINE) + 1 ;
  packet_size = (packet_size / BYTES_IN_LINE) + 1 ;
  MTL_TRACE('1', "\n Packet \n ----------------------------------------------------\n");

  while(packet_size--){
    MTL_TRACE('1', "\b %d) \b 0x%02X\t   %d) 0x%02X\t   %d) 0x%02X\t   %d) 0x%02X \n",index, packet_buf_p[index],
           index+gap, packet_buf_p[index+gap], index+(2*gap), packet_buf_p[index+(2*gap)],
           index+(3*gap), packet_buf_p[index+(3*gap)]);
     if(packet_size == pad){
        index++;
        while(pad--){
        MTL_TRACE('1', " %d) 0x%02X\t   %d) 0x%02X\t   %d) 0x%02X \n",index, packet_buf_p[index],
               index+gap, packet_buf_p[index+gap], index+(2*gap), packet_buf_p[index+(2*gap)]);
        index++;
        }
      packet_size = 0;
     }
  index++;
  }
}

/*************************************************************************************/
/*                             FREE IB_PKT_st                                        */
/*************************************************************************************/
call_result_t
MPGA_free_pkt_st_fields(IB_PKT_st *pkt_st_p)
{
  FREE_PKT_FIELD(pkt_st_p->lrh_st_p);
  FREE_PKT_FIELD(pkt_st_p->grh_st_p);
  FREE_PKT_FIELD(pkt_st_p->bth_st_p);
  FREE_PKT_FIELD(pkt_st_p->rdeth_st_p);
  FREE_PKT_FIELD(pkt_st_p->deth_st_p);
  FREE_PKT_FIELD(pkt_st_p->reth_st_p);
  FREE_PKT_FIELD(pkt_st_p->atomic_eth_st_p);
  FREE_PKT_FIELD(pkt_st_p->aeth_st_p);
  FREE_PKT_FIELD(pkt_st_p->atomic_acketh_st_p);

  /*FREE_PKT_FIELD(pkt_st_p->payload_buf_p);*/
  /*No need to free the payload it is a pointer to the packet payload the user must free himself*/
  return(MT_OK);
}

