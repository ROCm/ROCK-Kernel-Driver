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


#ifndef H_INTERNAL_FUNCTIONS_H
#define H_INTERNAL_FUNCTIONS_H

/* Layers Includes */ 
#ifdef VXWORKS_OS
#include <bit_ops.h>
#endif /* VXWORKS_OS */
#include <mtl_types.h>
#include <mtl_common.h>

/* MPGA Includes */      
#include <packet_append.h>


#define LITTLE_ENDIAN_TYPE 0
#define BIG_ENDIAN_TYPE  1

#define  ALLOCATE(__type,__num)  (__type *)INTR_MALLOC((__num)*sizeof(__type))

#define INSERTF(W,O1,F,O2,S) ( MT_INSERT32(W, MT_EXTRACT32(F, O2, S), O1, S) )
#define IS_LITTLE_ENDIAN (is_little_endian())
#define IS_BIG_ENDIAN (is_little_endian())


/******************************************************************************
*  Function: allocate_packet
*
*  Description: This function aloocate IB packets for the transport layer only.
*  it allocates the buf acording to the packet size given ,
*  The function will make the malloc for the packet buffer.
*  and will appdate the payload_buf_p.
*
*
*  Parameters:
*   payload_size(in) u_int16_t
*				The size of the packet payload.
*		payload_buf_p(in) u_int16_t *
*				A pointer to the payload buffer.
*		packet_size_p(out) u_int16_t *
*       The full size of the packet (for transport layer). will update if need.
*   packet_buf_p(out) u_int16_t **
*				A pointer to the packet pointer (will be allocated by the function.
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR .
*        MT_EKMALLOC. could not allocate mem .
*
*****************************************************************************/
call_result_t
allocate_packet(u_int16_t payload_size, u_int16_t *payload_buf_p,
                u_int16_t packet_size, u_int16_t **packet_buf_p);

/******************************************************************************
*  Function: allocate_packet_LRH
*
*  Description: This function aloocate IB packets for the transport layer only.
*  it allocates the buf acording to the packet size given ,
*  The function will make the malloc for the packet buffer.
*  and will appdate the payload_buf_p.
*
*
*  Parameters:
*   TCRC_packet_size(in) u_int16_t
*				The size of the Transport packet with the crc .
*   t_packet_size(in) u_int16_t
*       The size of the Transport packet with out the crc .
*		t_packet_buf_p(in) u_int16_t *
*				A pointer to the transport packet.
*		packet_size(in) u_int16_t
*       The full size of the packet with the LRH.
*   packet_buf_p(out) u_int16_t **
*				A pointer to the packet pointer (will be allocated by the function).
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR .
*        MT_EKMALLOC. could not allocate mem .
*
*****************************************************************************/
call_result_t
allocate_packet_LRH(u_int16_t TCRC_packet_size, u_int16_t t_packet_size,
                    u_int16_t *t_packet_buf_p, u_int16_t packet_size, u_int16_t **packet_buf_p);

/******************************************************************************
*  Function: analyze_trans_packet
*
*  Description: This function Analyze transport layer  packets .
*  and updates the needed structures acording to its content.
*
*  Parameters:
*   pkt_st_p(out) IB_Pkt_st *
*       A pointer to a packet structure that will be update by the function.
*   packet_buf_p(in) u_int16_t **
*				A pointer to the start of the packet (Must have LRH field) .
*
*       NOTE : the function will allocate mem for the inside buffers
*              and it is the user responsibility for free it.
*
*  Returns:
*    call_result_t
*        MT_OK,
*        MT_ERROR
*
*****************************************************************************/
call_result_t
analyze_trans_packet(IB_PKT_st *pkt_st_p, u_int16_t **packet_buf_p);

/******************************************************************************
*  Function: is_it_little_endian
*
*  Description: This function checks if the machine is a big or little endian.
*  it will return IS_LITTLE_ENDAIN (0) if it is a little endain and
*  IS_BIG_ENDAIN (1) if it is a big_endian machine.
*
*  Parameters:
*
*  Returns:
*    u_int8_t:
*         LITTLE_ENDAIN_TYPE (0).
*         BIG_ENDAIN_TYPE (1).
*
*****************************************************************************/
u_int8_t is_little_endian(void);

/******************************************************************************
*  Function: little_endian_16
*
*  Description: This function convert Big endain implimintation to little.
*
*   all The parametrs are:
*     byte_x(in) u_int8_t
*     The first byte is the LSB and so on
*
*  Returns:
*    u_int16_t:
******************************************************************************/
u_int16_t
little_endian_16(u_int8_t byte_0, u_int8_t byte_1);

/******************************************************************************
*  Function: little_endian_32
*
*  Description: This function convert Big endain implimintation to little.
*
*   all The parametrs are:
*     byte_x(in) u_int8_t
*     The first byte is the LSB and so on
*
*  Returns:
*    u_int32_t:
******************************************************************************/
u_int32_t
little_endian_32(u_int8_t byte_0, u_int8_t byte_1, u_int8_t byte_2, u_int8_t byte_3);

