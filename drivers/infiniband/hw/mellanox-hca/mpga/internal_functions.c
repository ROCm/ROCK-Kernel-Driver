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


#ifndef MT_KERNEL

    #include <stdio.h>
    #include <stdlib.h>

#endif  /* MT_KERNEL */      

/* MPGA Includes */ 
#include <internal_functions.h>

/* Layers Includes */
#include <mtl_common.h>
#ifdef __WIN__
#include <mosal.h>
#endif



/*For the calc of the ICRC */
static u_int32_t crc32_table[256] = { /* The Polynomial used is 0x04C11DB7 seed 0xFFFFFFFF */
0x00000000,  0x77073096,  0xEE0E612C,  0x990951BA,  0x076DC419,  0x706AF48F,  0xE963A535,  0x9E6495A3,
0x0EDB8832,  0x79DCB8A4,  0xE0D5E91E,  0x97D2D988,  0x09B64C2B,  0x7EB17CBD,  0xE7B82D07,  0x90BF1D91,
0x1DB71064,  0x6AB020F2,  0xF3B97148,  0x84BE41DE,  0x1ADAD47D,  0x6DDDE4EB,  0xF4D4B551,  0x83D385C7,
0x136C9856,  0x646BA8C0,  0xFD62F97A,  0x8A65C9EC,  0x14015C4F,  0x63066CD9,  0xFA0F3D63,  0x8D080DF5,
0x3B6E20C8,  0x4C69105E,  0xD56041E4,  0xA2677172,  0x3C03E4D1,  0x4B04D447,  0xD20D85FD,  0xA50AB56B,
0x35B5A8FA,  0x42B2986C,  0xDBBBC9D6,  0xACBCF940,  0x32D86CE3,  0x45DF5C75,  0xDCD60DCF,  0xABD13D59,
0x26D930AC,  0x51DE003A,  0xC8D75180,  0xBFD06116,  0x21B4F4B5,  0x56B3C423,  0xCFBA9599,  0xB8BDA50F,
0x2802B89E,  0x5F058808,  0xC60CD9B2,  0xB10BE924,  0x2F6F7C87,  0x58684C11,  0xC1611DAB,  0xB6662D3D,
0x76DC4190,  0x01DB7106,  0x98D220BC,  0xEFD5102A,  0x71B18589,  0x06B6B51F,  0x9FBFE4A5,  0xE8B8D433,
0x7807C9A2,  0x0F00F934,  0x9609A88E,  0xE10E9818,  0x7F6A0DBB,  0x086D3D2D,  0x91646C97,  0xE6635C01,
0x6B6B51F4,  0x1C6C6162,  0x856530D8,  0xF262004E,  0x6C0695ED,  0x1B01A57B,  0x8208F4C1,  0xF50FC457,
0x65B0D9C6,  0x12B7E950,  0x8BBEB8EA,  0xFCB9887C,  0x62DD1DDF,  0x15DA2D49,  0x8CD37CF3,  0xFBD44C65,
0x4DB26158,  0x3AB551CE,  0xA3BC0074,  0xD4BB30E2,  0x4ADFA541,  0x3DD895D7,  0xA4D1C46D,  0xD3D6F4FB,
0x4369E96A,  0x346ED9FC,  0xAD678846,  0xDA60B8D0,  0x44042D73,  0x33031DE5,  0xAA0A4C5F,  0xDD0D7CC9,
0x5005713C,  0x270241AA,  0xBE0B1010,  0xC90C2086,  0x5768B525,  0x206F85B3,  0xB966D409,  0xCE61E49F,
0x5EDEF90E,  0x29D9C998,  0xB0D09822,  0xC7D7A8B4,  0x59B33D17,  0x2EB40D81,  0xB7BD5C3B,  0xC0BA6CAD,
0xEDB88320,  0x9ABFB3B6,  0x03B6E20C,  0x74B1D29A,  0xEAD54739,  0x9DD277AF,  0x04DB2615,  0x73DC1683,
0xE3630B12,  0x94643B84,  0x0D6D6A3E,  0x7A6A5AA8,  0xE40ECF0B,  0x9309FF9D,  0x0A00AE27,  0x7D079EB1,
0xF00F9344,  0x8708A3D2,  0x1E01F268,  0x6906C2FE,  0xF762575D,  0x806567CB,  0x196C3671,  0x6E6B06E7,
0xFED41B76,  0x89D32BE0,  0x10DA7A5A,  0x67DD4ACC,  0xF9B9DF6F,  0x8EBEEFF9,  0x17B7BE43,  0x60B08ED5,
0xD6D6A3E8,  0xA1D1937E,  0x38D8C2C4,  0x4FDFF252,  0xD1BB67F1,  0xA6BC5767,  0x3FB506DD,  0x48B2364B,
0xD80D2BDA,  0xAF0A1B4C,  0x36034AF6,  0x41047A60,  0xDF60EFC3,  0xA867DF55,  0x316E8EEF,  0x4669BE79,
0xCB61B38C,  0xBC66831A,  0x256FD2A0,  0x5268E236,  0xCC0C7795,  0xBB0B4703,  0x220216B9,  0x5505262F,
0xC5BA3BBE,  0xB2BD0B28,  0x2BB45A92,  0x5CB36A04,  0xC2D7FFA7,  0xB5D0CF31,  0x2CD99E8B,  0x5BDEAE1D,
0x9B64C2B0,  0xEC63F226,  0x756AA39C,  0x026D930A,  0x9C0906A9,  0xEB0E363F,  0x72076785,  0x05005713,
0x95BF4A82,  0xE2B87A14,  0x7BB12BAE,  0x0CB61B38,  0x92D28E9B,  0xE5D5BE0D,  0x7CDCEFB7,  0x0BDBDF21,
0x86D3D2D4,  0xF1D4E242,  0x68DDB3F8,  0x1FDA836E,  0x81BE16CD,  0xF6B9265B,  0x6FB077E1,  0x18B74777,
0x88085AE6,  0xFF0F6A70,  0x66063BCA,  0x11010B5C,  0x8F659EFF,  0xF862AE69,  0x616BFFD3,  0x166CCF45,
0xA00AE278,  0xD70DD2EE,  0x4E048354,  0x3903B3C2,  0xA7672661,  0xD06016F7,  0x4969474D,  0x3E6E77DB,
0xAED16A4A,  0xD9D65ADC,  0x40DF0B66,  0x37D83BF0,  0xA9BCAE53,  0xDEBB9EC5,  0x47B2CF7F,  0x30B5FFE9,
0xBDBDF21C,  0xCABAC28A,  0x53B39330,  0x24B4A3A6,  0xBAD03605,  0xCDD70693,  0x54DE5729,  0x23D967BF,
0xB3667A2E,  0xC4614AB8,  0x5D681B02,  0x2A6F2B94,  0xB40BBE37,  0xC30C8EA1,  0x5A05DF1B,  0x2D02EF8D
};

