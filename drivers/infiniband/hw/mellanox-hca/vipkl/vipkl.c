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

#include "vipkl.h"
#include <vip_array.h>
#include <hobkl.h>
#include <srqm.h>
#include <cqm.h>
#include <mmu.h>
#include <pdm.h>
#include <devmm.h>
#include <em.h>
#include <vapi_common.h>
#include <vipkl_eq.h>
#include <vipkl_cqblk.h>


#define INC_HCA_RSC_CNT if (ret == VIP_OK){ \
                            VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);   \
                        }

#define DEC_HCA_RSC_CNT if (ret == VIP_OK){ \
                            VIP_array_find_release(hobs, hca_hndl); \
                        }               
#define VIP_WATERMARK 1536 /* 1.5K*/
/*============= static variables definitions ================*/
static VIP_array_p_t hobs= NULL;
static MOSAL_mutex_t open_hca_mtx;

/*************************************************************************
 * Function: VIPKL_open_hca
 *
 * Arguments:
 *  hca_id: The system specific HCA ID (device name)
 *  hca_hndl_p: VIP's handle to map into HOBKL object
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_ID: Unknown device or device is not HCA
 *  VIP_ENOSYS: Given device is not supported
 *  VIP_EAGAIN  
 *  
 *
 * Description:
 *  
 *  This function creates an HOBKL object and allocates a handle to map to it from user 
 *  space. An HCA-driver appropriate to the given device should be initilized succesfully 
 *  before creating HOBKL, the HOBKL is assoicated with specific HCA driver (when 
 *  more than one driver is supported, HH function pointers record is associated) and device 
 *  handle. Then HOBKL may be initialized with HCA capabilities for future referece 
 *  (assuming HCA cap. are static).
 *  
 * Note: HOBKL_create needs the HCA handle. Therefore I use insert_ptr
 * beforehand to allocate a handle, and then fill the entry through
 * the pointer returned. 
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_open_hca(/*IN*/ VAPI_hca_id_t hca_id,
                         /*IN*/ EVAPI_hca_profile_t  *profile_p,
                         /*OUT*/ EVAPI_hca_profile_t  *sugg_profile_p,
                         /*OUT*/ VIP_hca_hndl_t *hca_hndl_p)
{
  VIP_ret_t ret=VIP_OK;
  HOBKL_hndl_t hob_hndl;
  VIP_hca_hndl_t vip_hca_hndl;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  MTL_DEBUG3("Calling " "%s(0x%p, 0x%p);\n", __func__, hca_id, hca_hndl_p);
  if ((char *)hca_id == NULL) return VIP_EINVAL_HCA_ID;

  /* Make "opens" sequential (avoid duplicate opens). Also protect from cleanup. */
  if (MOSAL_mutex_acq(&open_hca_mtx,TRUE) != MT_OK)  return VIP_EAGAIN; 

  if (hobs == NULL) { /* Cleanup was invoked already */
    MTL_ERROR1(MT_FLFMT("VIPKL_open_hca invoked after VIPKL_cleanup"));
    ret= VIP_ENOSYS; /* "operation not supported" within this time frame */
    goto failed_hobs;
  }

  if ( VIP_array_get_num_of_objects(hobs) >= VIP_MAX_HCA ) {
    ret= VIP_EAGAIN;
    goto failed_max_hcas;
  }

  /* Test if this hca id is already in the array */
  ret = VIPKL_get_hca_hndl(hca_id, &vip_hca_hndl, NULL);
  if (ret == VIP_OK) {
    if (hca_hndl_p != NULL) *hca_hndl_p=vip_hca_hndl;
    ret= VIP_EBUSY;
    goto failed_already_open;
  }

  /* Create the HOBKL object. */
  ret = HOBKL_create(hca_id, profile_p, sugg_profile_p, &hob_hndl);
  if (ret != VIP_OK) {
    goto failed_hobkl;
  }
  MTL_DEBUG3("Inside " "%s: created HOBKL %s successfully;\n", __func__, hca_id);


  ret = VIP_array_insert(hobs, hob_hndl, &vip_hca_hndl);
  if (ret != VIP_OK) {
    goto failed_array;
  }

  /* Set HCA handle to be used with EM's callbacks */
  HOBKL_set_vapi_hca_hndl(hob_hndl,vip_hca_hndl); /* VAPI's HCA handle identical to VIP's */

  MOSAL_mutex_rel(&open_hca_mtx);
  if (hca_hndl_p != NULL) *hca_hndl_p= vip_hca_hndl;
  return VIP_OK;

  /* Failure cleanup */
  failed_array:
    HOBKL_destroy(hob_hndl);
  failed_hobkl:
  failed_already_open:
  failed_max_hcas:
  failed_hobs:
    MOSAL_mutex_rel(&open_hca_mtx);
    return ret;
}

/*************************************************************************
 * Function: VIPKL_close_hca
 *
 * Arguments:
 *  hca_hndl: HCA to close.
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_HNDL
 *  VIP_EBUSY: HCA is still used by processes (?)
 *  
 *  
 *
 * Description:
 *  
 *  This function destroy the HOBKL object associated with given handle and close the 
 *  device in the associated HCA driver.
 *  This function prevent closing HCA which is still in use (see process resource tracking - 
 *  PRT). Only VIP module cleanup may force destroying of HOBKL which are still in use 
 *  (resource  tracking - PRT - enables freeing of all resources, though still in use).
 *  
 *  
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_close_hca(/*IN*/ VIP_hca_hndl_t hca_hndl)
{
  VIP_ret_t ret=VIP_OK;
  HOBKL_hndl_t hob_hndl;
  MTL_DEBUG3("Inside " "%s\n", __func__);

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  
  ret = VIP_array_erase(hobs, hca_hndl, (VIP_array_obj_t *)&hob_hndl);
  if (ret != VIP_OK) {
    return ret;
  }

  if ( (ret=HOBKL_destroy(hob_hndl)) != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("HOBKL_destroy returned %s (possible cleanup problems)"), VAPI_strerror_sym((VAPI_ret_t)ret));
  } 
  
  /* handle is not in the array anymore so cleanup result is unimportant */
  return VIP_OK;
}

