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

/* MPGA Includes */
#include <mpga.h>
#include <packet_append.h>
#include <internal_functions.h>
#include <MPGA_headers.h>


#define nMPGA_MT_BIT_OFFSET(reg_path) \
    ((MT_offset_t) &( ((union MPGA_headers_p_t *)(0))-> (reg_path) ))
#define nMPGA_MT_BIT_SIZE(reg_path) \
    ((MT_size_t) sizeof( ((union MPGA_headers_p_t *)(0))-> (reg_path) ))
#define nMPGA_MT_BIT_OFFSET_SIZE(reg_path) \
            nMPGA_MT_BIT_OFFSET(reg_path) , nMPGA_MT_BIT_SIZE(reg_path)

/*
#define MT_BIT_OFFSET(struct_ancore,reg_path) \
    ((MT_offset_t) &( ((struct (struct_ancore), *)(0))-> (reg_path) ))
#define MT_BIT_SIZE(reg_path) \
    ((MT_size_t) sizeof( ((struct (struct_ancore) *)(0))-> (reg_path) ))
#define MT_BIT_OFFSET_SIZE(struct_ancore,reg_path) \
            nMPGA_MT_BIT_OFFSET((struct_ancore),(reg_path)) , nMPGA_MT_BIT_SIZE((struct_ancore),(reg_path))

*/


/******************************************************************************************
* Function: MPGA_make_headers
*
* Description: Packs the headers according to the given opcode into the given buffer
*              , ICRC/VCRC will be added if asked . The packing does not modify the
*              headers themselves or in their packed state.
*   
* Supported types:
*              All IBA_LOCAL and IBA_GLOBAL except:
*                    ATOMIC_ACKNOWLEDGE_OP, RD_CMP_SWAP_OP, RD_FETCH_ADD_OP
*
*  Parameters: 
*   
*  MPGA_headers_t  *MPGA_headers_p (IN) - Pointer to union which include the headers of the packet.
*  IB_opcode_t     opcode          (IN) - The headers will be build accordinly to the opcode
*  LNH_t           LNH             (IN) - idetify next header (e.g IBA_LOCAL)
*  MT_bool            CRC             (IN) - if true then ICRC and VCRC will be added after the payload
*                                         In this case the payload length should be provided
*                                         And the packet should be contiguous(*) in the buffer.
*                                         (*):packet_p (explained later) should point to a buffer with 
*                                         sufficiant space before it for the headers (as usuall),
*                                         payload immediatly after it and after the payload 6 allocated free bytes 
*                                         for the I/VCRC.
*  u_int16_t      payload_size     (IN) - Used only if CRC==true
*  u_int8_t       packet_p         (OUT)- Pointer to pre-alocated buffer - SHOULDN'T point to the buffer start,
*                                         instead should have 126 free and allocated bytes before it,this is
*                                         where the headers will be written.the headers end will be where 
*                                         the given pointer is.the pointer will be modified to point to 
*                                         the start of the packed headers.(READ the NOTE!)
*                                       Example:
*                                       Before: | 126 bytes buffer  P          |
*                                                                   ^Pointer
*
*                                       After:  |        P(P.H)-(P.H)          |
*                                                 Pointer^||||||||||
*                                                         Packed Headers   
*                                 NOTE FOR ADVANCED USERS: the headers will be built from this pointer 
*                                       backwards. a good way to use it is give a pointer with atleast 
*                                       128 bytes free for use behind it,so it would fit to any kind 
*                                       of headers.If one wishes to allocate exactly the space needed 
*                                       he can use the function MPGA_get_headers_size
*
*  Returns:
*    MT_OK
*    MT_ERROR
*
*****************************************************************************/                  
call_result_t MPGA_make_headers(MPGA_headers_t *MPGA_headers_p,   /*pointer to a headers union*/
                                IB_opcode_t opcode,
                                LNH_t LNH,
                                MT_bool CRC,
                                u_int16_t payload_size,
                                u_int8_t **packet_p_p);  /* pointer to packet buffer*/
/*********************************************************************************
* Function: MPGA_make_fast
*
*Description: Generaly the same as MPGA_make_headers, with some enhancment.
*             LNH,pad_count and packet length field are filled automaticaly.
*             The packet will be build acording to given fields but will overrun the automatic calculated fields.
*  MPGA_headers_t  *MPGA_headers_p (IN) - Pointer to union which include the headers of the packet.
*  LNH_t           LNH             (IN) - idetify next header (e.g IBA_LOCAL)
*  u_int16_t      payload_size     (IN) - Used only if CRC==true
*  u_int8_t       packet_p         (OUT)- Pointer to pre-alocated buffer - SHOULDN'T point to the buffer start,
*********************************************************************************/
call_result_t MPGA_make_fast(MPGA_headers_t *MPGA_headers_p, /*pointer to headers union*/
                             LNH_t LNH,
                             u_int16_t payload_size,
                             u_int8_t **packet_p_p);

