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

#include <mosal.h>
#include <MT23108.h>
#include <udavm.h>
#include <epool.h>
#include <tlog2.h>
#include <vip_array.h>
#include <vip_common.h>

#define UD_AV_ENTRY_SIZE (sizeof(struct tavorprm_ud_address_vector_st) / 8)
#define UD_AV_ENTRY_DWORD_SIZE (sizeof(struct tavorprm_ud_address_vector_st) / 32)
#define UD_AV_ENTRY_SIZE_LOG2 5
#define UD_AV_ALIGNMENT_MASK (UD_AV_ENTRY_SIZE - 1)
/*================ macro definitions ===============================================*/
/* check that ud_av_p is in the av table range and that it is aligned to entry size */
#define IS_INVALID_AV_MEMBER(udavm, ud_av_p) \
  (((ud_av_p) < udavm->ud_av_table) || \
   (((MT_virt_addr_t)ud_av_p) >= ((MT_virt_addr_t)(udavm->ud_av_table))+(udavm->ud_av_table_sz))  )
#define IS_INVALID_AV_ALIGN(ud_av_p) \
   (((MT_virt_addr_t)(ud_av_p)) & UD_AV_ALIGNMENT_MASK)

#define RGID_OFFSET (MT_BIT_OFFSET(tavorprm_ud_address_vector_st, rgid_127_96) >> 5) /* in DWORDS */
#define LOG2_PORT_GID_TABLE_SIZE 5 /* 32 GID entries per port */
#define HANDLE_2_INDEX(udavm, ah) (((MT_ulong_ptr_t)ah - ((MT_ulong_ptr_t)(udavm)->ud_av_table)) >> UD_AV_ENTRY_SIZE_LOG2)

/*================ type definitions ================================================*/


/* The main UDAV-manager structure */
struct THH_udavm_st {
  u_int32_t*       ud_av_table; /* the actual table of UDAV*/
  u_int32_t        max_av;  /* number of entries in table */
  MT_size_t        ud_av_table_sz; /* total table size in bytes */
  VAPI_lkey_t      table_memkey; 
  VIP_array_p_t    udavs_array;       /* the array which holds the free list of AV entries */
  MOSAL_spinlock_t table_spinlock;     /* protect on the table from destroying UDAV and modifying at the same time */
}THH_udavm_int_t;


/*================ global variables definitions ====================================*/



/*================ static functions prototypes =====================================*/

static void fill_udav_entry(/*IN */ HH_pd_hndl_t pd, 
                /*IN */ MT_bool      is_new_pd,
                /*IN */ VAPI_ud_av_t *av_p, 
                /*IN */ u_int32_t    *av_entry_p);



#ifdef MAX_DEBUG
static void print_udav(VAPI_ud_av_t *av_p);
#endif

/*================ global functions definitions ====================================*/


HH_ret_t THH_udavm_create( /*IN */ THH_ver_info_t *version_p, 
                           /*IN */ VAPI_lkey_t ud_av_table_memkey, 
                           /*IN */ MT_virt_addr_t ud_av_table, 
                           /*IN */ MT_size_t ud_av_table_sz,
                           /*OUT*/ THH_udavm_t *udavm_p)