/*************************************************************************
 * Function: VIPKL_get_hca_hndl
 *
 * Arguments:
 *  hca_id: The system specific HCA ID (device name)
 *  hca_hndl_p: VIP's handle to map into HOBKL object
 *  hh_hndl_p: HH handle to map into HOBKL object
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EINVAL_HCA_ID: Given device is not opened.
 *  
 *  
 *
 * Description:
 *  
 *  When VIP in user space performs HCA_open_hca_ul it does not open an HCA but only 
 *  initilizes user space resources for working with given HCA. This function enables it to 
 *  get the HCA handle associated with given HCA ID.
 *  
 *  
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_get_hca_hndl(/*IN*/ VAPI_hca_id_t hca_id,
    /*OUT*/ VIP_hca_hndl_t *hca_hndl_p,
    /*OUT*/ HH_hca_hndl_t *hh_hndl_p
    )
{
  VIP_array_handle_t hdl;
  HOBKL_hndl_t hob_hndl;
  VIP_hca_id_t t_hca_id;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  if ((char *)hca_id == NULL) return VIP_EINVAL_HCA_ID;
  MTL_DEBUG3("Inside " "%s looking for %s\n", __func__, hca_id);
  
  if (hobs == NULL) {
    return VIP_EINVAL_HCA_ID;
  }
  
  ret= VIP_array_get_first_handle(hobs,&hdl,NULL);
  while (ret == VIP_OK) {
    /* Find object using "find_hold" in order to avoid removal of hobkl while refering it */
    if (VIP_array_find_hold(hobs, hdl, (VIP_array_obj_t*)&hob_hndl) == VIP_OK ) {
      HOBKL_get_hca_id(hob_hndl, &t_hca_id);
      if (strncmp(hca_id, t_hca_id, sizeof(VIP_hca_id_t))==0 ) {
        if ( hca_hndl_p ) *hca_hndl_p = hdl;
        if ( hh_hndl_p ) *hh_hndl_p = HOBKL_get_hh_hndl(hob_hndl);
        VIP_array_find_release(hobs, hdl);
        return VIP_OK;
      }
    }
    VIP_array_find_release(hobs, hdl);
    ret= VIP_array_get_next_handle(hobs,&hdl,NULL);
  }
  MTL_DEBUG3("Inside " "%s HCA %s not found\n", __func__, hca_id);
  return VIP_EINVAL_HCA_ID;
}

/*****************************************************************************
 * Function: VIPKL_list_hcas
 *
 * Arguments:
 *            hca_id_buf_sz(IN)    : Number of entries in supplied array 'hca_id_buf_p',
 *            num_of_hcas_p(OUT)   : Actual number of currently available HCAs
 *            hca_id_buf_p(OUT)    : points to an array allocated by the caller of 
 *                                   'VAPI_hca_id_t' items, able to hold 'hca_id_buf_sz' 
 *                                    entries of that item.

 *
 * Returns:   VIP_OK     : operation successful.
 *            VIP_EINVAL_PARAM : Invalid params.
 *            VIP_EAGAIN : hca_id_buf_sz is smaller than num_of_hcas.  In this case, NO hca_ids
 *                         are returned in the provided array.
 *
 * Description:
 *   Used to get a list of the device IDs of the available devices.
 *   These names can then be used in VAPI_open_hca to open each
 *   device in turn.
 *
 *   If the size of the supplied buffer is too small, the number of available devices
 *   is still returned in the num_of_hcas parameter, but the return code is set to
 *   VIP_EAGAIN.  In this case, the user's buffer is filled to capacity with 
 *   'hca_id_buf_sz' device IDs; to get more of the available device IDs, the the user may
 *   allocate a larger array and call this procedure again. (The user may call this function
 *   with hca_id_buf_sz = 0 and hca_id_buf_p = NULL to get the number of hcas currently
 *   available).
 *****************************************************************************/

VIP_ret_t VIPKL_list_hcas(/* IN*/ u_int32_t          hca_id_buf_sz,
                            /*OUT*/ u_int32_t*       num_of_hcas_p,
                            /*OUT*/ VAPI_hca_id_t*   hca_id_buf_p)
{
    HH_ret_t       ret;
    u_int32_t      nActual = 0, i, num_to_copy;
    HH_hca_hndl_t* hcas_buf;

    MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
    /* request only for number of available HCAs*/
    MTL_DEBUG4(MT_FLFMT("VIPKL_list_hcas: buf_sz = %u, in_buf = %p"), 
               hca_id_buf_sz, hca_id_buf_p);
    if (hca_id_buf_sz == 0 && hca_id_buf_p == NULL ) {
        ret = HH_list_hcas(0,num_of_hcas_p,NULL);
        if (ret == HH_EINVAL) {
            MTL_ERROR1(MT_FLFMT("HH_list_hcas returned HH_EINVAL (%d)"), ret);
            return  VIP_EINVAL_PARAM;
        }
        else{
            MTL_DEBUG4(MT_FLFMT("HH_list_hcas returned (%d), num_of_hcas = %u"), 
                       ret, *num_of_hcas_p);
            return ret;
        }
    }

    /* bad parameter check */
    if (hca_id_buf_sz == 0 || hca_id_buf_p == NULL ) {
        MTL_ERROR1(MT_FLFMT("failed bad parameter check"));
        return  VIP_EINVAL_PARAM;
    }
    
    /* allocate hca handles array for returned data */
    hcas_buf = (HH_hca_hndl_t *) MALLOC(sizeof(HH_hca_hndl_t) * hca_id_buf_sz);
    if (hcas_buf == NULL) {
        /* could not allocate memory */
        MTL_ERROR1(MT_FLFMT("Could not allocate memory"));
        return  VIP_EINVAL_PARAM;
    }

    ret=HH_list_hcas(hca_id_buf_sz,&nActual,hcas_buf);
    MTL_DEBUG4(MT_FLFMT("non-null query. HH_list_hcas returned (%d), num_of_hcas = %u"),
               ret,nActual); 
    if (ret != HH_OK && ret != HH_EAGAIN) {
        /* error.  Nothing is retrieved*/
        MTL_ERROR1(MT_FLFMT("Nothing retrieved. HH_list_hcas returned %d"), ret);
        ret = VIP_EINVAL_PARAM;
        goto list_err;
    }
    
    if (nActual == 0) {
        ret = VIP_OK;
        goto no_hcas;
    }
    
    /* we have a non-zero number of HCAs available.  Copy over the IDS
     * and return, using the value returned by HH_list_hcas
     */

    num_to_copy = (ret == HH_OK ? nActual :  hca_id_buf_sz);
    for (i = 0; i < num_to_copy; i++) {
        memset((char *)hca_id_buf_p[i], 0, sizeof(VAPI_hca_id_t));
        MTL_DEBUG4(MT_FLFMT("returned HCA ID[%d] = %s"), i, (hcas_buf[i])->dev_desc);
        strncpy(hca_id_buf_p[i], (hcas_buf[i])->dev_desc, HCA_MAXNAME-1); 
        MTL_DEBUG4(MT_FLFMT("After strncpy. HCA ID[%d] = %s"), i, hca_id_buf_p[i]);
    }

no_hcas:
    *num_of_hcas_p = nActual; /* valid result, even if rc != HH_OK */
list_err:
    FREE(hcas_buf);
    return ret;

}