static u_int16_t crc16_table[256] = {  /* The Polynomial used is 0x100B seed 0xFFFF */
 0x0000, 0x1BA1, 0x3742, 0x2CE3, 0x6E84, 0x7525, 0x59C6, 0x4267,
 0xDD08, 0xC6A9, 0xEA4A, 0xF1EB, 0xB38C, 0xA82D, 0x84CE, 0x9F6F,
 0x1A01, 0x01A0, 0x2D43, 0x36E2, 0x7485, 0x6F24, 0x43C7, 0x5866,
 0xC709, 0xDCA8, 0xF04B, 0xEBEA, 0xA98D, 0xB22C, 0x9ECF, 0x856E,
 0x3402, 0x2FA3, 0x0340, 0x18E1, 0x5A86, 0x4127, 0x6DC4, 0x7665,
 0xE90A, 0xF2AB, 0xDE48, 0xC5E9, 0x878E, 0x9C2F, 0xB0CC, 0xAB6D,
 0x2E03, 0x35A2, 0x1941, 0x02E0, 0x4087, 0x5B26, 0x77C5, 0x6C64,
 0xF30B, 0xE8AA, 0xC449, 0xDFE8, 0x9D8F, 0x862E, 0xAACD, 0xB16C,
 0x6804, 0x73A5, 0x5F46, 0x44E7, 0x0680, 0x1D21, 0x31C2, 0x2A63,
 0xB50C, 0xAEAD, 0x824E, 0x99EF, 0xDB88, 0xC029, 0xECCA, 0xF76B,
 0x7205, 0x69A4, 0x4547, 0x5EE6, 0x1C81, 0x0720, 0x2BC3, 0x3062,
 0xAF0D, 0xB4AC, 0x984F, 0x83EE, 0xC189, 0xDA28, 0xF6CB, 0xED6A,
 0x5C06, 0x47A7, 0x6B44, 0x70E5, 0x3282, 0x2923, 0x05C0, 0x1E61,
 0x810E, 0x9AAF, 0xB64C, 0xADED, 0xEF8A, 0xF42B, 0xD8C8, 0xC369,
 0x4607, 0x5DA6, 0x7145, 0x6AE4, 0x2883, 0x3322, 0x1FC1, 0x0460,
 0x9B0F, 0x80AE, 0xAC4D, 0xB7EC, 0xF58B, 0xEE2A, 0xC2C9, 0xD968,
 0xD008, 0xCBA9, 0xE74A, 0xFCEB, 0xBE8C, 0xA52D, 0x89CE, 0x926F,
 0x0D00, 0x16A1, 0x3A42, 0x21E3, 0x6384, 0x7825, 0x54C6, 0x4F67,
 0xCA09, 0xD1A8, 0xFD4B, 0xE6EA, 0xA48D, 0xBF2C, 0x93CF, 0x886E,
 0x1701, 0x0CA0, 0x2043, 0x3BE2, 0x7985, 0x6224, 0x4EC7, 0x5566,
 0xE40A, 0xFFAB, 0xD348, 0xC8E9, 0x8A8E, 0x912F, 0xBDCC, 0xA66D,
 0x3902, 0x22A3, 0x0E40, 0x15E1, 0x5786, 0x4C27, 0x60C4, 0x7B65,
 0xFE0B, 0xE5AA, 0xC949, 0xD2E8, 0x908F, 0x8B2E, 0xA7CD, 0xBC6C,
 0x2303, 0x38A2, 0x1441, 0x0FE0, 0x4D87, 0x5626, 0x7AC5, 0x6164,
 0xB80C, 0xA3AD, 0x8F4E, 0x94EF, 0xD688, 0xCD29, 0xE1CA, 0xFA6B,
 0x6504, 0x7EA5, 0x5246, 0x49E7, 0x0B80, 0x1021, 0x3CC2, 0x2763,
 0xA20D, 0xB9AC, 0x954F, 0x8EEE, 0xCC89, 0xD728, 0xFBCB, 0xE06A,
 0x7F05, 0x64A4, 0x4847, 0x53E6, 0x1181, 0x0A20, 0x26C3, 0x3D62,
 0x8C0E, 0x97AF, 0xBB4C, 0xA0ED, 0xE28A, 0xF92B, 0xD5C8, 0xCE69,
 0x5106, 0x4AA7, 0x6644, 0x7DE5, 0x3F82, 0x2423, 0x08C0, 0x1361,
 0x960F, 0x8DAE, 0xA14D, 0xBAEC, 0xF88B, 0xE32A, 0xCFC9, 0xD468,
 0x4B07, 0x50A6, 0x7C45, 0x67E4, 0x2583, 0x3E22, 0x12C1, 0x0960
};

