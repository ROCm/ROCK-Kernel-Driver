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


#ifndef H_PACKET_GEN_H
#define H_PACKET_GEN_H

/* Layers Includes */ 
#include <mtl_types.h>

/* MPGA Includes */
#include <ib_opcodes.h>
#include <packet_append.h>
#include <packet_utilities.h>  
 
/******************************************************************************
 *  Function: MPGA_build_pkt_lrh (build pakcet with lrh field)
 *
 *  Description: This function is appending LRH to IB packets .
 *  To use this function you must have a LRH struct,
 *  with all the detailes to create the wanted packet.
 *  The function should generate an IB packet .
 *
 *
 *  Parameters:
 *    lrh_st_p(*in)  IB_LRH_st *
 *	Local route header of the generated packet.
 *    t_packet_size(in) u_int16_t
 *	The transport packet size in bytes.
 *    t_packet_buf_p(in) u_int16_t *
 *	A pointer to the transport packet buffer that the lrh will be appended on.
 *    packet_size_p(out) u_int16_t *
 *      A pointer to the size in bytes include the VCRC of the packet (will be calc by the func).
 *      sould be allocted be the user .
 *    packet_buf_p(out) void **
 *	A pointer to the full packet .
 *      The function will allocate this buf and apdate the pointer.
 *    LNH(in) LNH_T
 *        Link Next Header Definition.
 *      * The LNH given will be placed on in the lrh_st_p->LNH field.
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if no packet was generated.
 *****************************************************************************/
call_result_t
MPGA_build_pkt_lrh(IB_LRH_st *lrh_st_p, u_int16_t t_packet_size, void *t_packet_buf_p,
                    u_int16_t *packet_size_p, void **packet_buf_p, LNH_t LNH);

/******************************************************************************
*  Function: reliable_send   (First , Middle or Last)
*
*  Description: This function generats IB packets for the transport layer only.
*  it appends the BTH field to the given payload ,
*  The function will make the malloc for the packet buffer.
*  and will update both packet_size_p and packet_buf_p
*
*
*  Parameters:
*   bth_st_p(in) IB_BTH_st *
*       Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   payload_buf_p(in) void *
*	A pointer to the payload buffer.
*   packet_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   packet_buf_p(out) void **
*	A pointer to the packet pointer (will be allocated by the function).
*   packet_place(in) IB_packet_place
*       Indicate if it is a first middle or last packet send .
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*
*****************************************************************************/
call_result_t
MPGA_reliable_send(IB_BTH_st *bth_st_p, u_int16_t payload_size, void *payload_buf_p,
                   u_int16_t *packet_size_p, void **packet_buf_p, IB_pkt_place packet_place);

/******************************************************************************
*  Function: reliable_send_only   (Send Only)
*
*	Description: This function generats IB packets (reliable send only)
*	for the transport layer only.
*	it appends the BTH field to the given payload ,
*	The function will make the malloc for the packet buffer.
*	and will update both packet_size_p and packet_buf_p
*
*
*  Parameters:
*   bth_st_p(in) IB_BTH_st *
*	Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   payload_buf_p(in) void *
*       A pointer to the payload buffer.
*   packet_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   packet_buf_p(out) void **
*	A pointer to the packet pointer(will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*
*****************************************************************************/
call_result_t
MPGA_rc_send_only(IB_BTH_st *bth_st_p, u_int16_t payload_size,
		  void *payload_buf_p, u_int16_t *packet_size_p, void **packet_buf_p);

/******************************************************************************
*  Function: reliable_rdma_w_only
*
*  Description: This function generats IB  packets (reliable rdma write only)
*  for the transport layer only.
*  it appends the BTH and RETH field to the given payload ,
*  The function will make the malloc for the packet buffer.
*  and will update both packet_size_p and packet_buf_p
*
*
*  Parameters:
*   bth_st_p(in) IB_BTH_st *
*				Base transport header (no need for opcode field).
*   reth_st_p(in) IB_RETH_st *
*       RDMA Extended trasport header .
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   payload_buf_p(in) void *
*	A pointer to the payload buffer.
*   packet_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   packet_buf_p(out) void **
*	A pointer to the packet pointer(will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*
*****************************************************************************/
call_result_t
MPGA_rc_rdma_w_only(IB_BTH_st *bth_st_p, IB_RETH_st *reth_st_p,
                    u_int16_t payload_size, void *payload_buf_p,
                    u_int16_t *packet_size_p, void **packet_buf_p); 	