/*************************************************************************
 * The following functions are the wrapper functions of VIP-KL.
 * These functions may be called directly in kernel space for kernel-modules which
 * use VIP-KL (i.e. their VIP-UL is running in kernel space),
 * or called via a system call wrapper from user space.
 *
 * All these functions are actually "shadows"
 * of functions of HOBKL and its contained objects (PDM,CQM,QPM,MMU,EM) but 
 * instead of the object handle a VIP_hca_hndl_t is provided.
 * These functions map from the HCA handle to the appropriate object and call
 * its function.
 * (so the documentation for these functions point to the called function).
 *************************************************************************/
 



/*************************************************************************
                        
                        HOBKL functions
 
 *************************************************************************/ 
/*************************************************************************
 * Function: VIPKL_alloc_ul_resources <==> HOBKL_alloc_ul_resources
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_alloc_ul_resources(VIP_hca_hndl_t hca_hndl, 
                                   MOSAL_protection_ctx_t usr_prot_ctx,
                                   MT_size_t hca_ul_resources_sz,
                                   void *hca_ul_resources_p,
                                   EM_async_ctx_hndl_t *async_hndl_ctx_p)

{
  HOBKL_hndl_t hob;
  VIP_ret_t  ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  if (!hca_ul_resources_p)
      return VAPI_EINVAL_VA;

  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }

  ret= HOBKL_alloc_ul_resources(hob, usr_prot_ctx, hca_ul_resources_p, async_hndl_ctx_p);
  
  INC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: VIPKL_free_ul_resources <==> HOBKL_free_ul_resources
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_free_ul_resources(
                                  VIP_hca_hndl_t hca_hndl, 
                                  MT_size_t hca_ul_resources_sz,
                                  void *hca_ul_resources_p,
                                  EM_async_ctx_hndl_t async_hndl_ctx)
{
  HOBKL_hndl_t hob;
  VIP_ret_t  ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  if (!hca_ul_resources_p)
      return VAPI_EINVAL_VA;

  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret= HOBKL_free_ul_resources(hob, hca_ul_resources_p, async_hndl_ctx);
  DEC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: VIPKL_query_hca_cap <==> HOBKL_query_cap
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_query_hca_cap(VIP_hca_hndl_t hca_hndl, VAPI_hca_cap_t *caps)
{
  HOBKL_hndl_t hob;
  VIP_ret_t  ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = HOBKL_query_cap(hob,caps);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: VIPKL_query_port_prop  <==> HOBKL_query_port_prop
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_query_port_prop(VIP_hca_hndl_t hca_hndl, IB_port_t port_num,
  VAPI_hca_port_t *port_props_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t  ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = HOBKL_query_port_prop(hob,port_num, port_props_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: VIPKL_query_port_gid_tbl  <==> HOBKL_query_port_gid_tbl
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_query_port_gid_tbl(VIP_hca_hndl_t hca_hndl, IB_port_t port_num,
  u_int16_t tbl_len_in, u_int16_t *tbl_len_out_p,
  VAPI_gid_t *gid_tbl_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret = VIP_OK;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  MTL_TRACE2("%s: hca_hndl=%d, port=%d, tbl_len_in=%d\ntbl_len_outp=%p, gid_tbl_p=%p\n", __func__,
                hca_hndl, port_num, tbl_len_in, (void *) tbl_len_out_p, (void *) gid_tbl_p);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = HOBKL_query_port_gid_tbl(hob, port_num, tbl_len_in, tbl_len_out_p, gid_tbl_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: VIPKL_query_port_pkey_tbl  <==> HOBKL_query_port_pkey_tbl
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_query_port_pkey_tbl(VIP_hca_hndl_t hca_hndl, IB_port_t port_num,
  u_int16_t tbl_len_in, u_int16_t *tbl_len_out_p,
  VAPI_pkey_t *pkey_tbl_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = HOBKL_query_port_pkey_tbl(hob,port_num, tbl_len_in, tbl_len_out_p, pkey_tbl_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: VIP_modify_hca_attr <==> HOBKL_modify_hca_attr
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_modify_hca_attr(VIP_hca_hndl_t hca_hndl, IB_port_t port_num,
  VAPI_hca_attr_t *hca_attr_p, VAPI_hca_attr_mask_t *hca_attr_mask_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = HOBKL_modify_hca_attr(hob,port_num, hca_attr_p, hca_attr_mask_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}


/*************************************************************************
 * Function: VIPKL_get_hh_hndl <==> HOBKL_get_hh_hndl
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_get_hh_hndl(VIP_hca_hndl_t hca_hndl, HH_hca_hndl_t *hh_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  if (hh_p) *hh_p=HOBKL_get_hh_hndl(hob);
  VIP_array_find_release(hobs, hca_hndl);
  return VIP_OK;
}

/*************************************************************************
 * Function: VIPKL_get_hca_ul_info <==> HOBKL_get_hca_ul_info
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_get_hca_ul_info(VIP_hca_hndl_t hca_hndl, HH_hca_dev_t *hca_ul_info_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t  ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  if (!hca_ul_info_p)
      return VAPI_EINVAL_VA;

  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret= HOBKL_get_hca_ul_info(hob, hca_ul_info_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}


/*************************************************************************
 * Function: VIPKL_get_hca_id <==> HOBKL_get_hca_id
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_get_hca_id(VIP_hca_hndl_t hca_hndl, VAPI_hca_id_t *hca_id_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t  ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = HOBKL_get_hca_id(hob, hca_id_p);

  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}



/*************************************************************************
                        PDM functions
 *************************************************************************/ 

/*************************************************************************
 * Function: VIP_create_pd <==> PDM_create_pd
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_create_pd(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, MOSAL_protection_ctx_t prot_ctx, 
                          MT_size_t pd_ul_resources_sz,
                        void * pd_ul_resources_p,PDM_pd_hndl_t *pd_hndl_p, HH_pd_hndl_t *pd_num_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t  ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = PDM_create_pd(usr_ctx,HOBKL_get_pdm(hob), prot_ctx, pd_ul_resources_p,pd_hndl_p, pd_num_p);
  INC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}


/*************************************************************************
 * Function: VIP_destroy_pd <==> PDM_destroy_pd
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_destroy_pd(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, PDM_pd_hndl_t pd_hndl)
{
  HOBKL_hndl_t hob;
  VIP_ret_t  ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = PDM_destroy_pd(usr_ctx,HOBKL_get_pdm(hob),pd_hndl);
  DEC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}




/*************************************************************************
                        
                        QPM functions
 
 *************************************************************************/ 

/******************************************************************************
 *  Function: VIPKL_create_qp <==> QPM_create_qp
 *****************************************************************************/