/*static u_int8_t test_array[] = {
0x70, 0x12, 0x37, 0x5C, 0x00, 0x0E, 0x17, 0xD2, 0x0A, 0x20, 0x24, 0x87,
0x00, 0x87, 0xB1, 0xB3, 0x00, 0x0D, 0xEC, 0x2A, 0x01, 0x71, 0x0A, 0x1C,
0x01, 0x5D, 0x40, 0x02, 0x38, 0xF2, 0x7A, 0x05, 0x00, 0x00, 0x00, 0x0E,
0xBB, 0x88, 0x4D, 0x85, 0xFD, 0x5C, 0xFB, 0xA4, 0x72, 0x8B, 0xC0, 0x69,
0x0E, 0xD4, 0x00, 0x00
};*/

/*static u_int8_t test_array2[] = {
0x70, 0x13, 0x37, 0x5C, 0x00, 0x18, 0x17, 0xD2, 0x60, 0x00, 0x00, 0x00, 0x00, 0x32,
0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x25, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x17, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x0A, 0x20, 0x24, 0x87, 0x00, 0x87, 0xB1, 0xB3,
0x00, 0x0D, 0xEC, 0x2A, 0x01, 0x71, 0x0A, 0x1C, 0x01, 0x5D, 0x40, 0x02, 0x38, 0xF2,
0x7A, 0x05, 0x00, 0x00, 0x00, 0x0E, 0xBB, 0x88, 0x4D, 0x85, 0xFD, 0x5C, 0xFB, 0xA4,
0x72, 0x8B, 0xC0, 0x69, 0x0E, 0xD4, 0x00, 0x00
};*/