/******************************************************************************
*  Function: reliable_rdma_w_first
*
*  Description: This function generats IB  packets (reliable rdma write first)
*  for the transport layer only.
*  it appends the BTH and RETH field to the given payload ,
*  The function will make the malloc for the packet buffer.
*  and will update both packet_size_p and packet_buf_p
*
*
*  Parameters:
*   bth_st_p(in) IB_BTH_st *
*				Base transport header (no need for opcode field).
*   reth_st_p(in) IB_RETH_st *
*       RDMA Extended trasport header .
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   payload_buf_p(in) void *
*	A pointer to the payload buffer.
*   packet_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   packet_buf_p(out) void **
*	A pointer to the packet pointer(will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*
*****************************************************************************/
call_result_t
MPGA_rc_rdma_w_first(IB_BTH_st *bth_st_p, IB_RETH_st *reth_st_p,
                    u_int16_t payload_size, void *payload_buf_p,
                    u_int16_t *packet_size_p, void **packet_buf_p); 	

/******************************************************************************
*  Function: reliable_rdma_w_middle
*
*  Description: This function generats IB  packets (reliable rdma write middle)
*  for the transport layer only.
*  it appends the given payload to the BTH.
*  The function will make the malloc for the packet buffer.
*  and will update both packet_size_p and packet_buf_p
*
*
*  Parameters:
*   bth_st_p(in) IB_BTH_st *
*				Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*     	The size of the packet payload.
*   payload_buf_p(in) void *
*	      A pointer to the payload buffer.
*   packet_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   packet_buf_p(out) void **
*	      A pointer to the packet pointer(will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*
*****************************************************************************/
call_result_t
MPGA_rc_rdma_w_middle(IB_BTH_st *bth_st_p, u_int16_t payload_size, 
                     void *payload_buf_p, u_int16_t *packet_size_p,
                     void **packet_buf_p); 	

/******************************************************************************
*  Function: reliable_rdma_w_last
*
*  Description: This function generats IB  packets (reliable rdma write last)
*  for the transport layer only.
*  it appends the given payload to the BTH.
*  The function will make the malloc for the packet buffer.
*  and will update both packet_size_p and packet_buf_p
*
*
*  Parameters:
*   bth_st_p(in) IB_BTH_st *
*				Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*     	The size of the packet payload.
*   payload_buf_p(in) void *
*	      A pointer to the payload buffer.
*   packet_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   packet_buf_p(out) void **
*	      A pointer to the packet pointer(will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*
*****************************************************************************/
call_result_t
MPGA_rc_rdma_w_last(IB_BTH_st *bth_st_p, u_int16_t payload_size, 
                     void *payload_buf_p, u_int16_t *packet_size_p,
                     void **packet_buf_p); 

/******************************************************************************
*  Function: reliable_rdma_read_request
*
*  Description: This function generats IB  packets (reliable rdma read request)
*  for the transport layer only.
*  it appends the BTH and RETH field to the given payload ,
*
*
*  Parameters:
*   bth_st_p(in) IB_BTH_st *
*	Base transport header (no need for opcode field).
*   reth_st_p(in) IB_RETH_st *
*       RDMA Extended trasport header .
*   packet_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   packet_buf_p(out) void **
*	A pointer to the packet pointer(will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR.
*
*****************************************************************************/
call_result_t
MPGA_rc_rdma_r_req(IB_BTH_st *bth_st_p, IB_RETH_st *reth_st_p,
                   u_int16_t *packet_size_p, void **packet_buf_p);