VIP_ret_t
VIPKL_create_qp (VIP_RSCT_t usr_ctx,
                 VIP_hca_hndl_t hca_hndl,
                 VAPI_qp_hndl_t vapi_qp_hndl,
                 EM_async_ctx_hndl_t async_hndl_ctx,
                 MT_size_t qp_ul_resources_sz,
                 void  *qp_ul_resources_p,
                 QPM_qp_init_attr_t *qp_init_attr_p,
                 QPM_qp_hndl_t *qp_hndl_p, 
                 VAPI_qp_num_t *qp_num_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed to find HCA 0x%X in hobs array"),__func__,hca_hndl);
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = QPM_create_qp(usr_ctx, HOBKL_get_qpm(hob), vapi_qp_hndl, async_hndl_ctx, 
                      qp_ul_resources_p, qp_init_attr_p, qp_hndl_p, qp_num_p);
  INC_HCA_RSC_CNT
  MTL_TRACE2("%s: returning from QPM_create_qp. ret = %d\n", __func__,ret);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/******************************************************************************
 *  Function: VIPKL_get_special_qp <==> QPM_get_special_qp
 *****************************************************************************/
VIP_ret_t
VIPKL_get_special_qp (VIP_RSCT_t            usr_ctx,
                      VIP_hca_hndl_t        hca_hndl,
                      VAPI_qp_hndl_t        vapi_qp_hndl, /* VAPI handle to the QP */
                      EM_async_ctx_hndl_t   async_hndl_ctx,
                      MT_size_t             qp_ul_resources_sz,
                      void                  *qp_ul_resources_p, 
                      IB_port_t             port,
                      VAPI_special_qp_t     qp_type,
                      QPM_qp_init_attr_t    *qp_init_attr_p, 
                      QPM_qp_hndl_t         *qp_hndl_p,
                      IB_wqpn_t             *sqp_hndl)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  MTL_TRACE2("%s: calling QPM_get_special_qp(...,type=%d,port=%d,...)\n", __func__,
    qp_type,port);
  ret = QPM_get_special_qp(usr_ctx,HOBKL_get_qpm(hob), vapi_qp_hndl, async_hndl_ctx,
                           qp_ul_resources_p, qp_type, port, qp_init_attr_p,qp_hndl_p, sqp_hndl);
  INC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/******************************************************************************
 *  Function: VIPKL_destroy_qp <==> QPM_destroy_qp
 *****************************************************************************/
VIP_ret_t
VIPKL_destroy_qp (VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, QPM_qp_hndl_t qp_hndl,MT_bool in_rsct_cleanup)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = QPM_destroy_qp(usr_ctx,HOBKL_get_qpm(hob), qp_hndl, in_rsct_cleanup);
  DEC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/******************************************************************************
 *  Function: VIPKL_modify_qp <==> QPM_modify_qp
 *****************************************************************************/


VIP_ret_t
VIPKL_modify_qp (VIP_RSCT_t usr_ctx,
                 VIP_hca_hndl_t      hca_hndl, 
                 QPM_qp_hndl_t       qp_hndl,
                 VAPI_qp_attr_mask_t *qp_mod_mask_p,
                 VAPI_qp_attr_t      *qp_mod_attr_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  MTL_DEBUG3(MT_FLFMT("ret=%d, qp_hndl=0x%x"), ret, qp_hndl);
  
#ifdef QPX_PRF
  prf_counter_init((&hob->pc_qpm_mod_qp));
  
  //prf_counter_start((&hob->pc_qpm_mod_qp));
#endif
  
  ret = QPM_modify_qp(usr_ctx,HOBKL_get_qpm(hob), qp_hndl, qp_mod_mask_p, qp_mod_attr_p);

#ifdef QPX_PRF
  //prf_counter_stop((&hob->pc_qpm_mod_qp));	
  if( ret == VAPI_OK ) {
	prf_counter_add((&hob->pc_qpm_mod_qp),
					  (&hob->pc_qpm_mod_qp_array[qp_mod_attr_p->prev_state][qp_mod_attr_p->qp_state]));
  }
#endif
  VIP_array_find_release(hobs, hca_hndl);
  MTL_DEBUG3(MT_FLFMT("ret=%d=%s"), ret, VAPI_strerror_sym(ret));
  return ret;
}

/******************************************************************************
 *  Function:  VIPKL_query_qp <==> QPM_query_qp
 *****************************************************************************/
VIP_ret_t
VIPKL_query_qp (VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, QPM_qp_hndl_t qp_hndl,
    QPM_qp_query_attr_t *qp_query_prop_p, VAPI_qp_attr_mask_t *qp_mod_mask_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = QPM_query_qp(usr_ctx,HOBKL_get_qpm(hob), qp_hndl, qp_query_prop_p, qp_mod_mask_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}


/*************************************************************************
                        
                        SRQM functions
 
 *************************************************************************/ 

/******************************************************************************
 *  Function: VIPKL_create_srq <==> SRQM_create_srq
 *****************************************************************************/
VIP_ret_t
VIPKL_create_srq (VIP_RSCT_t usr_ctx,
                 VIP_hca_hndl_t hca_hndl,
                  VAPI_srq_hndl_t   vapi_srq_hndl, 
                  PDM_pd_hndl_t     pd_hndl,
                  EM_async_ctx_hndl_t async_hndl_ctx,
                  MT_size_t           srq_ul_resources_sz,
                  void                *srq_ul_resources_p,
                  SRQM_srq_hndl_t     *srq_hndl_p )
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed to find HCA 0x%X in hobs array"),__func__,hca_hndl);
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = SRQM_create_srq(usr_ctx, HOBKL_get_srqm(hob), vapi_srq_hndl, pd_hndl, async_hndl_ctx,
                        srq_ul_resources_p, srq_hndl_p);
  INC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/******************************************************************************
 *  Function: VIPKL_destroy_srq <==> SRQM_destroy_srq
 *****************************************************************************/
VIP_ret_t
VIPKL_destroy_srq(VIP_RSCT_t usr_ctx,
                  VIP_hca_hndl_t hca_hndl,
                  SRQM_srq_hndl_t srq_hndl)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed to find HCA 0x%X in hobs array"),__func__,hca_hndl);
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = SRQM_destroy_srq(usr_ctx, HOBKL_get_srqm(hob), srq_hndl);
  DEC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/******************************************************************************
 *  Function: VIPKL_query_srq <==> SRQM_query_srq
 *****************************************************************************/
VIP_ret_t
VIPKL_query_srq(VIP_RSCT_t usr_ctx,
                VIP_hca_hndl_t hca_hndl,
                SRQM_srq_hndl_t srq_hndl,
                u_int32_t *limit_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed to find HCA 0x%X in hobs array"),__func__,hca_hndl);
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = SRQM_query_srq(usr_ctx, HOBKL_get_srqm(hob), srq_hndl, limit_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}


/*************************************************************************
                        
                        Multicast group functions - part of QPM
 
 *************************************************************************/ 

/*******************************************************************
 * FUNCTION: VIPKL_attach_to_multicast <==> MCG_attach_to_multicast
 *******************************************************************/ 
VIP_ret_t VIPKL_attach_to_multicast(
                                            VIP_RSCT_t usr_ctx,
                                /* IN */     VAPI_hca_hndl_t     hca_hndl,
                                /* IN */     IB_gid_t            mcg_dgid,
                                /* IN */     VAPI_qp_hndl_t      qp_hndl)
{
  
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  
  ret = MCG_attach_to_multicast(usr_ctx,HOBKL_get_qpm(hob), mcg_dgid, (QPM_qp_hndl_t)qp_hndl);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}


/*******************************************************************
 * FUNCTION: VIPKL_detach_from_multicast <==> MCG_detach_from_multicast
 *******************************************************************/ 
VIP_ret_t VIPKL_detach_from_multicast(
                                            VIP_RSCT_t usr_ctx,
                                /* IN */     VAPI_hca_hndl_t     hca_hndl,
                                /* IN */     IB_gid_t            mcg_dgid,
                                /* IN */     VAPI_qp_hndl_t      qp_hndl)

{
  
  VIP_ret_t ret;
  HOBKL_hndl_t hob;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = MCG_detach_from_multicast(usr_ctx,HOBKL_get_qpm(hob), mcg_dgid, (QPM_qp_hndl_t)qp_hndl);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}



/*************************************************************************
                        
                        CQM functions
 
 *************************************************************************/ 

/*************************************************************************
 * Function: VIPKL_create_cq <==> CQM_create_cq
 *************************************************************************/ 
VIP_ret_t VIPKL_create_cq(
            VIP_RSCT_t usr_ctx,   
     /*IN*/ VIP_hca_hndl_t hca_hndl,
    /*IN*/ VAPI_cq_hndl_t vapi_cq_hndl,
    /*IN*/ MOSAL_protection_ctx_t  usr_prot_ctx,
    /*IN*/ EM_async_ctx_hndl_t async_hndl_ctx,
    /*IN*/ MT_size_t cq_ul_resources_sz,
    /*IN*/ void  *cq_ul_resources_p,
    /*OUT*/ CQM_cq_hndl_t *cq_p,
    /*OUT*/ HH_cq_hndl_t* cq_id_p
    )
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = CQM_create_cq(usr_ctx, HOBKL_get_cqm(hob), vapi_cq_hndl, usr_prot_ctx, 
                      async_hndl_ctx, cq_ul_resources_p, cq_p, cq_id_p);
  INC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: VIPKL_destroy_cq <==> CQM_destroy_cq
 *************************************************************************/ 
VIP_ret_t VIPKL_destroy_cq(VIP_RSCT_t usr_ctx,/*IN*/ VIP_hca_hndl_t hca_hndl,/*IN*/ CQM_cq_hndl_t cq,
                           /*IN*/MT_bool in_rsct_cleanup)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("VIPKL_destroy_cq failed: cq handle=0x%x"), cq);
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = CQM_destroy_cq(usr_ctx,HOBKL_get_cqm(hob),cq,in_rsct_cleanup);
  DEC_HCA_RSC_CNT
  if ( ret != VIP_OK ) {
    MTL_ERROR1(MT_FLFMT("CQM_destroy_cq failed: cq handle=0x%x"), cq);
  }
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: VIPKL_get_cq_props <==> CQM_get_cq_props
 *************************************************************************/ 
VIP_ret_t VIPKL_get_cq_props(
    VIP_RSCT_t usr_ctx,
    /*IN*/ VIP_hca_hndl_t hca_hndl,
    /*IN*/ CQM_cq_hndl_t cq,
    /*OUT*/ HH_cq_hndl_t* cq_id_p,
    /*OUT*/ VAPI_cqe_num_t *num_o_entries_p
    )
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = CQM_get_cq_props(usr_ctx,HOBKL_get_cqm(hob), cq, cq_id_p,/* NULL, */ num_o_entries_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}


/*************************************************************************
 * Function: VIPKL_modify_cq_props <==> CQM_modify_cq_props
 *************************************************************************/ 
VIP_ret_t VIPKL_resize_cq(
           VIP_RSCT_t usr_ctx,
    /*IN*/ VIP_hca_hndl_t hca_hndl,
    /*IN*/ CQM_cq_hndl_t cq,
    /*IN*/ MT_size_t cq_ul_resources_sz,
    /*IN/OUT*/ void *cq_ul_resources_p
    )
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }

  ret = CQM_resize_cq(usr_ctx,HOBKL_get_cqm(hob), cq, cq_ul_resources_p);

  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}