/*static u_int8_t test_array_vcrc[] = {
0x70, 0x12, 0x37, 0x5C, 0x00, 0x0E, 0x17, 0xD2, 0x0A, 0x20, 0x24, 0x87,
0x00, 0x87, 0xB1, 0xB3, 0x00, 0x0D, 0xEC, 0x2A, 0x01, 0x71, 0x0A, 0x1C,
0x01, 0x5D, 0x40, 0x02, 0x38, 0xF2, 0x7A, 0x05, 0x00, 0x00, 0x00, 0x0E,
0xBB, 0x88, 0x4D, 0x85, 0xFD, 0x5C, 0xFB, 0xA4, 0x72, 0x8B, 0xC0, 0x69,
0x0E, 0xD4, 0x00, 0x00, 0x96, 0x25, 0xB7, 0x5A
};*/
/***********************************************************************************/
/*                             Allocate Packet                                     */
/***********************************************************************************/
call_result_t
allocate_packet(u_int16_t payload_size, u_int16_t *payload_buf_p,
                u_int16_t packet_size, u_int16_t **packet_buf_p)
{
 u_int8_t* temp_buffer_p,i;
 u_int8_t* start_of_payload_p;
 u_int8_t align;

 align = ((IBWORD - (payload_size % IBWORD)) % IBWORD);
 if(packet_size < payload_size) return MT_EINVAL;
	/*preventing the memcpy from writing on non alocated mem */

  if((temp_buffer_p = ALLOCATE(u_int8_t,packet_size))== NULL){ /* Allocting size in bytes*/
   MTL_ERROR('1', "\nfailed to allocate temp_buffer_p");
   return(MT_EKMALLOC);
  };

  for(i=0;i < align ;i++)
    {
     *(temp_buffer_p + (packet_size) - (i + 1)) = 0x00;
     /*Appending Zeros at the end of the packet to align to 4 byte long*/
    }

 start_of_payload_p = temp_buffer_p + (packet_size - payload_size - align );
  /*The address of the first byte in the packet payload*/
   MTL_TRACE('5', "\n the allocated mem is %p  start payload is %p",temp_buffer_p,start_of_payload_p);
  if(payload_buf_p != NULL){
  memcpy(start_of_payload_p,payload_buf_p, payload_size);
  /*Coping the given buffer (payload) to the end of th buffer living place for the header*/
 }

  MTL_TRACE('5', "\n start of payload[0] inside = %d",start_of_payload_p[0]);

 (*packet_buf_p) = (u_int16_t *)start_of_payload_p;/*init the given pointer*/

   MTL_TRACE('5', "\n *packet_buf[0] = %d",(*packet_buf_p)[0]);
   MTL_TRACE('5', "\n packet_buf_p is %p\n",(*packet_buf_p)); 
   
 return(MT_OK);
}