{
  THH_udavm_t new_udavm_p = NULL;
  HH_ret_t ret;
  
  FUNC_IN;

  /* allocation of object structure */
  new_udavm_p = (THH_udavm_t)MALLOC(sizeof(THH_udavm_int_t));
  if (!new_udavm_p) {
    MTL_ERROR4("%s: Cannot allocate UDAVM object.\n", __func__);
    MT_RETURN( HH_EAGAIN);
  }

  memset(new_udavm_p,0,sizeof(THH_udavm_int_t));
  
  /* filling the of UDAV struct */
  if (MOSAL_spinlock_init(&(new_udavm_p->table_spinlock)) != MT_OK){
    MTL_ERROR4("%s: Failed to initializing spinlocks.\n", __func__);
    ret= HH_ERR;
    goto err_free_mem;
  } 
  new_udavm_p->ud_av_table = (u_int32_t*)ud_av_table;
  memset(new_udavm_p->ud_av_table,0,ud_av_table_sz);
  new_udavm_p->max_av = (u_int32_t)(ud_av_table_sz/UD_AV_ENTRY_SIZE);
  new_udavm_p->ud_av_table_sz = ud_av_table_sz;
  new_udavm_p->table_memkey = ud_av_table_memkey;
  /* init the free list */
  ret = VIP_array_create_maxsize(new_udavm_p->max_av,new_udavm_p->max_av,&(new_udavm_p->udavs_array));
  if ( ret != VIP_OK ) {
    MTL_ERROR1("%s: VIP_array_create_maxsize failed, ret=%d \n", __func__, ret);
    goto err_free_mem;
  }

  /* succeeded to create object - return params: */

  *udavm_p = new_udavm_p;

  MT_RETURN(HH_OK);

  /* error handling cleanup */
err_free_mem:
  VFREE(new_udavm_p);
  MT_RETURN(ret);

}


/************************************************************************************/

HH_ret_t THH_udavm_destroy( /*IN */ THH_udavm_t udavm )
{
  
  FUNC_IN;
  if (udavm == NULL) {
    MTL_ERROR4("%s: udavm is NULL.\n", __func__);
    MT_RETURN(HH_EINVAL);
  }

  /* destroy the handlers array */
  if (VIP_array_destroy(udavm->udavs_array, NULL) != VIP_OK) {
    MTL_ERROR1("%s: VIP_array_destroy failed \n", __func__);
  }
  FREE(udavm);
  MT_RETURN(HH_OK);
}



/************************************************************************************/

HH_ret_t THH_udavm_get_memkey( /*IN */ THH_udavm_t udavm, 
                               /*IN */ VAPI_lkey_t *table_memkey_p )
{
  FUNC_IN;
  if ((udavm != NULL) && (table_memkey_p !=NULL)) {
    *table_memkey_p = udavm->table_memkey;
    MT_RETURN(HH_OK);
  }

  MT_RETURN(HH_EINVAL);

}

/************************************************************************************/
HH_ret_t THH_udavm_create_av( /*IN */ THH_udavm_t udavm, 
                              /*IN */ HH_pd_hndl_t pd, 
                              /*IN */ VAPI_ud_av_t *av_p, 
                              /*OUT*/ HH_ud_av_hndl_t *ah_p)
{
  u_int32_t* av_entry_p;
  u_int32_t ah_index=0;
  VIP_common_ret_t ret;
  
  FUNC_IN;
  
  if (udavm == NULL) {
    MTL_ERROR4("THH_udavm_create_av: udavm is NULL.\n");
    MT_RETURN(HH_EINVAL);
  }

  if (av_p == NULL) {
    MTL_ERROR4("THH_udavm_create_av: av_p is NULL.\n");
    MT_RETURN(HH_EINVAL);
  }

  if (ah_p == NULL) {
    MTL_ERROR4("THH_udavm_create_av: ah_p is NULL.\n");
    MT_RETURN(HH_EINVAL);
  }

  if (av_p->dlid == 0) {
    MTL_ERROR4("THH_udavm_create_av: invalid dlid (ZERO).\n");
    MT_RETURN(HH_EINVAL);
  }
   
  /* get a free index for the udav */
  ret = VIP_array_insert(udavm->udavs_array, NULL, &ah_index);
  if (ret != VIP_OK) {
    MTL_ERROR4("THH_udavm_create_av: Not enough resources.\n");
    MT_RETURN(HH_EAGAIN);
  }

  av_entry_p = (u_int32_t*) (((MT_virt_addr_t)((udavm)->ud_av_table)) + 
                             ((ah_index)*UD_AV_ENTRY_SIZE)); 

  /* filling the entry */

  fill_udav_entry(pd, TRUE, av_p, av_entry_p);
  
  *ah_p = (HH_ud_av_hndl_t)av_entry_p; 
  MTL_DEBUG4(MT_FLFMT("Allocated address handle = " MT_ULONG_PTR_FMT ", entry index=%d"),*ah_p, ah_index);
  MT_RETURN(HH_OK);
}