/*************************************************************************
                        
                        MMU functions
 
 *************************************************************************/ 

/*******************************************************************
 * FUNCTION: VIPKL_create_mr <==> MM_create_mr
 *******************************************************************/ 
VIP_ret_t VIPKL_create_mr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,VAPI_mrw_t* mr_req_p,PDM_pd_hndl_t pd_hndl,
                           MM_mrw_hndl_t *mrw_hndl_p,MM_VAPI_mro_t* mr_prop_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = MM_create_mr(usr_ctx,HOBKL_get_mm(hob),mr_req_p,pd_hndl,mrw_hndl_p,mr_prop_p);
  INC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*******************************************************************
 * FUNCTION: VIPKL_reregister_mr <==> MM_reregister_mr
 *******************************************************************/ 
VIP_ret_t VIPKL_reregister_mr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,VAPI_mr_hndl_t mr_hndl,
                              VAPI_mr_change_t change_type,VAPI_mrw_t  *req_mrw_p, PDM_pd_hndl_t  pd_hndl,
                              MM_mrw_hndl_t  *rep_mr_hndl_p,MM_VAPI_mro_t* mr_prop_p)
{

    VIP_ret_t ret;
    HOBKL_hndl_t hob;
    
    MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
    ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
    if (ret != VIP_OK) {
        return VIP_EINVAL_HCA_HNDL;
    }
    ret = MM_reregister_mr(usr_ctx,HOBKL_get_mm(hob),mr_hndl,change_type,req_mrw_p,pd_hndl,rep_mr_hndl_p,mr_prop_p);
    VIP_array_find_release(hobs, hca_hndl);
    return ret;
}

/*******************************************************************/ 
VIP_ret_t VIPKL_create_mw(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, PDM_pd_hndl_t pd_hndl,
                          IB_rkey_t* r_key_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t  ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }

  ret = MM_create_mw(usr_ctx,HOBKL_get_mm(hob),pd_hndl,r_key_p);
  INC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
} /* VIPKL_create_mw */

/*******************************************************************
 * FUNCTION: VIPKL_create_smr <==> MM_create_smr
 *******************************************************************/ 
VIP_ret_t VIPKL_create_smr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,MM_mrw_hndl_t mrw_orig_hndl,VAPI_mrw_t* mr_req_p,
                          PDM_pd_hndl_t pd_hndl,MM_mrw_hndl_t *mrw_hndl_p,MM_VAPI_mro_t* mr_prop_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret=VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  if (ret != VIP_OK) return VIP_EINVAL_HCA_HNDL;
  ret= MM_create_smr(usr_ctx,HOBKL_get_mm(hob),mrw_orig_hndl,mr_req_p,pd_hndl,mrw_hndl_p,mr_prop_p);
  INC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}
