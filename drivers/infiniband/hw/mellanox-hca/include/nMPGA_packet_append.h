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


#ifndef H_nMPGA_PACKET_APPEND_H
#define H_nMPGA_PACKET_APPEND_H

/* Layers Includes */ 
#include <mtl_types.h>

/*******************/

#ifdef __WIN__
#include <string.h>
#endif

#if !defined(__DARWIN__) && defined(__LINUX__) && !defined(__KERNEL__)
  #include <endian.h>
#endif

#include <mpga.h>

/*Start of function declarations*/
/******************************************************************************
 *  Function: nMPGA_append_LRH
 *
 *  Description: This function is appending LRH to IB packets .
 *  To use this function you must have a LRH struct,
 *  with all the detailes to create the wanted packet.
 *  additionaly you should give a pointer for the new appended LRH
 *
 *  Parameters:
 *    IB_LRH_st   *lrh_st_p(IN)        Link next header .
 *    u_int8_t    *start_LRH_p(OUT)    preallocated buffer
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if no packet was generated.
 *****************************************************************************/
call_result_t
nMPGA_append_LRH(IB_LRH_st *lrh_st_p, u_int8_t *start_LRH_p);
/******************************************************************************
 *  Function: nMPGA_append_GRH
 *
 *  Description: This function is appending GRH to IB packets .
 *  To use this function you must have a GRH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with free space for the GRH field
 *
 *  Parameters:
 *    IB_GRH_st *grh_st_p(IN)    Global Route Header.
 *    u_int8_t *start_GRH_p(OUT) preallocated buffer.
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if the field was not appended.
 *****************************************************************************/
call_result_t
nMPGA_append_GRH(IB_GRH_st *grh_st_p,u_int8_t *start_GRH_p);

/******************************************************************************
 *  Function: nMPGA_append_BTH
 *
 *  Description: This function is appending BTH to IB packets .
 *  To use this function you must have a BTH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with free space for the BTH field
 *
 *  Parameters:
 *  IB_BTH_st *bth_st_p(out)  
 *  u_int8_t  *start_BTH_p(IN)
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if no packet was generated.
 *****************************************************************************/
call_result_t
nMPGA_append_BTH(IB_BTH_st *bth_st_p, u_int8_t *start_BTH_p);

/******************************************************************************
 *  Function: nMPGA_append_RETH
 *
 *  Description: This function is appending RETH to IB packets .
 *  To use this function you must have a RETH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with a free space for the RETH field.
 *
 *  Parameters:
 *  IB_RETH_st *reth_st_p(in)
 *  u_int8_t   *start_RETH_p(out)
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if no packet was generated.
 *****************************************************************************/
call_result_t
nMPGA_append_RETH(IB_RETH_st *reth_st_p, u_int8_t *start_RETH_p);

/******************************************************************************
 *  Function: nMPGA_append_AETH
 *
 *  Description: This function is appending AETH to IB packets .
 *  To use this function you must have a AETH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with a free space for the AETH field.
 *
 *  Parameters:
 *  IB_AETH_st *aeth_st_p(in)  
 *  u_int8_t   *start_AETH_p(out)
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if no packet was generated.
 *****************************************************************************/
call_result_t
nMPGA_append_AETH(IB_AETH_st *aeth_st_p, u_int8_t *start_AETH_p);

/*****************************************************************************/
/*                   From this point the function is Datagram                */
/*****************************************************************************/

/******************************************************************************
 *  Function: nMPGA_append_DETH
 *
 *  Description: This function is appending DETH to IB packets .
 *  To use this function you must have a DETH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with a free space for the DETH field.
 *
 *  Parameters:
 *  IB_DETH_st *deth_st_p(in)
 *  u_int8_t   *start_DETH_p(out)
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR .
 *****************************************************************************/
call_result_t
nMPGA_append_DETH(IB_DETH_st *deth_st_p, u_int8_t *start_DETH_p);

/******************************************************************************
 *  Function: nMPGA_append_RDETH
 *
 *  Description: This function is appending RDETH to IB packets .
 *  To use this function you must have a RDETH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with a free space for the RDETH field.
 *
 *  Parameters:
 *  IB_RDETH_st *deth_st_p(in)
 *  u_int8_t   *start_RDETH_p(out)
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR .
 *****************************************************************************/
call_result_t
nMPGA_append_RDETH(IB_RDETH_st *deth_st_p, u_int8_t *start_RDETH_p);

/******************************************************************************
 *  Function: nMPGA_append_ImmDt
 *
 *  Description: This function is appending ImmDt to IB packets .
 *  To use this function you must have a ImmDt struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with a free space for the ImmDt field.
 *
 *  Parameters:
 *  IB_ImmDt_st *ImmDt_st_p(in)
 *  u_int8_t    *start_ImmDt_p(out)
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR .
 *****************************************************************************/   
call_result_t
nMPGA_append_ImmDt(IB_ImmDt_st *ImmDt_st_p, u_int8_t *start_ImmDt_p);            

 /******************************************************************************
 *  Function: nMPGA_append_ICRC
 *
 *  Description: This function is appending the ICRC  to the  IB packets .
 *
 *  Parameters:
 *   start_ICRC(in) u_int16_t *
 *    pointer to the start of the ICRC field
 *   ICRC(in) u_int32_t
 *    The ICRC to insert
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
nMPGA_append_ICRC(u_int16_t *start_ICRC, u_int32_t ICRC);

  /******************************************************************************
 *  Function: nMPGA_append_VCRC
 *
 *  Description: This function is appending the VCRC  to the  IB packets .
 *
 *  Parameters:
 *   start_VCRC(in) u_int16_t *
 *    pointer to the start of the VCRC field
 *   VCRC(in) u_int32_t
 *    The VCRC to insert
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
nMPGA_append_VCRC(u_int16_t *start_VCRC, u_int16_t VCRC);

#endif /* H_nMPGA_PACKET_APPEND_H */