/************************************************************************************/
HH_ret_t THH_udavm_modify_av( /*IN */ THH_udavm_t udavm, 
                              /*IN */ HH_ud_av_hndl_t ah, 
                              /*IN */ VAPI_ud_av_t *av_p )
{
  u_int32_t *ud_av_p= (u_int32_t*)(MT_virt_addr_t)ah;
  HH_ret_t hh_ret;
  u_int32_t ah_index;
  VIP_common_ret_t ret=VIP_OK;

  FUNC_IN;
  
  if (udavm == NULL) {
    MTL_ERROR4("THH_udavm_modify_av: udavm is NULL.\n");
    MT_RETURN(HH_EINVAL);
  }

  if (av_p == NULL) {
    MTL_ERROR4("THH_udavm_modify_av: av_p is NULL.\n");
    MT_RETURN(HH_EINVAL);
  }

  if (av_p->dlid == 0) {
    MTL_ERROR4("THH_udavm_modify_av: invalid dlid (ZERO).\n");
    MT_RETURN(HH_EINVAL);
  }

  /* Check that ah within the table and aligned to entry beginning */
  if (IS_INVALID_AV_ALIGN(ud_av_p)) {
      MTL_ERROR4("THH_udavm_modify_av: invalid av alignment.\n");
      MT_RETURN(HH_EINVAL);
  }
  if (IS_INVALID_AV_MEMBER(udavm, ud_av_p)) {
    MTL_DEBUG4("THH_udavm_modify_av: invalid ah (" MT_ULONG_PTR_FMT ").\n",ah);
    MT_RETURN(HH_EINVAL_AV_HNDL);
  }
  
  /* check that ah is a valid (allocated) handle */
  ah_index = (u_int32_t)HANDLE_2_INDEX(udavm, ud_av_p); 
  ret = VIP_array_find_hold(udavm->udavs_array, ah_index, NULL);
  MTL_DEBUG3(MT_FLFMT("Calling VIP_array_find_hold, ah_index=%d, ret=%d\n"), ah_index, ret);
  if ( ret!=VIP_OK ) {
    MTL_ERROR4("THH_udavm_modify_av: handle is not valid.\n");
    hh_ret = HH_EINVAL_AV_HNDL;
  } else {
      MOSAL_spinlock_irq_lock(&(udavm->table_spinlock));
      fill_udav_entry(0, FALSE, av_p, ud_av_p);
      hh_ret = HH_OK;
      MOSAL_spinlock_unlock(&(udavm->table_spinlock));
  }
  ret = VIP_array_find_release(udavm->udavs_array, ah_index);
  MTL_DEBUG3(MT_FLFMT("Calling VIP_array_find_release ret=%d"), ret);
  if ( ret!=VIP_OK ) {
    MTL_ERROR1("%s: Internal mismatch - hv_index (%d) is not in array\n", __func__,ah_index);
    hh_ret = ret;
  }
  MTL_DEBUG4(MT_FLFMT("THH_udavm_modify_av: address handle = " MT_ULONG_PTR_FMT ", entry index=%d"),ah, ah_index);
  MT_RETURN(hh_ret);
}