/***********************************************************************************/
/*                             Allocate Packet_LRH                                     */
/***********************************************************************************/
call_result_t
allocate_packet_LRH(u_int16_t TCRC_packet_size, u_int16_t t_packet_size,
                    u_int16_t *t_packet_buf_p, u_int16_t packet_size, u_int16_t **packet_buf_p)
{
 u_int8_t* temp_buffer_p;
 u_int8_t* end_of_LRH_p;
 if(packet_size < TCRC_packet_size) return MT_EINVAL;
	/*preventing the memcpy from writing on non alocated mem */

  if((temp_buffer_p = ALLOCATE(u_int8_t,(packet_size)))== NULL){ /* Allocting size in bytes*/
   MTL_ERROR('1', "\nfailed to allocate temp_buffer_p");
   return(MT_EKMALLOC);
  };

 end_of_LRH_p = temp_buffer_p + (packet_size - TCRC_packet_size);
	/*The address of the first byte in the packet payload*/

 if(t_packet_buf_p != NULL){
  memcpy(end_of_LRH_p, t_packet_buf_p, t_packet_size);
  /*Coping the given buffer (payload) to the end of th buffer living place for the header*/
 }

 (*packet_buf_p) = (u_int16_t *)end_of_LRH_p;/*init the given pointer*/

 return(MT_OK);
}

/***********************************************************************************/
/*                             init packet struct                                  */
/***********************************************************************************/
call_result_t
init_pkt_st(IB_PKT_st *pkt_st_p)
{
  pkt_st_p->lrh_st_p           = NULL;
  pkt_st_p->grh_st_p           = NULL;
  pkt_st_p->bth_st_p           = NULL;
  pkt_st_p->rdeth_st_p         = NULL;
  pkt_st_p->deth_st_p          = NULL;
  pkt_st_p->reth_st_p          = NULL;
  pkt_st_p->atomic_eth_st_p    = NULL;
  pkt_st_p->aeth_st_p          = NULL;
  pkt_st_p->atomic_acketh_st_p = NULL;
  pkt_st_p->payload_buf        = NULL;
  pkt_st_p->payload_size       = 0;/*Not a pointer like every one Not every one is perfect*/
  pkt_st_p->packet_size        = 0;
  return(MT_OK);
}

/***********************************************************************************/
/*                             is little endian                                    */
/***********************************************************************************/
u_int8_t
is_little_endian()
{
 int* p_2_int;
 u_int8_t* p_2_8_bit;
 
 p_2_int = ALLOCATE(int,1);
 *p_2_int = 1;
 p_2_8_bit =(u_int8_t *)p_2_int + (sizeof(int) - 1); /*higer byte*/
 
  if(*p_2_8_bit == 1){
         MTL_TRACE('3', "\n\n\n\n ********** This is a big endian machine **********\n\n\n\n");
   FREE(p_2_int);
         return(BIG_ENDIAN_TYPE);
  }else{
         MTL_TRACE('3', "\n\n\n\n ********** This is a little endian machine **********\n\n\n\n");
   FREE(p_2_int);
         return(LITTLE_ENDIAN_TYPE);
        }
}                                 

/***********************************************************************************/
/*                             little endain 16                                    */
/***********************************************************************************/
u_int16_t
little_endian_16(u_int8_t byte_0, u_int8_t byte_1)
{
  u_int8_t convert_arry[2];
  u_int16_t *p_2_16bit;

  convert_arry[0] = byte_0;
  convert_arry[1] = byte_1;

  p_2_16bit = (u_int16_t*)convert_arry;

  return(*(p_2_16bit));
}

/***********************************************************************************/
/*                             little endain 32                                    */
/***********************************************************************************/
u_int32_t
little_endian_32(u_int8_t byte_0, u_int8_t byte_1, u_int8_t byte_2, u_int8_t byte_3)
{
  u_int8_t convert_arry[4];
  u_int32_t *p_2_32bit;

  convert_arry[0] = byte_0;
  convert_arry[1] = byte_1;
  convert_arry[2] = byte_2;
  convert_arry[3] = byte_3;

  p_2_32bit = (u_int32_t*)convert_arry;

  return(*(p_2_32bit));
}

