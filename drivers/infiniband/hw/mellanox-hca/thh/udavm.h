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

#if !defined(H_TUDAV_H)
#define H_TUDAV_H

#include <mtl_common.h>
#include <vapi_types.h>
#include <mosal.h>
#include <hh.h>
#include <thh.h>


/************************************************************************
 *  Function: THH_udavm_create
 *  
 *  Arguments:
 *  version_p - Version information (see ...) 
 *  ud_av_table_memkey
 *  ud_av_table 
 *  ud_av_table ud_av_table_sz
 *  uadvm_p - Allocated object 
 *  
 *  Returns:
 *  HH_OK 
 *  HH_EINVAL -Invalid parameters (NULLs) 
 *  HH_EAGAIN -Not enough resources to allocate object 
 *
 *  Description: 
 *  Create the THH_udavm_t class instance. 
 ************************************************************************/
 
extern HH_ret_t THH_udavm_create( /*IN */ THH_ver_info_t *version_p, 
                                  /*IN */ VAPI_lkey_t ud_av_table_memkey, 
                                  /*IN */ MT_virt_addr_t ud_av_table, 
                                  /*IN */ MT_size_t ud_av_table_sz,
                                  /*OUT*/ THH_udavm_t *udavm_p);


/************************************************************************
 *  Function: THH_udavm_destroy
 *  
    Arguments:
    udavm - object to destroy

    Returns:
    HH_OK 
    HH_EINVAL - Unknown object 

    Description: 
    Free associated memory resources of this object. 
 ************************************************************************/

extern HH_ret_t THH_udavm_destroy( /*IN */ THH_udavm_t udavm );


                  

/************************************************************************
 *  Function: THH_udavm_get_memkey
 *  
    Arguments:
    udavm - the object to work
    table_memkey_p - pointer to place the memory key of the registered table 

    Returns:
    HH_OK 
    HH_EINVAL - Memory key was not set yet (or NULL ptr.) or Unknown object 

    Description: 
    Return the memory key associated with UD AVs of this object. 
 ************************************************************************/

extern HH_ret_t THH_udavm_get_memkey( /*IN */ THH_udavm_t udavm, 
                                      /*IN */ VAPI_lkey_t *table_memkey_p );




/************************************************************************
 *  Function: THH_udavm_create_av
 *  
    Arguments:
    udavm -
    pd - PD of given UD AV 
    av_p - The address vector 
    ah_p - Returned address handle 
    
    Returns:
    HH_OK 
    HH_EINVAL - Invalid parameters 
    HH_EAGAIN - No available resources (UD AV entries) 
    
    Description: Create address handle for given UD address vector. 
    
 ************************************************************************/

extern HH_ret_t THH_udavm_create_av( /*IN */ THH_udavm_t udavm, 
                                     /*IN */ HH_pd_hndl_t pd, 
                                     /*IN */ VAPI_ud_av_t *av_p, 
                                     /*OUT*/ HH_ud_av_hndl_t *ah_p );



/************************************************************************
 *  Function: THH_udavm_modify_av
 *  
    Arguments:
    udavm 
    ah - The address handle of UD AV to modify 
    av_p - The updated UD AV 

    Returns:
    HH_OK 
    HH_EINVAL - Invalid parameters 
    HH_EINVAL_AV_HNDL - Invalid address handle (no such handle) 

    Description: 
    Modify the UD AV entry associated with given address handle. 
    
 ************************************************************************/

extern HH_ret_t THH_udavm_modify_av( /*IN */ THH_udavm_t udavm, 
                                     /*IN */ HH_ud_av_hndl_t ah, 
                                     /*IN */ VAPI_ud_av_t *av_p );


/************************************************************************
 *  Function: THH_udavm_query_av
 *  

    Arguments:
    udavm 
    ah - The address handle of UD AV to query 
    av_p - The UD AV associated with given handle 
    
    Returns:
    HH_OK 
    HH_EINVAL - Invalid parameters 
    HH_EINVAL_AV_HNDL - Invalid address handle (no such handle) 
    
    Description: 
    Get the UD AV associated with given address handle.

 ************************************************************************/
extern HH_ret_t THH_udavm_query_av( /*IN */ THH_udavm_t udavm, 
                                    /*IN */ HH_ud_av_hndl_t ah, 
                                    /*OUT*/ VAPI_ud_av_t *av_p );


/************************************************************************
 *  Function: THH_udavm_destroy_av
 *  
    Arguments:
    udavm 
    ah - The address handle of UD AV to destroy 
    
    Returns:
    HH_OK 
    HH_EINVAL - Invalid udavm 
    HH_EINVAL_AV_HNDL - Invalid address handle (no such handle) 
    
    Description: 
    Free UD AV entry associated with given address handle. 
 **************************************************************************/
extern HH_ret_t THH_udavm_destroy_av( /*IN */ THH_udavm_t udavm, 
                                      /*IN */ HH_ud_av_hndl_t ah );


extern HH_ret_t THH_udavm_parse_udav_entry(u_int32_t *ud_av_p, 
                                          VAPI_ud_av_t *av_p);



#endif /* H_TUDAV_H */
