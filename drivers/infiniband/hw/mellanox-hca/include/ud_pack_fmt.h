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
#ifndef H_IBPACKET_H
#define H_IBPACKET_H

#define IBPCK_GRH_IPVER 6
#define IBPCK_GRH_NEXT_HEADER 0x1B

/* 5.2.5 Table 8 */

typedef struct datagram_extended_transport_header_le {
    unsigned int	Q_Key;


#ifdef MT_LITTLE_ENDIAN
/* --------------------------------------------------------- */
    unsigned int	SrcQP:24;
    unsigned int	:8;
/* --------------------------------------------------------- */
#else
/* --------------------------------------------------------- */
    unsigned int	:8;
    unsigned int	SrcQP:24;
/* --------------------------------------------------------- */
#endif
} IBPCK_deth_le_t;  

/* 5.2.3 Table 6 */

typedef struct base_transport_header_le {
#ifdef MT_LITTLE_ENDIAN
    unsigned int	P_KEY:16;
    unsigned int	TVer:4;
    unsigned int	PadCnt:2;
    unsigned int	M:1;
    unsigned int	SE:1;
    unsigned int	OpCode:8;
/* --------------------------------------------------------- */
    unsigned int	DestQP:24;
    unsigned int	:8;
/* --------------------------------------------------------- */
    unsigned int	PSN:24;
    unsigned int	:7;
    unsigned int	A:1;
/* --------------------------------------------------------- */
#else
    unsigned int	OpCode:8;
    unsigned int	SE:1;
    unsigned int	M:1;
    unsigned int	PadCnt:2;
    unsigned int	TVer:4;
    unsigned int	P_KEY:16;
/* --------------------------------------------------------- */
    unsigned int	:8;
    unsigned int	DestQP:24;
/* --------------------------------------------------------- */
    unsigned int	A:1;
    unsigned int	:7;
    unsigned int	PSN:24;
/* --------------------------------------------------------- */
#endif
} IBPCK_bth_le_t; 

/* 5.2.1 Table 4 */

typedef struct local_route_header_le {
#ifdef MT_LITTLE_ENDIAN
    unsigned int	DLID:16;
    unsigned int	LNH:2;
    unsigned int	:2;
    unsigned int	SL:4;
    unsigned int	LVer:4;
    unsigned int	VL:4;
/* --------------------------------------------------------- */
    unsigned int	SLID:16;
    unsigned int	PktLen:11;
    unsigned int	:5;
/* --------------------------------------------------------- */
#else
    unsigned int	VL:4;
    unsigned int	LVer:4;
    unsigned int	SL:4;
    unsigned int	:2;
    unsigned int	LNH:2;
    unsigned int	DLID:16;
/* --------------------------------------------------------- */
    unsigned int	:5;
    unsigned int	PktLen:11;
    unsigned int	SLID:16;
/* --------------------------------------------------------- */
#endif
} IBPCK_lrh_le_t; 

/* 5.2.2 Table 5 */

typedef struct global_route_header_le {
#ifdef MT_LITTLE_ENDIAN
    unsigned int    flow_lable:20;
    unsigned int    traffic_class:8;
    unsigned int    IPvers:4;

    unsigned int    hop_limit:8;
    unsigned int    next_hdr:8;
    unsigned int    pay_len:16;
    
    unsigned int    sgid_3;
    unsigned int    sgid_2;
    unsigned int    sgid_1;
    unsigned int    sgid_0;

    unsigned int    dgid_3;
    unsigned int    dgid_2;
    unsigned int    dgid_1;
    unsigned int    dgid_0;
#else
    unsigned int    IPvers:4;
    unsigned int    traffic_class:8;
    unsigned int    flow_lable:20;

    unsigned int    pay_len:16;
    unsigned int    next_hdr:8;
    unsigned int    hop_limit:8;

    unsigned int    sgid_3;
    unsigned int    sgid_2;
    unsigned int    sgid_1;
    unsigned int    sgid_0;

    unsigned int    dgid_3;
    unsigned int    dgid_2;
    unsigned int    dgid_1;
    unsigned int    dgid_0;

#endif
} IBPCK_grh_le_t;

typedef struct
{
    struct local_route_header_le lrh;
    struct base_transport_header_le bth;
    struct datagram_extended_transport_header_le deth;

} IBPCK_local_udh_t;

typedef struct
{
    struct local_route_header_le lrh;
    struct global_route_header_le grh;
    struct base_transport_header_le bth;
    struct datagram_extended_transport_header_le deth;

} IBPCK_global_udh_t;



/*Define Link Next Header Definition */
typedef enum{
   IBPCK_RAW = 0x0,               /* |LRH|... (Etertype)*/
   IBPCK_IP_NON_IBA_TRANS = 0x1,  /* |LRH|GRH|...       */
   IBPCK_IBA_LOCAL = 0x2,         /* |LRH|BTH|...       */
   IBPCK_IBA_GLOBAL = 0x3         /* |LRH|GRH|BTH|...   */
} IBPCK_LNH_t;




#endif /* H_IB_PACKET_H */
