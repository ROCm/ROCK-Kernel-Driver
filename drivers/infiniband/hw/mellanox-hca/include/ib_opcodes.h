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



#ifndef __IB_OPCODES_H
#define __IB_OPCODES_H

/***********************************************/
/*   Define all base transport OpCode fields   */
/***********************************************/

typedef u_int8_t IB_opcode_t;

typedef enum{
 
   IB_ST_RC = 0, /* Reliable Connection (RC).   */
 
   IB_ST_UC = 1, /* Unreliable Connection (UC). */
 
   IB_ST_RD = 2, /* Reliable Datagram (RD).     */
 
   IB_ST_UD = 3  /* Unreliable Datagram (UD).   */
 
} IB_service_type_t;                   
/***********************************************/
/* reliable Connection (RC)                  */
/***********************************************/
#define RC_SEND_FIRST_OP          0x00
#define RC_SEND_MIDDLE_OP         0x01
#define RC_SEND_LAST_OP           0x02
#define RC_SEND_LAST_W_IM_OP      0x03
#define RC_SEND_ONLY_OP           0x04
#define RC_SEND_ONLY_W_IM_OP      0x05

#define RC_WRITE_FIRST_OP         0x06
#define RC_WRITE_MIDDLE_OP        0x07
#define RC_WRITE_LAST_OP          0x08
#define RC_WRITE_LAST_W_IM_OP     0x09
#define RC_WRITE_ONLY_OP          0x0A
#define RC_WRITE_ONLY_W_IM_OP     0x0B

#define RC_READ_REQ_OP            0x0C
#define RC_READ_RESP_FIRST_OP     0x0D
#define RC_READ_RESP_MIDDLE_OP    0x0E
#define RC_READ_RESP_LAST_OP      0x0F
#define RC_READ_RESP_ONLY_OP      0x10

#define RC_ACKNOWLEDGE_OP         0x11
#define RC_ATOMIC_ACKNOWLEDGE_OP  0x12

#define RC_CMP_SWAP_OP            0x13
#define RC_FETCH_ADD_OP           0x14

/***********************************************/
/* Unreliable Connection (UC)                  */
/***********************************************/

#define UC_SEND_FIRST_OP          0x20
#define UC_SEND_MIDDLE_OP         0x21
#define UC_SEND_LAST_OP           0x22
#define UC_SEND_LAST_W_IM_OP      0x23
#define UC_SEND_ONLY_OP           0x24
#define UC_SEND_ONLY_W_IM_OP      0x25

#define UC_WRITE_FIRST_OP         0x26
#define UC_WRITE_MIDDLE_OP        0x27
#define UC_WRITE_LAST_OP          0x28
#define UC_WRITE_LAST_W_IM_OP     0x29
#define UC_WRITE_ONLY_OP          0x2A
#define UC_WRITE_ONLY_W_IM_OP     0x2B

/***********************************************/
/* Reliable Datagram (RD)                      */
/***********************************************/

#define RD_SEND_FIRST_OP          0x40
#define RD_SEND_MIDDLE_OP         0x41
#define RD_SEND_LAST_OP           0x42
#define RD_SEND_LAST_W_IM_OP      0x43
#define RD_SEND_ONLY_OP           0x44
#define RD_SEND_ONLY_W_IM_OP      0x45

#define RD_WRITE_FIRST_OP         0x46
#define RD_WRITE_MIDDLE_OP        0x47
#define RD_WRITE_LAST_OP          0x48
#define RD_WRITE_LAST_W_IM_OP     0x49
#define RD_WRITE_ONLY_OP          0x4A
#define RD_WRITE_ONLY_W_IM_OP     0x4B

#define RD_READ_REQ_OP            0x4C
#define RD_READ_RESP_FIRST_OP     0x4D
#define RD_READ_RESP_MIDDLE_OP    0x4E
#define RD_READ_RESP_LAST_OP      0x4F
#define RD_READ_RESP_ONLY_OP      0x50

#define RD_ACKNOWLEDGE_OP         0x51
#define RD_ATOMIC_ACKNOWLEDGE_OP  0x52

#define RD_CMP_SWAP_OP            0x53
#define RD_FETCH_ADD_OP           0x54

/***********************************************/
/* Unreliable Datagram (UD)                    */
/***********************************************/

#define UD_SEND_ONLY_OP           0x64
#define UD_SEND_ONLY_W_IM_OP      0x65



#endif /* __IB_OPCODES_H */