/*******************************************************************
 * FUNCTION: VIPKL_alloc_fmr <==> MM_alloc_fmr
 *******************************************************************/ 
VIP_ret_t VIPKL_alloc_fmr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,EVAPI_fmr_t* fmr_props_p,
                          PDM_pd_hndl_t pd_hndl,MM_mrw_hndl_t *mrw_hndl_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret; 

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret=VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) return VIP_EINVAL_HCA_HNDL;
  ret= MM_alloc_fmr(usr_ctx,HOBKL_get_mm(hob),fmr_props_p,pd_hndl,mrw_hndl_p);
  INC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}
/*******************************************************************
 * FUNCTION: VIPKL_map_fmr <==> MM_map_fmr
 *******************************************************************/ 
VIP_ret_t VIPKL_map_fmr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,EVAPI_fmr_hndl_t fmr_hndl,EVAPI_fmr_map_t* map_p,
                        VAPI_lkey_t *lkey_p,VAPI_rkey_t *rkey_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret=VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) return VIP_EINVAL_HCA_HNDL;
  ret= MM_map_fmr(usr_ctx,HOBKL_get_mm(hob),fmr_hndl,map_p,lkey_p,rkey_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}
/*******************************************************************
 * FUNCTION: VIPKL_unmap_fmr <==> MM_unmap_fmr
 *******************************************************************/ 
VIP_ret_t VIPKL_unmap_fmr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,MT_size_t size,VAPI_mr_hndl_t* mr_hndl_arr)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret=VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) return VIP_EINVAL_HCA_HNDL;
  ret= MM_unmap_fmr(usr_ctx,HOBKL_get_mm(hob),size,mr_hndl_arr);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}
/*******************************************************************
 * FUNCTION: VIPKL_free_fmr <==> MM_free_fmr
 *******************************************************************/ 
VIP_ret_t VIPKL_free_fmr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,VAPI_mr_hndl_t mr_hndl)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret=VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) return VIP_EINVAL_HCA_HNDL;
  ret= MM_free_fmr(usr_ctx,HOBKL_get_mm(hob),mr_hndl);
  DEC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*******************************************************************
 * FUNCTION:  VIPKL_destroy_mr <==>  MM_destroy_mr
 *******************************************************************/ 
VIP_ret_t VIPKL_destroy_mr(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, MM_mrw_hndl_t mrw_hndl)
{
  HOBKL_hndl_t hob;
  VIP_ret_t  ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  
  ret = MM_destroy_mr(usr_ctx,HOBKL_get_mm(hob), mrw_hndl);
  DEC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*******************************************************************
 * FUNCTION:  VIPKL_destroy_mw <==>        MM_destroy_mw
 *******************************************************************/ 
VIP_ret_t VIPKL_destroy_mw(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, IB_rkey_t initial_rkey)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  
  ret = MM_destroy_mw(usr_ctx,HOBKL_get_mm(hob),initial_rkey);
  DEC_HCA_RSC_CNT
  MTL_DEBUG1("%s MM_destroy_mw() returned 0x%x", __func__,ret);
  
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
} /* VIPKL_destroy_mw */


/*******************************************************************
 * FUNCTION:  VIPKL_query_mr <==>        MM_query_mr
 *******************************************************************/ 
VIP_ret_t VIPKL_query_mr (VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, MM_mrw_hndl_t mrw_hndl,
    MM_VAPI_mro_t *mr_prop_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = MM_query_mr(usr_ctx,HOBKL_get_mm(hob), mrw_hndl,mr_prop_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*******************************************************************
 * FUNCTION:  VIPKL_query_mw <==>        MM_destroy_mw
 *******************************************************************/ 
VIP_ret_t VIPKL_query_mw(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, IB_rkey_t initial_rkey,IB_rkey_t *current_key)
{
  HOBKL_hndl_t hob;
  HH_ret_t hhrc;
  VIP_ret_t ret;
  HH_pd_hndl_t pd;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  
  hhrc = HH_query_mw(HOBKL_get_hh_hndl(hob), initial_rkey, current_key, &pd);
  MTL_DEBUG4(MT_FLFMT("hhrc=%d=%s"), hhrc, HH_strerror_sym(hhrc));

  ret = hhrc;
  if (ret != HH_OK) {
    ret = (ret < VAPI_ERROR_MAX ? ret : VAPI_EGEN);
  }
  
  if (ret != VIP_OK) 
  {
    MTL_ERROR1(MT_FLFMT("VIPKL_query_mw: rc=%d=%s"), ret, VAPI_strerror_sym(ret));
  }
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
} /* VIPKL_destroy_mw */

/*******************************************************************
 * FUNCTION:   VIPKL_bind_async_error_handler <==> EM_bind_async_error_handler
 *******************************************************************/ 
VIP_ret_t VIPKL_bind_async_error_handler(/*IN*/ VIP_hca_hndl_t hca_hndl,
    /*IN*/ VAPI_async_event_handler_t handler, /*IN*/ void* private_data)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = EM_bind_async_error_handler(HOBKL_get_em(hob), handler, private_data);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * FUNCTION: VIPKL_bind_completion_event_handler <==> EM_bind_completion_event_handler
 *************************************************************************/ 
VIP_ret_t VIPKL_bind_completion_event_handler(/*IN*/ VIP_hca_hndl_t hca_hndl,
    /*IN*/ VAPI_completion_event_handler_t handler, /*IN*/ void* private_data)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = EM_bind_completion_event_handler(HOBKL_get_em(hob), handler, private_data);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * FUNCTION: VIPKL_bind_evapi_completion_event_handler <==> EM_bind_evapi_completion_event_handler
 *************************************************************************/ 
VIP_ret_t VIPKL_bind_evapi_completion_event_handler(/*IN*/ VIP_hca_hndl_t hca_hndl,
     /*IN*/ CQM_cq_hndl_t cq_hndl, /*IN*/ VAPI_completion_event_handler_t handler, 
     /*IN*/ void* private_data)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = EM_bind_evapi_completion_event_handler(HOBKL_get_em(hob), cq_hndl, handler, private_data);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}


/*************************************************************************
 * FUNCTION: VIPKL_set_async_event_handler <==> EM_set_async_event_handler
 *************************************************************************/ 
VIP_ret_t VIPKL_set_async_event_handler(
                                  /*IN*/  VIP_hca_hndl_t                  hca_hndl,
                                  /*IN*/  EM_async_ctx_hndl_t             hndl_ctx,
                                  /*IN*/  VAPI_async_event_handler_t      handler,
                                  /*IN*/  void*                           private_data,
                                  /*OUT*/ EVAPI_async_handler_hndl_t     *async_handler_hndl
                                        )
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = EM_set_async_event_handler(HOBKL_get_em(hob), hndl_ctx, handler, private_data, async_handler_hndl);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * FUNCTION: VIPKL_clear_async_event_handler <==> EM_clear_async_event_handler
 *************************************************************************/ 
VIP_ret_t VIPKL_clear_async_event_handler(
                               /*IN*/ VIP_hca_hndl_t                  hca_hndl, 
                               /*IN*/ EM_async_ctx_hndl_t             hndl_ctx,
                               /*IN*/ EVAPI_async_handler_hndl_t      async_handler_hndl)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = EM_clear_async_event_handler(HOBKL_get_em(hob), hndl_ctx, async_handler_hndl);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}