/******************************************************************************
 *  Function: reliable_rdma_read_response (First Middle or Last)
 *
 *  Description: This function generats IB  packets (reliable rdma read response First Middle or Last)
 *  for the transport layer only.
 *  it appends the BTH and AETH (if needed) field to the given payload ,
 *  The function will allocate the packet buffer.
 *  and will update both packet_size_p and packet_buf_p
 *
 *
 *  Parameters:
 *   bth_st_p(in) IB_BTH_st *
 *       Base transport header (no need for opcode field).
 *   aeth_st_p(in) IB_AETH_st *
 *       ACK     Extended Transport Header
 *   payload_size(in) u_int16_t
 *       The size of the packet payload.
 *   payload_buf_p(in) void *
 *       A pointer to the payload buffer.
 *   packet_size_p(out) u_int16_t *
 *       A pointer to the size of the packet .
 *   packet_buf_p(out) void **
 *       A pointer to the packet pointer(will be allocated by the function).
 *   packet_place(IN) IB_pkt_place (enum).
 *       FISRT_PACKET MIDDLE_PACKET LAST_PACKET (0,1,2).
 *           
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if no packet was generated.
 *
 ******************************************************************************/                
call_result_t
MPGA_rc_rdma_r_resp(IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p,
		    u_int16_t payload_size, void *payload_buf_vp, u_int16_t *packet_size_p,
		    void **packet_buf_vp, IB_pkt_place packet_place);

/******************************************************************************
*  Function: reliable_rdma_read_response_only
*
*  Description: This function generats IB  packets (reliable rdma read response only)
*  for the transport layer only.
*  it appends the BTH and AETH field to the given payload ,
*  The function will make the malloc for the packet buffer.
*  and will update both packet_size_p and packet_buf_p
*
*
*  Parameters:
*   bth_st_p(in) IB_BTH_st *
*  	Base transport header (no need for opcode field).
*   aeth_st_p(in) IB_AETH_st *
*       ACK	Extended Transport Header
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   payload_buf_p(in) void *
*	A pointer to the payload buffer.
*   packet_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   packet_buf_p(out) void **
*	A pointer to the packet pointer(will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*
*****************************************************************************/
call_result_t
MPGA_rc_rdma_r_resp_only(IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p,
			 u_int16_t payload_size, void *payload_buf_p,
			 u_int16_t *packet_size_p, void **packet_buf_p); 	

/******************************************************************************
* From this part the declaration of unreliable send IB packts functions
******************************************************************************/

/******************************************************************************
*  Function: unreliable_datagram_send_only   (Send Only)
*
*	Description: This function generats IB packets (unreliable datagram send only)
*	for the transport layer only.
*	it appends the BTH and DETH field to the given payload ,
*	The function will make the malloc for the packet buffer.
*	and will update both packet_size_p and packet_buf_p
*
*  Parameters:
*   bth_st_p(in) IB_BTH_st *
*	Base transport header (no need for opcode field).
*   deth_st_p(in) IB_DETH_st *
*       Datagram Extended Transport Header
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   payload_buf_p(in) void *
*	A pointer to the payload buffer.
*   packet_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   packet_buf_p(out) void **
*	A pointer to the packet pointer(will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*****************************************************************************/
call_result_t
MPGA_ud_send_only(IB_BTH_st *bth_st_p, IB_DETH_st *deth_st_p,
                  u_int16_t payload_size, void *payload_buf_p,
                  u_int16_t *packet_size_p, void **packet_buf_p);	


/************************************************************************/
/*                           Bulding headers only                       */
/************************************************************************/
/******************************************************************************
*  Function: unreliable_datagram_send_only   (Send Only)
*
*	Description: This function generats IB packets (unreliable datagram send only)
*	for the transport layer and link layer.
*	it will create the LRH BTH and DETH field to the given payload ,
*	The function will make the malloc for the header.
*	and will update both packet_size_p and header_buf_p
*
*  Parameters:
*   lrh_st_p(in) IB_LRH_st *
* Local route header of the generated header.
*   bth_st_p(in) IB_BTH_st *
*	Base transport header (no need for opcode field).
*   deth_st_p(in) IB_DETH_st *
*       Datagram Extended Transport Header
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   header_size_p(out) u_int16_t *
*       A pointer to the size of the generated packet .
*   header_buf_p(out) void **
*	A pointer to the header pointer(will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was send.
*        MT_ENOSYS
*****************************************************************************/
call_result_t
MPGA_fast_ud_send_only(IB_LRH_st *lrh_st_p, IB_BTH_st *bth_st_p,
                       IB_DETH_st *deth_st_p, u_int16_t payload_size,
                       u_int16_t *header_size_p, void **header_buf_p);  