/************************************************************************************/
HH_ret_t THH_udavm_query_av( /*IN */ THH_udavm_t udavm, 
                             /*IN */ HH_ud_av_hndl_t ah, 
                             /*OUT*/ VAPI_ud_av_t *av_p )
{
  HH_ret_t hh_ret;
  u_int32_t ah_index;
  VIP_common_ret_t ret=VIP_OK;
  u_int32_t *ud_av_p= (u_int32_t*)(MT_virt_addr_t)ah;
 
  FUNC_IN;
  
  if (udavm == NULL) {
    MTL_ERROR4("THH_udavm_query_av: udavm is NULL.\n");
    MT_RETURN(HH_EINVAL);
  }

  if (av_p == NULL) {
    MTL_ERROR4("THH_udavm_query_av: av_p is NULL.\n");
    MT_RETURN(HH_EINVAL);
  }

  /* Check that ah within the table and aligned to entry beginning */
  if (IS_INVALID_AV_ALIGN(ud_av_p)) {
      MTL_ERROR4("THH_udavm_query_av: invalid av alignment.\n");
      MT_RETURN(HH_EINVAL);
  }
  if (IS_INVALID_AV_MEMBER(udavm, ud_av_p)) {
    MTL_DEBUG4("THH_udavm_query_av: invalid ah (" MT_ULONG_PTR_FMT ").\n",ah);
    MT_RETURN(HH_EINVAL_AV_HNDL);
  }

  /* check that ah is a valid (allocated) handle */
  ah_index = (u_int32_t)HANDLE_2_INDEX(udavm, ud_av_p); 
  ret = VIP_array_find_hold(udavm->udavs_array, ah_index, NULL);
  MTL_DEBUG3(MT_FLFMT("Calling VIP_array_find_hold, hv_index=%d, ret=%d\n"), ah_index, ret);
  if ( ret!=VIP_OK ) {
    MTL_ERROR4("THH_udavm_query_av: handle is not valid.\n");
    hh_ret = HH_EINVAL_AV_HNDL;
  } else { /* DO IT */
    MOSAL_spinlock_irq_lock(&(udavm->table_spinlock));
    hh_ret = THH_udavm_parse_udav_entry(ud_av_p, av_p);
    MOSAL_spinlock_unlock(&(udavm->table_spinlock));
  }
  ret = VIP_array_find_release(udavm->udavs_array, ah_index);
  MTL_DEBUG3(MT_FLFMT("Calling VIP_array_find_release ret=%d"), ret);
  if ( ret!=VIP_OK ) {
    MTL_ERROR1("%s: Internal mismatch - hv_index (%d) is not in array\n", __func__, ah_index);
    hh_ret = ret;
  }

  MTL_DEBUG4(MT_FLFMT("THH_udavm_query_av: address handle = " MT_ULONG_PTR_FMT ", entry index=%d"),ah, ah_index);
#if 1 <= MAX_DEBUG
  print_udav(av_p);
#endif
  MT_RETURN(hh_ret);
  
}

/************************************************************************************/
HH_ret_t THH_udavm_destroy_av( /*IN */ THH_udavm_t udavm, 
                               /*IN */ HH_ud_av_hndl_t ah )
{
  u_int32_t ah_index;
  VIP_common_ret_t ret=VIP_OK;
  u_int32_t *ud_av_p= (u_int32_t*)(MT_virt_addr_t)ah;
  HH_ret_t hh_ret = HH_OK;
  
  FUNC_IN;
  
  if (udavm == NULL) {
    MTL_ERROR4("THH_udavm_destroy_av: udavm is NULL.\n");
    MT_RETURN(HH_EINVAL);
  }

  /* Check that given ah is within the table and aligned to entry size */
  if (IS_INVALID_AV_ALIGN(ud_av_p)) {
      MTL_ERROR4("THH_udavm_destroy_av: invalid av alignment.\n");
      MT_RETURN(HH_EINVAL);
  }
  if (IS_INVALID_AV_MEMBER(udavm, ud_av_p)) {
    MTL_DEBUG4("THH_udavm_destroy_av: invalid ah (" MT_ULONG_PTR_FMT ").\n",ah);
    MT_RETURN(HH_EINVAL_AV_HNDL);
  }
  
  /* check that ah is a valid (allocated) handle and release it */
  ah_index = (u_int32_t)HANDLE_2_INDEX(udavm, ud_av_p); 
  ret = VIP_array_erase_prepare(udavm->udavs_array, ah_index, NULL);
  MTL_DEBUG3(MT_FLFMT("Calling VIP_array_erase_prepare, hv_index=%d, ret=%d\n"), ah_index, ret);
  if ( ret==VIP_OK ) { /* DO IT */
    /* Use the knowledge that PD is in the first Dword of UDAV entry - so only put zeros there */
    ud_av_p[0] = 0;
    ret = VIP_array_erase_done(udavm->udavs_array, ah_index, NULL);
    if ( ret!=VIP_OK ) { 
      MTL_ERROR4("THH_udavm_destroy_av: internal error VIP_array_erase_done failed.\n");
    }
  } else if ( ret==VIP_EBUSY ) {
    MTL_ERROR4("THH_udavm_destroy_av: handle is busy (in modify or query).\n");
    hh_ret = HH_EBUSY;
  } else if (ret == VIP_EINVAL_HNDL) {
    MTL_ERROR4("THH_udavm_destroy_av: Invalid handle.\n");
    hh_ret = HH_EINVAL_AV_HNDL;
  }

  MTL_DEBUG4(MT_FLFMT("THH_udavm_destroy_av: address handle = " MT_ULONG_PTR_FMT ", entry index=%d"),ah, ah_index);
  MT_RETURN(ret);
}