/***********************************************************************************/
/*                             little endain 64                                    */
/***********************************************************************************/
u_int64_t
little_endian_64(u_int8_t byte_0, u_int8_t byte_1, u_int8_t byte_2, u_int8_t byte_3,
                 u_int8_t byte_4, u_int8_t byte_5, u_int8_t byte_6, u_int8_t byte_7)
{
  u_int8_t convert_arry[8];
  u_int64_t *p_2_64bit;

  convert_arry[0] = byte_0;
  convert_arry[1] = byte_1;
  convert_arry[2] = byte_2;
  convert_arry[3] = byte_3;
  convert_arry[4] = byte_4;
  convert_arry[5] = byte_5;
  convert_arry[6] = byte_6;
  convert_arry[7] = byte_7;

  p_2_64bit = (u_int64_t*)convert_arry;

  return(*(p_2_64bit));
}

/***********************************************************************************/
/*                             fast calc ICRC                                      */
/***********************************************************************************/
u_int32_t
fast_calc_ICRC(u_int16_t packet_size, u_int16_t *packet_buf_p,LNH_t LNH)
{
  u_int8_t *start_ICRC;
  u_int8_t *packet_start;
  u_int32_t ICRC;

if((LNH != IBA_LOCAL) && (LNH != IBA_GLOBAL)) return (MT_OK);
   /* This is not a IBA trans port (No need for ICRC*/

  start_ICRC = (u_int8_t*)packet_buf_p +packet_size -VCRC_LEN  -ICRC_LEN;
  packet_start =(u_int8_t*) packet_buf_p;

  ICRC = update_ICRC((u_int8_t *)packet_start, (u_int16_t)(packet_size -VCRC_LEN -ICRC_LEN), LNH);
  /*ICRC = update_ICRC(test_array1, 52);*/
  return(ICRC);
}

/***********************************************************************************/
/*                             Update  ICRC                                        */
/***********************************************************************************/
u_int32_t
update_ICRC(u_int8_t *byte, u_int16_t size,LNH_t LNH) /* size of the buffer in bytes*/
{
    u_int8_t VL_mask = 0xF0;
    u_int8_t TClass_mask = 0x0F;
    u_int8_t reserved_mask = 0xFF;
    u_int16_t index = 0;
	  u_int32_t ICRC = 0xFFFFFFFF;


						
    if(LNH == IBA_LOCAL){
	    VL_mask = VL_mask | byte[0];    /*LRH field*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (VL_mask)];  /*masked 1111____*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[1]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[2]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[3]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[4]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[5]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[6]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[7]) ];
                                     /*BTH field*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[8]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[9]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[10]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[11]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^  reserved_mask ]; /*masked 11111111*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[13]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[14]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[15]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[16]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[17]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[18]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[19]) ];

      size = size - BTH_LEN - LRH_LEN;
      index = BTH_LEN + LRH_LEN;

          while(size--)
          {
             ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[index]) ];
             index ++;
          }
		}else{/*It is IBA Global*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ];  /*masking all the LRH field*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ]; /* 0 - 7 bytes*/

      TClass_mask = TClass_mask | byte[8]; /*GRH field 40 byte*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (TClass_mask)];  /*masked _ _ _ _ 1111*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ]; /*byte 9 masking the FLow Lable*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ]; /*end of masking FLow Lable*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[12]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[13]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[14]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (0xFF) ]; /*Masking the HopLmt 1111111*/

      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[16]) ]; /*SGID DGID*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[17]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[18]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[19]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[20]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[21]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[22]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[23]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[24]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[25]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[26]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[27]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[28]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[29]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[30]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[31]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[32]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[33]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[34]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[35]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[36]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[37]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[38]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[39]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[40]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[41]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[42]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[43]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[44]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[45]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[46]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[47]) ];
                              /*BTH field 12 bytes*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[48]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[49]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[50]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[51]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (reserved_mask)];/*masking reserved field*/
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[53]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[54]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[55]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[56]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[57]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[58]) ];
      ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[59]) ];

      size = size - LRH_LEN - GRH_LEN - BTH_LEN ;
      index = LRH_LEN + GRH_LEN + BTH_LEN;

          while(size--)  /*The rest of The packet*/
          {
             ICRC = (ICRC >> 8) ^ crc32_table[(ICRC & 0xFF) ^ (byte[index]) ];
             index ++;
          }
		}

  return (ICRC ^ 0xFFFFFFFF);
}