/******************************************************************************
*  Function: unreliable_datagram_send_only   (Send Only with grh)
*
*	Description: This function generats IB packets (unreliable datagram send only)
*	for the transport layer and link layer.
*	it will create the LRH BTH and DETH field to the given payload ,
*	The function will make the malloc for the header.
*	and will update both packet_size_p and header_buf_p
*
*  Parameters:
*   lrh_st_p(in) IB_LRH_st *
* Local route header of the generated header.
*   grh_st_p(in) IB_GRH_st *
* Global route header of the generated header.
*   bth_st_p(in) IB_BTH_st *
*	Base transport header (no need for opcode field).
*   deth_st_p(in) IB_DETH_st *
*       Datagram Extended Transport Header
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   header_size_p(out) u_int16_t *
*       A pointer to the size of the generated packet .
*   header_buf_p(out) void **
*	A pointer to the header pointer(will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was send.
*        MT_ENOSYS
*****************************************************************************/
call_result_t
MPGA_fast_ud_send_grh(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p, 
                      IB_BTH_st *bth_st_p, IB_DETH_st *deth_st_p, 
                      u_int16_t payload_size, u_int16_t *header_size_p, 
                      void **header_buf_p);

/******************************************************************************
*  Function: reliable_send   (First)
*
*  Description: This function generats IB packets header for the transport and link layers.
*  it appends the LRH BTH  field to the given header ,
*  The function will make the malloc for the packet buffer.
*  and will update both header_size_p and header_buf_p
*
*
*  Parameters:
*   lrh_st_p(out) IB_LRH_st *
*       local route header.
*   grh_st_p(out) IB_GRH_st *
*     global route header. (not supported yet).  
*   bth_st_p(out) IB_BTH_st *
*       Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   header_buf_p(in) void *
*	A pointer to the payload buffer.
*   header_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   header_buf_p(out) void **
*	A pointer to the packet pointer (will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*        MT_ENOSYS
*
*****************************************************************************/
call_result_t 
MPGA_fast_rc_send_first(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                        IB_BTH_st *bth_st_p, LNH_t LNH, u_int16_t payload_size, 
                        u_int16_t *header_size_p, void **header_buf_p);

/******************************************************************************
*  Function: reliable_send   (middle)
*
*  Description: This function generats IB packets header for the transport and link layers.
*  it appends the LRH BTH  field to the given header ,
*  The function will make the malloc for the packet buffer.
*  and will update both header_size_p and header_buf_p
*
*
*  Parameters:
*   lrh_st_p(out) IB_LRH_st *
*       local route header.
*   grh_st_p(out) IB_GRH_st *
*     global route header. (not supported yet).  
*   bth_st_p(out) IB_BTH_st *
*       Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   header_buf_p(in) void *
*	A pointer to the payload buffer.
*   header_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   header_buf_p(out) void **
*	A pointer to the packet pointer (will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*        MT_ENOSYS
*
*****************************************************************************/
call_result_t 
MPGA_fast_rc_send_middle(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                         IB_BTH_st *bth_st_p, LNH_t LNH, u_int16_t payload_size, 
                         u_int16_t *header_size_p, void **header_buf_p);