/************ LOCAL FUNCTIONS ************************************************************************/

static void fill_udav_entry(/*IN */ HH_pd_hndl_t pd, 
                     /*IN */ MT_bool      is_new_pd,
                     /*IN */ VAPI_ud_av_t *av_p, 
                     /*IN */ u_int32_t    *av_entry_p)
{

  int i;
  u_int32_t new_av_arr[8] = {0,0,0,0,0,0,0,0}; /* entry size is 32 bytes */
  HH_pd_hndl_t pd_tmp;
  u_int32_t pd_word = 0;
  
  FUNC_IN;
  /*print_udav(av_p);*/
  
  /* if not new PD the we have to clear PD so entry will not considered valid by HW while we modify it*/
  /* need to save the current PD and write it back at the end */
  if (!is_new_pd) {
    pd_word = MOSAL_be32_to_cpu(*av_entry_p);
    pd_tmp = MT_EXTRACT32(pd_word, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, pd),
                               MT_BIT_SIZE(tavorprm_ud_address_vector_st, pd));
    av_entry_p[0] = 0;
  }
  else {
    pd_tmp = pd;
  }
  
  /* PD */
  MT_INSERT_ARRAY32(new_av_arr, pd_tmp, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, pd),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, pd));

  /* port */
  MT_INSERT_ARRAY32(new_av_arr, av_p->port, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, port_number),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, port_number));
  
  /* rlid */
  MT_INSERT_ARRAY32(new_av_arr, av_p->dlid, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, rlid),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, rlid));

  /* mylid_path_bits */
  MT_INSERT_ARRAY32(new_av_arr, av_p->src_path_bits, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, my_lid_path_bits),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, my_lid_path_bits));
  /* grh enable */
  MT_INSERT_ARRAY32(new_av_arr, av_p->grh_flag, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, g),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, g));
  /* hop_limit */
  MT_INSERT_ARRAY32(new_av_arr, av_p->hop_limit, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, hop_limit),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, hop_limit));

  /* max_stat_rate */
  /* 0 - Suited for matched links no flow control
     All other values are transmitted to 1 since this is the only flow control of Tavor */
  MT_INSERT_ARRAY32(new_av_arr, ((av_p->static_rate ==0) ? 0:1), MT_BIT_OFFSET(tavorprm_ud_address_vector_st, max_stat_rate),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, max_stat_rate));
  
  /* msg size - we put allays the max value */
  MT_INSERT_ARRAY32(new_av_arr, 3, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, msg),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, msg));
  
  /* mgid_index (index to port GID table) - 6th (LOG2_PORT_GID_TABLE_SIZE+1) bit is (port-1) */
  MT_INSERT_ARRAY32(new_av_arr, 
    (av_p->sgid_index & MASK32(LOG2_PORT_GID_TABLE_SIZE)) | 
      ((av_p->port - 1) << LOG2_PORT_GID_TABLE_SIZE), 
    MT_BIT_OFFSET(tavorprm_ud_address_vector_st, mgid_index), 
    MT_BIT_SIZE(tavorprm_ud_address_vector_st, mgid_index));
  
  /* flow_label */
  MT_INSERT_ARRAY32(new_av_arr, av_p->flow_label, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, flow_label),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, flow_label));
  
  /* tclass */
  MT_INSERT_ARRAY32(new_av_arr, av_p->traffic_class, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, tclass),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, tclass));
  
  /* sl */
  MT_INSERT_ARRAY32(new_av_arr, av_p->sl, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, sl),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, sl));
  
  
  /* the gid is coming in BE format so we can insert it directly to the av_entry */
  /* need to fill it only if GRH bit is set */
  if (av_p->grh_flag) {
    /* rgid_127_96 */
    MT_INSERT_ARRAY32(av_entry_p, ((u_int32_t*)(av_p->dgid))[0], MT_BIT_OFFSET(tavorprm_ud_address_vector_st, rgid_127_96),
                   MT_BIT_SIZE(tavorprm_ud_address_vector_st, rgid_127_96));
    
    /* rgid_95_64 */
    MT_INSERT_ARRAY32(av_entry_p, ((u_int32_t*)(av_p->dgid))[1], MT_BIT_OFFSET(tavorprm_ud_address_vector_st, rgid_95_64),
                   MT_BIT_SIZE(tavorprm_ud_address_vector_st, rgid_95_64));
    
    /* rgid_63_32 */
    MT_INSERT_ARRAY32(av_entry_p, ((u_int32_t*)(av_p->dgid))[2], MT_BIT_OFFSET(tavorprm_ud_address_vector_st, rgid_63_32),
                   MT_BIT_SIZE(tavorprm_ud_address_vector_st, rgid_63_32));
    
    /* rgid_31_0 */
    MT_INSERT_ARRAY32(av_entry_p, ((u_int32_t*)(av_p->dgid))[3], MT_BIT_OFFSET(tavorprm_ud_address_vector_st, rgid_31_0),
                   MT_BIT_SIZE(tavorprm_ud_address_vector_st, rgid_31_0));
  }
  else { /* Arbel mode workaround - must give GRH >1 in lowest bits*/
    /* rgid_31_0 */
    MT_INSERT_ARRAY32(av_entry_p, 2, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, rgid_31_0),
                   MT_BIT_SIZE(tavorprm_ud_address_vector_st, rgid_31_0));

  }
  
  /* now copy to the entry in the table with correct endianess */
  /* need to write the first DWORDs last since this is the PD that indicates 
     to HW that this entry is valid */
  
  for (i = RGID_OFFSET-1; i >= 0; i--) {
    /*MTL_DEBUG1(MT_FLFMT("(i=%d) %p <- 0x%X"),i,av_entry_p+i,new_av_arr[i]);*/
    MOSAL_MMAP_IO_WRITE_DWORD(av_entry_p+i,MOSAL_cpu_to_be32(new_av_arr[i])); 
  }
 
  FUNC_OUT;

}

