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


#ifndef H_PACKET_UTILITIES_H
#define H_PACKET_UTILITIES_H

/**************************************/
#ifndef MT_KERNEL

    #include <stdio.h>
    #include <stdlib.h>

#endif  /* MT_KERNEL */      

/* MPGA Includes */ 
#include <mpga.h>
#include <packet_append.h>
/**************************************/


/******************************************************************************
 *  Function: Print Packet
 *
 *  Description: This function is printing the given packet .
 *
 *  Parameters:
 *    packet_buf_p(IN)  u_int8_t *
 *				A pointer to the first byte in the packet.
 *    packet_size(IN) u_int16_t
 *        The packet size in bytes
 *
 *  Returns: (void function)
 *
 *****************************************************************************/
void
MPGA_print_pkt( u_int8_t *packet_buf_p, u_int16_t packet_size);

/******************************************************************************
 *  Function: free PKT struct fields
 *
 *  Description: This function will free all the allocted structures .
 *               in the IB_PKT_st (IB packet struct).
 *  Parameters:
 *    pkt_st_p(out)  IB_PKT_st *
 *				A pointer to the IB packet struct.
 *
 *  Returns:
 *           MT_OK
 *           MT_ERROR
 *****************************************************************************/
call_result_t
MPGA_free_pkt_st_fields(IB_PKT_st *pkt_st_p);


#endif /* PACKET_UTILITIES */