/******************************************************************************
*  Function: reliable_send   (last)
*
*  Description: This function generats IB packets header for the transport and link layers.
*  it appends the LRH BTH  field to the given header ,
*  The function will make the malloc for the packet buffer.
*  and will update both header_size_p and header_buf_p
*
*
*  Parameters:
*   lrh_st_p(out) IB_LRH_st *
*       local route header.
*   grh_st_p(out) IB_GRH_st *
*     global route header. (not supported yet).  
*   bth_st_p(out) IB_BTH_st *
*       Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   header_buf_p(in) void *
*	A pointer to the payload buffer.
*   header_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   header_buf_p(out) void **
*	A pointer to the packet pointer (will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*        MT_ENOSYS
*
*****************************************************************************/
call_result_t 
MPGA_fast_rc_send_last(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                       IB_BTH_st *bth_st_p, LNH_t LNH, u_int16_t payload_size, 
                       u_int16_t *header_size_p, void **header_buf_p);


/******************************************************************************
*  Function: reliable_send   (only)
*
*  Description: This function generats IB packets header for the transport and link layers.
*  it appends the LRH BTH  field to the given header ,
*  The function will make the malloc for the packet buffer.
*  and will update both header_size_p and header_buf_p
*
*
*  Parameters:
*   lrh_st_p(out) IB_LRH_st *
*       local route header.
*   grh_st_p(out) IB_GRH_st *
*     global route header. (not supported yet).  
*   bth_st_p(out) IB_BTH_st *
*       Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   header_buf_p(in) void *
*	A pointer to the payload buffer.
*   header_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   header_buf_p(out) void **
*	A pointer to the packet pointer (will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*        MT_ENOSYS
*
*****************************************************************************/
call_result_t 
MPGA_fast_rc_send_only(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                       IB_BTH_st *bth_st_p, LNH_t LNH, u_int16_t payload_size, 
                       u_int16_t *header_size_p, void **header_buf_p);

/******************************************************************************
*  Function: reliable_c RDMA READ RESPONSE  (First)
*
*  Description: This function generats IB packets header for the transport and link layers.
*  it appends the LRH BTH AETH field to the given header ,
*  The function will make the malloc for the packet buffer.
*  and will update both header_size_p and header_buf_p
*
*
*  Parameters:
*   lrh_st_p(out) IB_LRH_st *
*       local route header.
*   grh_st_p(out) IB_GRH_st *
*     global route header. (not supported yet).  
*   aeth_st_p(out) IB_AETH_st *
*       Ack extended transport header.
*      bth_st_p(out) IB_BTH_st *
*       Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   header_buf_p(in) void *
*	A pointer to the payload buffer.
*   header_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   header_buf_p(out) void **
*	A pointer to the packet pointer (will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*        MT_ENOSY not supported. 
*
*****************************************************************************/
call_result_t 
MPGA_fast_rc_read_resp_first(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                      	     IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p, LNH_t LNH, 
			     u_int16_t payload_size, u_int16_t *header_size_p, 
			     void **header_buf_p);

/******************************************************************************
*  Function: reliable_c RDMA READ RESPONSE  (middle)
*
*  Description: This function generats IB packets header for the transport and link layers.
*  it appends the LRH BTH AETH field to the given header ,
*  The function will make the malloc for the packet buffer.
*  and will update both header_size_p and header_buf_p
*
*
*  Parameters:
*   lrh_st_p(out) IB_LRH_st *
*       local route header.
*   grh_st_p(out) IB_GRH_st *
*     global route header. (not supported yet).  
*      bth_st_p(out) IB_BTH_st *
*       Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   header_buf_p(in) void *
*	A pointer to the payload buffer.
*   header_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   header_buf_p(out) void **
*	A pointer to the packet pointer (will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*        MT_ENOSY not supported. 
*
*****************************************************************************/
call_result_t 
MPGA_fast_rc_read_resp_middle(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                      	      IB_BTH_st *bth_st_p, LNH_t LNH, u_int16_t payload_size, 
			      u_int16_t *header_size_p, void **header_buf_p);