/************************************************************************************/

HH_ret_t THH_udavm_parse_udav_entry(u_int32_t    *ud_av_p, 
                         VAPI_ud_av_t *av_p)
{
  u_int32_t i;
  u_int32_t tmp_av_arr[8] = {0,0,0,0,0,0,0,0}; /* entry size is 32 bytes */
  HH_pd_hndl_t pd_tmp = 0;
  
  FUNC_IN;
  /* read entry to tmp area */
  /* the gid should stay in BE format so we don't change its endianess */
  for (i=0; i<RGID_OFFSET; i++) {
    tmp_av_arr[i] = MOSAL_be32_to_cpu(MOSAL_MMAP_IO_READ_DWORD(ud_av_p+i)); 
  }

  /* PD */
  pd_tmp = MT_EXTRACT_ARRAY32(tmp_av_arr, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, pd),
                           MT_BIT_SIZE(tavorprm_ud_address_vector_st, pd));

  /* if PD is zero it means that the entry is not valid */
  if (pd_tmp == 0) {
    MT_RETURN(HH_EINVAL);
  }

  /* port */
  av_p->port = MT_EXTRACT_ARRAY32(tmp_av_arr, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, port_number),
                  MT_BIT_SIZE(tavorprm_ud_address_vector_st, port_number));
  
  /* rlid */
  av_p->dlid = MT_EXTRACT_ARRAY32(tmp_av_arr, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, rlid),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, rlid));

  /* mylid_path_bits */
  av_p->src_path_bits = MT_EXTRACT_ARRAY32(tmp_av_arr, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, my_lid_path_bits),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, my_lid_path_bits));
  /* grh enable */
  av_p->grh_flag = MT_EXTRACT_ARRAY32(tmp_av_arr, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, g),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, g));
  /* hop_limit */
  av_p->hop_limit = MT_EXTRACT_ARRAY32(tmp_av_arr, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, hop_limit),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, hop_limit));

  /* max_stat_rate */
  /* 0 - stay 0, 1 transmitted to 3 as defined in IPD encoding: IB-spec. 9.11.1, table 63 */
  i = MT_EXTRACT_ARRAY32(tmp_av_arr, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, max_stat_rate),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, max_stat_rate));
  av_p->static_rate = (i ? 3:0);
  
  /* msg size - not needed for the av info */
    
  /* mgid_index (index to port GID table)*/
  av_p->sgid_index = MT_EXTRACT_ARRAY32(tmp_av_arr, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, mgid_index),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, mgid_index)) & MASK32(LOG2_PORT_GID_TABLE_SIZE);
  /* TBD: sanity check that 6th bit of mgid_index matches port field (-1) */
  
  /* flow_label */
  av_p->flow_label = MT_EXTRACT_ARRAY32(tmp_av_arr, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, flow_label),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, flow_label));
  
  /* tclass */
  av_p->traffic_class = MT_EXTRACT_ARRAY32(tmp_av_arr, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, tclass),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, tclass));
  
  /* sl */
  av_p->sl = MT_EXTRACT_ARRAY32(tmp_av_arr, MT_BIT_OFFSET(tavorprm_ud_address_vector_st, sl),
                 MT_BIT_SIZE(tavorprm_ud_address_vector_st, sl));
  
  /* gid stays in BE so it is extracted directly from the ud_av_p */
  /* rgid_127_96 */
  memcpy(av_p->dgid,ud_av_p+(MT_BYTE_OFFSET(tavorprm_ud_address_vector_st, rgid_127_96) >> 2),
         sizeof(av_p->dgid));

  