/******************************************************************************
*  Function: little_endian_64
*
*  Description: This function convert Big endain implimintation to little.
*
*   all The parametrs are:
*     byte_x(in) u_int8_t
*     The first byte is the LSB and so on
*
*  Returns:
*    u_int64_t:
******************************************************************************/
u_int64_t
little_endian_64(u_int8_t byte_0, u_int8_t byte_1, u_int8_t byte_2, u_int8_t byte_3,
                 u_int8_t byte_4, u_int8_t byte_5, u_int8_t byte_6, u_int8_t byte_7);

/******************************************************************************
*  Function: init pkt st (init packet struct)
*
*  Description: This function inisilize the pkt_st members with null pointer.
*               and zero for the packet size member.
*
*   all The parametrs are:
*     pkt_st_p(out) IB_PKY_st
*     packet struct pointer .
*
*  Returns:
*    MT_OK
*    MT_ERROR
******************************************************************************/
call_result_t
init_pkt_st(IB_PKT_st *pkt_st_p);

/******************************************************************************
*  Function: Fast calc ICRC
*
*  Description: This function calculate the ICRC  only if it is an IBA_GLOBAL
*               or IBA_LOCAL packet .
*
*   all The parametrs are:
*     packet_size(in) u_int8_t
*     The packet size .
*     packet_buf_p(in) u_int16_t*
*     Pointer to the start of the packet befor the LRH field
*     LNH(in) LNH_t
*     Packet kind IBA_GLOBAL LOCAL (RAW GRH) (RAW RWH)
*
*  Returns:
*    CALC ICRC (u_int32_t)
******************************************************************************/
u_int32_t
fast_calc_ICRC(u_int16_t packet_size, u_int16_t *packet_buf_p,LNH_t LNH);

/******************************************************************************
*  Function: Fast calc VCRC
*
*  Description: This function calculate the VCRC of the IB Packet.
*
*   all The parametrs are:
*     packet_size(in) u_int8_t
*     The packet size .
*     packet_buf_p(in) u_int16_t*
*     Pointer to the start of the packet befor the LRH field
*
*  Returns:
*    CALC VCRC (u_int16_t)
******************************************************************************/
u_int16_t
fast_calc_VCRC(u_int16_t packet_size, u_int16_t *packet_buf_p);

/******************************************************************************
*  Function: Update ICRC
*
*  Description: This function is updating The ICRC using the crc32 .
*  table for a fast calcculation.
*
*   all The parametrs are:
*     packet_size(in) u_int8_t
*     The packet size .
*     packet_buf_p(in) u_int16_t*
*     Pointer to the start of the packet befor the LRH field
*     LNH(in) LNH_t
*     Packet kind IBA_GLOBAL LOCAL (RAW GRH) (RAW RWH)
*
*  Returns:
*  CALC ICRC (u_int32_t)
******************************************************************************/
u_int32_t
update_ICRC(u_int8_t *byte, u_int16_t size, LNH_t LNH);

/******************************************************************************
*  Function: Update VCRC
*
*  Description: This function is updating The VCRC using the crc16 .
*  table for a fast calcculation.
*
*   all The parametrs are:
*     packet_size(in) u_int8_t
*     The packet size .
*     packet_buf_p(in) u_int16_t*
*     Pointer to the start of the packet befor the LRH field
*
*  Returns:
* CALC VCRC (u_int16_t)
******************************************************************************/
u_int16_t
update_VCRC(u_int8_t *byte, u_int16_t size);

/******************************************************************************
*  Function: Cheak VCRC
*
*  Description: This function is cheaking The VCRC using the crc16 .
*  table for a fast calcculation and extract_VCRC function for compering.
*  the 2 results.
*
*   all The parametrs are:
*     pkt_st_p(in) IB_PKT_ST *
*     General struct packet .
*     packet_start_p(in) u_int16_t*
*     Pointer to the start of the packet befor the LRH field
*
*  Returns:
*        MT_OK results the same .
*        MT_ERROR .
******************************************************************************/
call_result_t
check_VCRC(IB_PKT_st *pkt_st_p, u_int16_t *packet_start_p);

/******************************************************************************
*  Function: Cheak ICRC
*
*  Description: This function is cheaking The ICRC using the crc16 .
*  table for a fast calcculation and extract_ICRC function for compering.
*  the 2 results.
*
*   all The parametrs are:
*     pkt_st_p(in) IB_PKT_ST *
*     General struct packet .
*     packet_start_p(in) u_int16_t*
*     Pointer to the start of the packet befor the LRH field
*
*  Returns:
*        MT_OK
*        MT_ERROR.
******************************************************************************/
call_result_t
check_ICRC(IB_PKT_st *pkt_st_p, u_int16_t *packet_start_p);

#endif /* internal function */