VAPI_ret_t VIPKL_set_destroy_cq_cbk(
  /*IN*/ VIP_hca_hndl_t           hca_hndl,
  /*IN*/ CQM_cq_hndl_t            cq_hndl,
  /*IN*/ EVAPI_destroy_cq_cbk_t   cbk_func,
  /*IN*/ void*                    private_data
)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = CQM_set_destroy_cq_cbk(HOBKL_get_cqm(hob),cq_hndl,cbk_func,private_data);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

 
VAPI_ret_t VIPKL_clear_destroy_cq_cbk(
  /*IN*/ VIP_hca_hndl_t                   hca_hndl,
  /*IN*/ CQM_cq_hndl_t                    cq_hndl
)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = CQM_clear_destroy_cq_cbk(HOBKL_get_cqm(hob),cq_hndl);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}


VAPI_ret_t VIPKL_set_destroy_qp_cbk(
  /*IN*/ VIP_hca_hndl_t           hca_hndl,
  /*IN*/ QPM_qp_hndl_t            qp_hndl,
  /*IN*/ EVAPI_destroy_qp_cbk_t   cbk_func,
  /*IN*/ void*                    private_data
)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = QPM_set_destroy_qp_cbk(HOBKL_get_qpm(hob),qp_hndl,cbk_func,private_data);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

 
VAPI_ret_t VIPKL_clear_destroy_qp_cbk(
  /*IN*/ VIP_hca_hndl_t           hca_hndl,
  /*IN*/ QPM_qp_hndl_t            qp_hndl
)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;
  
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = QPM_clear_destroy_qp_cbk(HOBKL_get_qpm(hob),qp_hndl);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}



/*************************************************************************
 * Function: VIPKL_process_local_mad  <==> HOBKL_process_local_mad
 *************************************************************************/ 