#if 1 <= MAX_DEBUG
  print_udav(av_p);
#endif  
  MT_RETURN(HH_OK);

  
}  

/************************************************************************************/
#ifdef MAX_DEBUG

static void print_udav(VAPI_ud_av_t *av_p)
{
  FUNC_IN;
  MTL_DEBUG1("UDAV values:\n==================\n sl = %d \n dlid = %d\n src_path_bits = %d\n static_rate = %d\n grh_flag = %d\n traffic_class = %d\n"
         " flow_label = %d\n hop_limit = %d\n sgid_index = %d\n port = %d\n, dgid = %d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d\n",
         av_p->sl, av_p->dlid, av_p->src_path_bits, av_p->static_rate, av_p->grh_flag, av_p->traffic_class, av_p->flow_label,
         av_p->hop_limit, av_p->sgid_index, av_p->port, av_p->dgid[0],av_p->dgid[1],av_p->dgid[2],av_p->dgid[3],av_p->dgid[4],av_p->dgid[5],
         av_p->dgid[6],av_p->dgid[7],av_p->dgid[8],av_p->dgid[9],av_p->dgid[10],av_p->dgid[11],
         av_p->dgid[12],av_p->dgid[13],av_p->dgid[14],av_p->dgid[15]);
  FUNC_OUT;
}


#endif