/******************************************************************************************
* Function: MPGA_set_field
*
* Description: updates a field within a packed packet,the field is stated using
*              bit_offset (counting from the packet start) and bit_size.
*              It's advicable to use the macro nMPGA_MT_BIT_OFFSET_SIZE or like.
*   
*  Parameters: 
*  u_int8_t       *packet     (IN) pointer to packet buffer
*  MT_offset_t    bit_offset  (IN)        
*  MT_size_t      bit_size    (IN)           
*  u_int32_t      data        (OUT)                        
*
*  Returns:
*    MT_OK
*    MT_ERROR
*
*****************************************************************************/
call_result_t MPGA_set_field(u_int8_t *packet, /*pointer to packet buffer*/
                             MT_offset_t bit_offset,/*bit offset*/
                             MT_size_t bit_size, /*bit size*/
                             u_int32_t data);

/******************************************************************************************
* Function: MPGA_read_field
*
* Description: read a field within a packed packet,the field is stated using
*              bit_offset (counting from the packet start) and bit_size.
*              It's advicable to use the macro nMPGA_MT_BIT_OFFSET_SIZE or like.
*   
*  Parameters: 
*  u_int8_t       *packet     (IN) pointer to packet buffer
*  MT_offset_t    bit_offset  (IN)        
*  MT_size_t      bit_size    (IN)           
*  u_int32_t      *data       (OUT)                        
*
*  Returns:
*    MT_OK
*    MT_ERROR
*
*****************************************************************************/
call_result_t MPGA_read_field(u_int8_t *packet, /*pointer to packet buffer*/
                              MT_offset_t bit_offset,/*bit offset*/
                              MT_size_t bit_size, /*bit size*/
                              u_int32_t *data);

/******************************************************************************************
* Function: MPGA_new_from_old
*
* Description:  Copies the headers of an already packed packet to a new buffer
*               
* Supported types: same as MPGA_get_headers_size
*   
*  Parameters: 
*  u_int8_t       *old_packet    pointer to the buffer where the old headers are
*  u_int8_t       *new_packet    pointer to the buffer where the new headers should be
*  u_int16_t      buffer_size    total byte size allocated for headers starting from packet_p
*                                 will be used to avoid illegal memory access
*
*  Returns:
*    MT_OK
*    MT_ERROR
*
*****************************************************************************/
call_result_t MPGA_new_from_old(u_int8_t *old_packet,  /*pointer to the buffer where the old headers are*/
                                u_int8_t *new_packet,  /*pointer to the buffer where the new headers should be*/
                                u_int16_t buffer_size);/*total byte size allocated for headers starting from packet_p*/
                                                       /*will be used to avoid illegal memory access*/
/***************************************************************************************
 * Function: MPGA_get_headers_size
 * Description: Returns the size of the buffer that is need to hold a given packet
 *              NOTE: This isn't equal to the Pkt_Len field in LRH header, no padding is
 *                    added and VCRC shouldn't be counted (if not explicitly asked)
 * supported types: IBA_LOCA - send and RDMA_write
 * parameters:
 *    IB_opcode_t        opcode (IN)
 *    LNH_t              LNH    (IN)
 *    u_int16_t          payload_len (IN)
 *    MT_bool               icrc  (IN)  if set - icrc exist 
 *    MT_bool               vcrc  (IN)  if set - vcrc exist
 *    u_int16_t          *packet_len(OUT) packet length in bytes
 * Returns:
 *  MT_OK
 *  MT_ERROR
 ***************************************************************************************/
call_result_t MPGA_get_headers_size(IB_opcode_t opcode,
                                    LNH_t LNH,
                                    u_int16_t payload_len, 
                                    MT_bool icrc, /*if set - icrc exist*/
                                    MT_bool vcrc, /*if set - vcrc exist*/
                                    u_int16_t *packet_len); /*packet length in bytes*/

/***************************************************************************************
 * Function: MPGA_extract_LNH
 * Description: extract the LNH field from a given packed packet (pointer is to the packet start)
 * Parameters: 
 * u_int8_t     *packet (OUT)  pointer to packet buffer
 * LNH_t        *LNH    (IN/OUT) will be modified (but not allocated!)                             
 * Returns:
 * MT_OK
 * MT_ERROR
 ***************************************************************************************/
call_result_t MPGA_extract_LNH(u_int8_t *packet, /*pointer to packet buffer*/
                               LNH_t *LNH);

/***************************************************************************************
 * Function: MPGA_extract_opcode
 * Description: extract the opcode field from a given packed packet (pointer is to the packet start)
 * Parameters: 
 * u_int8_t     *packet (OUT)  pointer to packet buffer
 * IB_opcode_t  *opcode (IN/OUT) will be modified (but not allocated!)                             
 * Returns:
 * MT_OK
 * MT_ERROR
***************************************************************************************/
call_result_t MPGA_extract_opcode(u_int8_t *packet,
                                  IB_opcode_t *opcode);

/***************************************************************************************
 * Function: MPGA_extract_PadCnt
 * Description: extract the PadCnt field from a given packed packet (pointer is to the packet start)
 * Parameters: 
 * u_int8_t     *packet (OUT)  pointer to packet buffer
 * u_int8_t     *PadCnt (IN/OUT) will be modified (but not allocated!)                             
 * Returns:
 * MT_OK
 * MT_ERROR
 ***************************************************************************************/
call_result_t MPGA_extract_PadCnt(u_int8_t *packet,
                                  u_int8_t *PadCnt);