/******************************************************************************
*  Function: reliable_c RDMA READ RESPONSE  (last)
*
*  Description: This function generats IB packets header for the transport and link layers.
*  it appends the LRH BTH AETH field to the given header ,
*  The function will make the malloc for the packet buffer.
*  and will update both header_size_p and header_buf_p
*
*
*  Parameters:
*   lrh_st_p(out) IB_LRH_st *
*       local route header.
*   grh_st_p(out) IB_GRH_st *
*     global route header. (not supported yet).  
*   aeth_st_p(out) IB_AETH_st *
*       Ack extended transport header.
*      bth_st_p(out) IB_BTH_st *
*       Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   header_buf_p(in) void *
*	A pointer to the payload buffer.
*   header_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   header_buf_p(out) void **
*	A pointer to the packet pointer (will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*        MT_ENOSY not supported. 
*
*****************************************************************************/
call_result_t 
MPGA_fast_rc_read_resp_last(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                      	    IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p, LNH_t LNH, 
			    u_int16_t payload_size, u_int16_t *header_size_p, 
			    void **header_buf_p);

/******************************************************************************
*  Function: reliable_c RDMA READ RESPONSE  (only)
*
*  Description: This function generats IB packets header for the transport and link layers.
*  it appends the LRH BTH AETH field to the given header ,
*  The function will make the malloc for the packet buffer.
*  and will update both header_size_p and header_buf_p
*
*
*  Parameters:
*   lrh_st_p(out) IB_LRH_st *
*       local route header.
*   grh_st_p(out) IB_GRH_st *
*     global route header. (not supported yet).  
*   aeth_st_p(out) IB_AETH_st *
*       Ack extended transport header.
*      bth_st_p(out) IB_BTH_st *
*       Base transport header (no need for opcode field).
*   payload_size(in) u_int16_t
*	The size of the packet payload.
*   header_buf_p(in) void *
*	A pointer to the payload buffer.
*   header_size_p(out) u_int16_t *
*       A pointer to the size of the packet .
*   header_buf_p(out) void **
*	A pointer to the packet pointer (will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR if no packet was generated.
*        MT_ENOSY not supported. 
*
*****************************************************************************/
call_result_t 
MPGA_fast_rc_read_resp_only(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                      	    IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p, LNH_t LNH, 
			    u_int16_t payload_size, u_int16_t *header_size_p, 
			    void **header_buf_p);




call_result_t 
MPGA_fast_rc_acknowledge(IB_LRH_st *lrh_st_p, IB_GRH_st *grh_st_p,
                         IB_BTH_st *bth_st_p, IB_AETH_st *aeth_st_p, LNH_t LNH,
                         u_int16_t *header_size_p, void **header_buf_p);






/*****************************************************************************/
/*                           Analyzer functions                              */
/*****************************************************************************/

/******************************************************************************
*  Function: analyze_packet
*
*  Description: This function Analyze IB  packets .
*  and updates the needed structures acording to its content.
*
*  Parameters:
*   pkt_st_p(out) IB_Pkt_st *
*       A pointer to a packet structure that will be update by the function.
*   packet_buf_p(in) void *
*	A pointer to the start of the packet that have  LRH field .
*
*   NOTE : the function will allocate mem for the inside buffers
*          and it is the user responsibility for free it.
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR
*
*****************************************************************************/
call_result_t
MPGA_analyze_packet(IB_PKT_st *pkt_st_p, void *packet_buf_p);
          											 												 												 												
/******************************************************************************
 *  Function: Packet_generator
 *
 *  Description: This function generats IB packets .
 *  To use this function you must have a general packet struct,
 *  with all the detailes to create the wanted packet.
 *  The function will make the malloc for the packet buffer.
 *  and will update both packet_size_p and packet_buf_p
 *
 *  Parameters:
 *    struct(in) packet_fields
 *	A general packet struct.
 *    payload_size(in) int32_t
 *	The size of the packet payload.
 *    payload_buf_p(in) void
 *	A pointer to the payload buffer.
 *    packet_size_p(out) int32_t *
        A pointer to the size of the packet .
 *    packet_buf_p(out) void
 *	A pointer to the full packet .
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if no packet was generated.
 *****************************************************************************/
/*call_result_t packet_generator ("struct packet_fields", int32_t payload_size,
																u_int8_t *payload_buf_p, int32_t *packet_size_p,
                                u_int8_t *packet_buf_p);*/
																
																
#endif /* H_PACKET_GEN_H */