VIP_ret_t VIPKL_process_local_mad(VIP_hca_hndl_t hca_hndl, IB_port_t port_num, IB_lid_t slid,
  EVAPI_proc_mad_opt_t proc_mad_opts, void * mad_in_p, void * mad_out_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret = VIP_OK;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  MTL_TRACE2("%s: hca_hndl=%d, port=%d, proc_mad_opts=0x%x, mad_in_p=0x%p\nmad_out_p=0x%p\n", __func__,
                hca_hndl, port_num, (u_int32_t) proc_mad_opts, (void *) mad_in_p, (void *) mad_out_p);
  ret = HOBKL_process_local_mad(hob, port_num, slid, proc_mad_opts, mad_in_p, mad_out_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}


/*************************************************************************
 * Function: VIPKL_alloc_devmem  <==> DEVMM_alloc_devmem
 *************************************************************************/ 
VIP_ret_t VIPKL_alloc_map_devmem(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, EVAPI_devmem_type_t mem_type,
                             VAPI_size_t  bsize,u_int8_t align_shift,
                             VAPI_phy_addr_t* buf_p,void** virt_addr_p, DEVMM_dm_hndl_t* dm_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret = VIP_OK;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = DEVMM_alloc_map_devmem(usr_ctx,HOBKL_get_devmm(hob),mem_type,bsize,align_shift,
                           buf_p,virt_addr_p, dm_p);
  INC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: VIPKL_query_devmem  <==> DEVMM_query_devmem
 *************************************************************************/ 
VIP_ret_t VIPKL_query_devmem(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl, EVAPI_devmem_type_t mem_type,
                             u_int8_t align_shift, EVAPI_devmem_info_t*  devmem_info_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret = VIP_OK;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = DEVMM_query_devmem(usr_ctx,HOBKL_get_devmm(hob),mem_type,align_shift,devmem_info_p);
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: VIPKL_free_unmap_devmem  <==> DEVMM_free_unmap_devmem
 *************************************************************************/ 
VIP_ret_t VIPKL_free_unmap_devmem(VIP_RSCT_t usr_ctx,VIP_hca_hndl_t hca_hndl,DEVMM_dm_hndl_t dm)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret = VIP_OK;
  
  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }
  ret = DEVMM_free_unmap_devmem(usr_ctx,HOBKL_get_devmm(hob),dm);
  DEC_HCA_RSC_CNT
  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/*************************************************************************
 * Function: VIPKL_cleanup
 *
 * Arguments:
 *  
 *  
 *
 * Returns:
 *  VIP_OK
 *  VIP_EBUSY: HCAs still used by processes (?)
 *  
 *  
 *
 * Description:
 *  
 *  This function performs module cleanup
 *  By destroying all HOBKL structures
 *  
 *
 *************************************************************************/ 
VIP_ret_t VIPKL_cleanup()
{
  HOBKL_hndl_t hob_hndl;
  VIP_hca_hndl_t hca_hndl;
  VIP_ret_t ret = VIP_OK;
  VIP_array_p_t save_hobs;
  call_result_t mt_rc;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  
  MTL_DEBUG1("* * inside VIPKL_cleanup \n");
  
  MOSAL_mutex_acq_ui(&open_hca_mtx); 
  if (hobs == NULL) { /* Assure only one performs cleanup */
    MOSAL_mutex_rel(&open_hca_mtx);
    return VIP_OK;
  }
  
 ret = VIP_array_get_first_handle(hobs,(VIP_array_handle_t *) &hca_hndl,(VIP_array_obj_t *)&hob_hndl);
 while (ret == VIP_OK) {
     if ( (ret=HOBKL_destroy(hob_hndl)) != VIP_OK ) {
       MTL_ERROR1(MT_FLFMT("HOBKL_destroy returned %s (possible cleanup problems)"), VAPI_strerror_sym(ret));
     }
     ret = VIP_array_get_next_handle(hobs, (VIP_array_handle_t *)&hca_hndl,(VIP_array_obj_t *)&hob_hndl);
 }

 save_hobs= hobs;
 hobs= NULL; /* Flag to denote cleanup was done */
 VIP_array_destroy(save_hobs,NULL);
  
  MOSAL_mutex_rel(&open_hca_mtx);
  mt_rc = MOSAL_mutex_free(&open_hca_mtx);
  if (mt_rc != MT_OK) {
    MTL_ERROR2(MT_FLFMT("Failed MOSAL_syncobj_free (%s)"),mtl_strerror_sym(mt_rc));
  }
  VIPKL_EQ_cleanup();
  VIPKL_cqblk_cleanup();

  return VIP_OK;
}


/*
 *  VIPKL_init_layer
 */
VIP_ret_t VIPKL_init_layer(void)
{
  VIP_common_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_create_maxsize(VIP_MAX_HCA,VIP_MAX_HCA,&hobs);
  if (ret != VIP_OK) {
      return VIP_EAGAIN;
  }
  MTL_DEBUG3("Inside " "%s: initialized successfully;\n", __func__);
  
  if (! VIPKL_cqblk_init()) {
    MTL_ERROR1(MT_FLFMT("Failed VIPKL_cq_blk_init()"));
    return VIP_EAGAIN;
  }
  VIPKL_EQ_init();

  MOSAL_mutex_init(&open_hca_mtx);
  return VIP_OK;
}

#if defined(MT_KERNEL)
VIP_ret_t VIPKL_get_debug_info(VIP_hca_hndl_t hca_hndl,VIPKL_debug_info_t *debug_info_p)
{
    HOBKL_hndl_t hob;
    VIP_ret_t ret;
    MT_bool have_error = FALSE;

    if (debug_info_p == NULL) {
        return VIP_EINVAL_PARAM;
    }
    ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
    if (ret != VIP_OK) {
      return VIP_EINVAL_HCA_HNDL;
    }

    memset(debug_info_p, 0, sizeof(VIPKL_debug_info_t));
    if(PDM_get_num_pds(HOBKL_get_pdm(hob), &(debug_info_p->allocated_pd))) have_error = TRUE;
    if(QPM_get_num_qps(HOBKL_get_qpm(hob), &(debug_info_p->allocated_qp))) have_error = TRUE;
    if(CQM_get_num_cqs(HOBKL_get_cqm(hob), &(debug_info_p->allocated_cq))) have_error = TRUE;
    if(DEVMM_get_num_alloc_chunks(HOBKL_get_devmm(hob),&(debug_info_p->allocated_devmm_chunks))) have_error = TRUE;
    if(MMU_get_num_objs(HOBKL_get_mm(hob),&(debug_info_p->allocated_mr),
                     &(debug_info_p->allocated_fmr),&(debug_info_p->allocated_mw))) have_error = TRUE;
    if(EM_get_num_hca_handles(HOBKL_get_em(hob),&(debug_info_p->allocated_hca_handles))) have_error = TRUE;
    VIP_array_find_release(hobs, hca_hndl);
    return (have_error ? VIP_EGEN : VIP_OK);
}


VIP_ret_t VIPKL_get_rsrc_info(VIP_hca_hndl_t hca_hndl, VIPKL_rsrc_info_t *rsrc_info_p)
{
  HOBKL_hndl_t hob;
  VIP_ret_t rc;
  QPM_hndl_t qpm;

  if ( !rsrc_info_p ) {
    return VIP_EINVAL_PARAM;
  }

  rc = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (rc != VIP_OK) {
    return VIP_EINVAL_HCA_HNDL;
  }

  /* QP info */
  qpm = HOBKL_get_qpm(hob);
  rc = QPM_get_qp_list(qpm, &rsrc_info_p->qp);

  VIP_array_find_release(hobs, hca_hndl);
  return rc;
}


int VIPKL_get_qp_rsrc_str(struct qp_data_st *qpd, char *str)
{
  int count;

  if ( qpd ) {
    count = sprintf(str, "0x%06x, %s, 0x%06x, 0x%06x, %d, %s", 
                    qpd->qpn, VAPI_qp_state_sym(qpd->qp_state), qpd->rq_cq_id, qpd->sq_cq_id, qpd->port, VAPI_ts_type_sym(qpd->ts_type));
    return count;
  }
  return 0;
}


void VIPKL_rel_qp_rsrc_info(struct qp_data_st *qpd)
{
  struct qp_data_st *tmp;

  while ( qpd ) {
    tmp = qpd->next;
    FREE(qpd);
    qpd = tmp;
  }
}

#if defined(MT_SUSPEND_QP)
/******************************************************************************
 *  Function: VIPKL_suspend_qp <==> QPM_suspend_qp
 *****************************************************************************/
VIP_ret_t
VIPKL_suspend_qp (VIP_RSCT_t      usr_ctx,
                  VIP_hca_hndl_t  hca_hndl, 
                  QPM_qp_hndl_t   qp_hndl,
                  MT_bool         suspend_flag)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Could not find HCA from handle. ret=%d, hca_hndl=0x%x, flag=%s"),
                __func__, ret, hca_hndl, ((suspend_flag==TRUE)? "TRUE" : "FALSE"));
    return VIP_EINVAL_HCA_HNDL;
  }
  
  ret = QPM_suspend_qp(usr_ctx,HOBKL_get_qpm(hob), qp_hndl, suspend_flag);
  if (ret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: QPM_suspend_qp failed. ret=%d, hca_hndl=0x%x, qp_hndl=0x%x, flag=%s"),
                __func__, ret, hca_hndl, qp_hndl, ((suspend_flag==TRUE)? "TRUE" : "FALSE"));
  }

  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}

/******************************************************************************
 *  Function: VIPKL_suspend_cq <==> QPM_suspend_cq
 *****************************************************************************/
VIP_ret_t
VIPKL_suspend_cq (VIP_RSCT_t      usr_ctx,
                  VIP_hca_hndl_t  hca_hndl, 
                  CQM_cq_hndl_t   cq_hndl,
                  MT_bool         do_suspend)
{
  HOBKL_hndl_t hob;
  VIP_ret_t ret;

  MT_RETURN_IF_LOW_STACK(VIP_WATERMARK);
  ret = VIP_array_find_hold(hobs, hca_hndl, (VIP_array_obj_t *)&hob);
  if (ret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Could not find HCA from handle. ret=%d, hca_hndl=0x%x, do_suspend=%s"),
                __func__, ret, hca_hndl, ((do_suspend==TRUE)? "TRUE" : "FALSE"));
    return VIP_EINVAL_HCA_HNDL;
  }
  
  ret = CQM_suspend_cq(usr_ctx,HOBKL_get_cqm(hob), cq_hndl, do_suspend);
  if (ret != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: QPM_suspend_cq failed. ret=%d, hca_hndl=0x%x, cq_hndl=0x%x, do_suspend=%s"),
                __func__, ret, hca_hndl, cq_hndl, ((do_suspend==TRUE)? "TRUE" : "FALSE"));
  }

  VIP_array_find_release(hobs, hca_hndl);
  return ret;
}
#endif

#endif


