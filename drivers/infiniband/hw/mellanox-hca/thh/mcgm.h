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

#if !defined(H_MCGM_H)
#define H_MCGM_H

#include <mtl_common.h>
#include <vapi_types.h>
#include <hh.h>
#include <thh.h>

typedef struct mcg_status_entry mcg_status_entry_t;

#define GID_2_HASH_KEY(gid) (*((u_int32_t *)gid) ^ *((u_int32_t *)gid+1) ^ *((u_int32_t *)gid+2) ^ *((u_int32_t *)gid+3))

#define THMCG_CHECK_HH_ERR(res, lable,message) \
    if ((res) != HH_OK) {                   \
        MTL_ERROR1(MT_FLFMT(message));     \
        goto lable;                        \
    }

#define THMCG_CHECK_CMDIF_ERR(res, lable,message) \
    if ((res) != THH_CMD_STAT_OK) {                   \
        MTL_ERROR1(MT_FLFMT(message));     \
        goto lable;                        \
    }

#define THMCG_CHECK_NULL(p, lable) \
    if ((p) == NULL) {                   \
        MTL_ERROR1(MT_FLFMT("Null pointer detected\n"));     \
        goto lable;                        \
    }




/************************************************************************
 *  Function: THH_mcgm_create
 *  
    Arguments:
      hob - THH_hob this object is included in 
      mght_total_entries - Number of entries in the MGHT 
      mght_hash_bins - Number of bins in the hash table 
      max_qp_per_mcg - Max number of QPs per Multicast Group 
      mcgm_p - Allocated THH_mcgm_t object 
    Returns:
      HH_OK 
      HH_EINVAL 
      HH_EAGAIN
       
   Description: Create THH_mcgm_t instance. 
   
 
 ************************************************************************/
 
extern HH_ret_t THH_mcgm_create(/*IN */ THH_hob_t hob, 
                                /*IN */ VAPI_size_t mght_total_entries, 
                                /*IN */ VAPI_size_t mght_hash_bins, 
                                /*IN */ u_int16_t max_qp_per_mcg, 
                                /*OUT*/ THH_mcgm_t *mcgm_p );

/************************************************************************
 *  Function: THH_mcgm_destroy
 *  
    Arguments:
      mcgm - THH_mcgm to destroy 
    
    Returns:
      HH_OK 
      HH_EINVAL 
    
    Description: 
      Free THH_mcgm context resources. 
 ************************************************************************/

extern HH_ret_t THH_mcgm_destroy( /*IN */ THH_mcgm_t mcgm );

/************************************************************************
 *  Function: THH_mcgm_attach_qp
 *  
    Arguments:
    mcgm 
    qpn -QP number of QP to attach 
    mgid -GID of a multicast group to attach to
    
    Returns:
    HH_OK 
    HH_EINVAL 
    HH_EAGAIN - No more MGHT entries. 
    HH_2BIG_MCG_SIZE - Number of QPs attached to multicast groups exceeded") \
    HH_ERR - an error has occured
    
    Description: 
    Attach given QP to multicast with given DGID.
 
 ************************************************************************/

         
extern HH_ret_t THH_mcgm_attach_qp(/*IN */ THH_mcgm_t mcgm, 
                                    /*IN */ IB_wqpn_t qpn, 
                                    /*IN */ IB_gid_t mgid );
/************************************************************************
 *  Function: THH_mcgm_detach_qp
 *  
    Arguments:
    mcgm 
    qpn - QP number of QP to attach 
    mgid - GID of a multicast group to attach to 
    
    Returns:
    HH_OK 
    HH_EINVAL - No such multicast group or given QP i not in given group 
    HH_ERR - an error has occured
    
    
    Description: 
    Detach given QP from multicast group with given GID. 
    
 ************************************************************************/

extern HH_ret_t THH_mcgm_detach_qp(/*IN */ THH_mcgm_t mcgm, 
                                   /*IN */ IB_wqpn_t qpn, 
                                   /*IN */ IB_gid_t mgid);  

/************************************************************************
 *  Function: THMCG_get_num_mcgs
 *  
    Arguments:
    mcgm 
    num_mcgs_p - current number of multicast groups 
    
    Returns:
    HH_OK 
    HH_EINVAL - invalid mcgm handle 
    
    
    Description: 
    
 ************************************************************************/
extern HH_ret_t THH_mcgm_get_num_mcgs(THH_mcgm_t mcgm, u_int32_t *num_mcgs_p);

#endif /* H_MCGM_H */