/***********************************************************************************/
/*                             fast calc VCRC                                      */
/***********************************************************************************/
u_int16_t
fast_calc_VCRC(u_int16_t packet_size, u_int16_t *packet_buf_p)
{
  u_int8_t *start_VCRC;
  u_int8_t *packet_start;
  u_int16_t VCRC = 0 ;

  start_VCRC = (u_int8_t*)packet_buf_p +packet_size -VCRC_LEN ;
  packet_start =(u_int8_t*) packet_buf_p;

  VCRC = update_VCRC((u_int8_t *)packet_start, (u_int16_t)(packet_size -VCRC_LEN));
  /*VCRC = update_VCRC(test_array_vcrc, 56);*/
  return(VCRC);
}
/***********************************************************************************/
/*                             update  VCRC                                        */
/***********************************************************************************/
u_int16_t
update_VCRC(u_int8_t *byte, u_int16_t size)
{
     u_int16_t VCRC = 0xFFFF; /*VCRC SEED */

      while( size-- ){
            VCRC = (VCRC >> 8) ^ crc16_table[(VCRC & 0xFF) ^ *byte++];
      }

     return(VCRC ^ 0xFFFF);
}

/***********************************************************************************/
/*                            Check  ICRC                                         */
/***********************************************************************************/
call_result_t
check_ICRC(IB_PKT_st *pkt_st_p, u_int16_t *packet_start_p)
{
       u_int32_t  extracted_ICRC;
       u_int32_t  calc_ICRC;
       u_int8_t* start_ICRC_p;
       LNH_t LNH;

       LNH = (pkt_st_p->lrh_st_p)->LNH;/*For calc ICRC */
       start_ICRC_p =(u_int8_t*)(packet_start_p) + ((pkt_st_p->lrh_st_p)->PktLen) * IBWORD - ICRC_LEN;

       extract_ICRC((u_int16_t*)start_ICRC_p, &extracted_ICRC);
       calc_ICRC = fast_calc_ICRC(pkt_st_p->packet_size, packet_start_p, LNH);
//       calc_ICRC = __be32_to_cpu(calc_ICRC);

       if(calc_ICRC != extracted_ICRC){
        MTL_TRACE('1', "\n** ERROR extracted ICRC  != calc ICRC  **\n");
        return(MT_ERROR);
       }else return(MT_OK);
}
/***********************************************************************************/
/*                             Check  VCRC                                         */
/***********************************************************************************/
call_result_t
check_VCRC(IB_PKT_st *pkt_st_p, u_int16_t *packet_start_p)
{
       u_int16_t  extracted_VCRC;
       u_int16_t  calc_VCRC;
       u_int8_t* start_VCRC_p;

       start_VCRC_p =(u_int8_t*)(packet_start_p) + ((pkt_st_p->lrh_st_p)->PktLen) * IBWORD ;

       extract_VCRC((u_int16_t*)start_VCRC_p, &extracted_VCRC);
       calc_VCRC = fast_calc_VCRC(pkt_st_p->packet_size, packet_start_p);
//       calc_VCRC = __be16_to_cpu(calc_VCRC);

       if(calc_VCRC != extracted_VCRC){
        MTL_TRACE('1', "\n** ERROR extracted VCRC  != calc VCRC  **\n");
        return(MT_ERROR);
       }else return(MT_OK);
}
