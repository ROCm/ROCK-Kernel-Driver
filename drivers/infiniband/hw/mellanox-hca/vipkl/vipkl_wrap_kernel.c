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
#include "vipkl_wrap.h"
#include <vapi_common.h>
/* #include "vipkl_hpool.c" - obsolete */


#define MTPERF
#include <mtperf.h>
MTPERF_NEW_SEGMENT(evapi_comp_stub,10000);

/* 
	Wrapper functions' implementation
	-----------------------------

1. Main ideas
	- user-level wrapper serializes all input parameters into an INPUT buffer; output ones -into an OUPUT buffer;
	- pointers are never relayed: if a parameter is a pointer to a structure/buffer, 
	   the structure/buffer is copied to the INPUT buffer;
	- kernel function return code is always relayed as the first field of the OUTPUT buffer;
	- the INPUT/OUTPUT buffers are relayed to/from kernel by an OS-dependent function;

2. Implementation detailes
	- user-level wrapper use the following
	  a) conventions:
	  	-- for function XXX, 
	  		INPUT buffer has 'struct i_XXX_ops_t' type,
	  		OUTPUT buffer has 'struct i_XXX_ops_t' type;
	  	-- if a function parameter is pointer to a structure or array, it must be named YYY_p and 
	  	    the appropriate field of INPUT or OUTPUT buffer must be named YYY;
	  	-- if a function parameter is a value of an integral type, its name must be the same, as the 
	  	    name of the appropriate INPUT buffer field;
	  	    
	  b) local variables:
	  	-- pi 	- pointer to the INPUT buffer of 'struct i_<func_name>_ops_t *' type
	  	-- pi_sz	- size of the INPUT buffer;
	  	-- po 	- pointer to the OUTPUT buffer of 'struct o_<func_name>_ops_t *' type
	  	-- pi_sz	- size of the OUTPUT buffer;

	- in accord to above conventions, kernel wrapper functions have the prototype (say, for function XXX):
		static VIP_ret_t XXX_stat(				// function name; has suffuix '_stat', shown in prints					
			IN	struct i_XXX_ops_t * pi, 			// typed pointer to the INPUT buffer
			IN	struct o_XXX_ops_t * po, 			// typed pointer to the OUTPUT buffer
			OUT	u_int32_t* ret_po_sz_p				// returned size the OUTPUT buffer (see more below)
			);

	- an OS-dependent kernel function is responsible for relaying the INPUT and OUTPUT buffers, prepared by
	   by the user-level wrapper, into the kernel. It may do that by zero-copying technology, just mapping them 
	   into kernel. But for generality we assume, that the kernel function allocates buffers in kernel space and 
	   copies buffers to/from user space. Because of this assumption comes the last parameter of the wrapper
	   functions, that tells what part of the OUTPUT buffer (from the beginning) is to be copied back to user space.

	- the wrapper functions are written with the help of macros, shortly documented below;   
*/

/* return code of the kernel function */
#define W_RET					po->ret

/* Generic input parameters size check (FM #18007) */                                                   
#define W_IPARAM_SZ_CHECK_DEFAULT(name)                                                           \
  if (sizeof(struct i_##name##_ops_t) > pi_sz) {                                                  \
     MTL_ERROR1(MT_FLFMT("%s: Input parameters buffer is too small "                              \
      "(sizeof(i_"#name"_ops_t)="SIZE_T_DFMT" > pi_sz="SIZE_T_DFMT")"),                                                 \
      __func__, sizeof(struct i_##name##_ops_t), pi_sz);  \
     return VIP_EINVAL_PARAM;                                                                     \
  }

/* Generic output parameters size check (FM #18007) */                                                   
#define W_OPARAM_SZ_CHECK_DEFAULT(name)                                                           \
  if (sizeof(struct o_##name##_ops_t) > po_sz) {                                                  \
     MTL_ERROR1(MT_FLFMT("%s: Output parameters buffer is too small "                             \
      "(sizeof(o_"#name"_ops_t)="SIZE_T_DFMT" > po_sz="SIZE_T_DFMT")"),       \
      __func__, sizeof(struct o_##name##_ops_t), po_sz);  \
     return VIP_EINVAL_PARAM;                                                                     \
  }

/* Size check for input buffer which includes variable size field (array[1]) */
#define W_IPARAM_SZ_CHECK_VARSIZE_BASE(name,vardata)                                              \
  if (FIELD_OFFSET(struct i_##name##_ops_t, vardata) > pi_sz) {                                   \
     MTL_ERROR1(MT_FLFMT("%s: Input parameters buffer is too small "                              \
      "(FIELD_OFFSET(struct i_"#name"_ops_t, "#vardata")=%lu > pi_sz="SIZE_T_DFMT")"),                       \
      __func__, FIELD_OFFSET(struct i_##name##_ops_t, vardata), pi_sz);                           \
     return VIP_EINVAL_PARAM;                                                                     \
  }

/* Size check for output buffer which includes variable size field (array[1]) */
#define W_OPARAM_SZ_CHECK_VARSIZE_BASE(name,vardata)                                              \
  if (FIELD_OFFSET(struct o_##name##_ops_t, vardata) > po_sz) {                                   \
     MTL_ERROR1(MT_FLFMT("%s: Output parameters buffer is too small "                             \
      "(FIELD_OFFSET(struct o_"#name"_ops_t, "#vardata")=%lu > pi_sz="SIZE_T_DFMT")"),                       \
      __func__, FIELD_OFFSET(struct o_##name##_ops_t, vardata), pi_sz);                           \
     return VIP_EINVAL_PARAM;                                                                     \
  }
  
/* Verify given input buffer size matches given table length (FM #18007)*/
#define W_IPARAM_SZ_CHECK_TABLE(name,table_base,table_len_fld)                                    \
       if ((pi_sz < FIELD_OFFSET(struct i_##name##_ops_t, table_base)) || /* _sz field is valid */\
           (pi_sz < FIELD_OFFSET(struct i_##name##_ops_t, table_base) +                           \
            pi->table_len_fld*sizeof(pi->table_base)) ) {                                         \
         MTL_ERROR1(MT_FLFMT(                                                                     \
           "%s: Given input " #table_base                                                         \
           " buffer is smaller (%lu entries) than declared ("SIZE_T_DFMT" entries) (pi_sz="SIZE_T_DFMT")"),             \
           __func__,                                                                              \
           (pi_sz - FIELD_OFFSET(struct i_##name##_ops_t, table_base)) / sizeof(pi->table_base) , \
           (pi_sz >= FIELD_OFFSET(struct i_##name##_ops_t, table_base)) ? (MT_size_t)pi->table_len_fld : 0,  \
           pi_sz );                                                                               \
         return VIP_EINVAL_PARAM;                                                                 \
       }

/* Verify given output buffer size matches given table length (FM #18007) */
/* Use this macro only after verifying validity of input params buffer    */
/* (it refers to pi->table_len_fld)                                       */
#define W_OPARAM_SZ_CHECK_TABLE(name,table_base,table_len_fld)                                    \
       if (po_sz < FIELD_OFFSET(struct o_##name##_ops_t, table_base) +                            \
            pi->table_len_fld*sizeof(po->table_base)) {                                           \
         MTL_ERROR1(MT_FLFMT(                                                                     \
           "%s: Given output " #table_base                                                        \
           " buffer is smaller (%lu entries) than declared ("SIZE_T_DFMT" entries)"),                        \
           __func__,                                                                              \
           (po_sz - FIELD_OFFSET(struct o_##name##_ops_t, table_base)) / sizeof(po->table_base) , \
           (MT_size_t)pi->table_len_fld);                                                                    \
         return VIP_EINVAL_PARAM;                                                                 \
       }

/* Verify given input buffer size matches given *_ul_resources_sz (FM #18007)*/
#define W_IPARAM_SZ_CHECK_ULRES(name,vardata)                                                     \
       if ((pi_sz < FIELD_OFFSET(struct i_##name##_ops_t, vardata)) || /* _sz field is valid */   \
           (pi_sz < FIELD_OFFSET(struct i_##name##_ops_t, vardata) + pi->vardata##_sz )) {        \
         MTL_ERROR1(MT_FLFMT(                                                                     \
           "%s: Given input ul_res buffer is smaller (%lu B) than declared ("SIZE_T_DFMT" B)"),__func__,     \
           pi_sz - FIELD_OFFSET(struct i_##name##_ops_t, vardata),                                \
           (pi_sz >= FIELD_OFFSET(struct i_##name##_ops_t, vardata)) ? pi->vardata##_sz : 0 );    \
         return VIP_EINVAL_PARAM;                                                                 \
         }

/* Verify given output buffer size matches given *_ul_resources_sz (FM #18007)*/
/* Use this macro only after verifying validity of input params buffer (it refers to pi->*_sz)   */
#define W_OPARAM_SZ_CHECK_ULRES(name,vardata)                                                     \
       if (po_sz < FIELD_OFFSET(struct o_##name##_ops_t, vardata) + pi->vardata##_sz ) {          \
         MTL_ERROR1(MT_FLFMT(                                                                     \
           "%s: Given output ul_res buffer is smaller (%lu B) than declared ("SIZE_T_DFMT" B)"),__func__,    \
           po_sz - FIELD_OFFSET(struct o_##name##_ops_t, vardata),                                \
           pi->vardata##_sz);                                                                     \
         return VIP_EINVAL_PARAM;                                                                 \
       }

#define W_DEFAULT_PARAM_SZ_CHECK(name)      \
  W_IPARAM_SZ_CHECK_DEFAULT(name)           \
  W_OPARAM_SZ_CHECK_DEFAULT(name)

/* create function prototype */
#define W_FUNC_PROTOTYPE(name)        \
static int name##_stat(               \
  VIP_hca_state_t* hca_rsct_arr,      \
  struct i_##name##_ops_t * pi,       \
  MT_size_t pi_sz,                    \
  struct o_##name##_ops_t * po,       \
  MT_size_t po_sz,                    \
  u_int32_t* ret_po_sz_p)             \
{


/* create function prototype and print it name by MTL_DEBUG3 */
#define W_FUNC_START(name)                  \
  W_FUNC_PROTOTYPE(name)                    \
  MTL_DEBUG3("CALLING " #name "_stat \n");  \
  W_DEFAULT_PARAM_SZ_CHECK(name)
  
/* Used when one needs to use local variables in the wrapper function */
/* Use W_DEFAULT_PARAM_SZ_CHECK after local variables are declared    */
#define W_FUNC_START_WO_PRINT(name)   W_FUNC_PROTOTYPE(name)              

#define W_FUNC_START_WO_PARAM_SZ_CHECK(name)  \
  W_FUNC_PROTOTYPE(name)                      \
  MTL_DEBUG3("CALLING " #name "_stat \n");  
  


/* print function name by MTL_DEBUG3. Used when one uses more local variables in the wrapper function */
#define W_FUNC_NAME(name)  {MTL_DEBUG3("CALLING " #name "_stat \n");}

/* This return code is the wrapper admin. return code - 
 *  not W_RET, which is the result of the VIPKL call.
 * If we reached here, the wrapper operation was OK (parameters were passed successfully) */
#define W_FUNC_END  return VIP_OK; }

/* return call_result and end the function */
#define W_FUNC_END_RC_SZ                                                               \
  if ((W_RET != VIP_OK)	&& (po_sz >= sizeof(VIP_ret_t)))  *ret_po_sz_p = sizeof(VIP_ret_t);  \
  W_FUNC_END

/**************** resource tracking functions & macros ************************
 ******************************************************************************/


static inline VIP_ret_t VIPKL_RSCT_check_inc(VIP_hca_hndl_t hca_hndl,
                                             VIP_hca_state_t* hca_p)
{      
  if (hca_hndl > VIP_MAX_HCA) return VIP_EINVAL_HCA_HNDL;
  //MTL_DEBUG1("before spinlock s \n");
  if (MOSAL_spinlock_lock(&hca_p->lock) != MT_OK) return VIP_EINVAL_PARAM;  
  if (hca_p->state < VIP_HCA_STATE_AVAIL) { 
      MOSAL_spinlock_unlock(&hca_p->lock);    
      MTL_ERROR1("error: kernel wrap: given hca was not registered !! \n");   
      MTL_ERROR1("error: state: %d !! \n",hca_p->state);   
      MTL_ERROR1("error: hca: %d !! \n",hca_hndl);  
      return VIP_EINVAL_HCA_HNDL;    
  } else {
    hca_p->state++; 
  }
  //MTL_DEBUG1("inc hca:%d state:%d $$ \n",hca_hndl,hca_p->state);  
  MOSAL_spinlock_unlock(&hca_p->lock);    
  //MTL_DEBUG1("after spinlock s \n");
  return VIP_OK;
}

static inline VIP_ret_t VIPKL_RSCT_check(VIP_hca_hndl_t hca_hndl,
                                         VIP_hca_state_t* hca_p)
{      
  if (hca_hndl > VIP_MAX_HCA) 
    return VIP_EINVAL_HCA_HNDL;
  if (MOSAL_spinlock_lock(&hca_p->lock) != MT_OK) 
    return VIP_EINVAL_PARAM;  
  if (hca_p->state < VIP_HCA_STATE_AVAIL) { 
    MOSAL_spinlock_unlock(&hca_p->lock);    
    MTL_ERROR1("error: kernel wrap: given hca was not registered !! \n");   
    MTL_ERROR1("error: state: %d !! \n",hca_p->state);   
    MTL_ERROR1("error: hca: %d !! \n",hca_hndl);  
    return VIP_EINVAL_HCA_HNDL;    
  }
  MOSAL_spinlock_unlock(&hca_p->lock);   
  return VIP_OK;
}


static inline VIP_ret_t VIPKL_RSCT_check_async_hndl_ctx(VIP_hca_hndl_t hca_hndl,
                                                        VIP_hca_state_t* hca_p,
                                                        EM_async_ctx_hndl_t async_hndl_ctx)

{      
  if (hca_hndl > VIP_MAX_HCA) 
    return VIP_EINVAL_HCA_HNDL;
  //MTL_DEBUG1("VIPKL_RSCT_check_async_hndl_ctx: before spinlock s \n");
  if (MOSAL_spinlock_lock(&hca_p->lock) != MT_OK) 
    return VIP_EINVAL_PARAM;  
  if (hca_p->rsct->async_ctx_hndl != async_hndl_ctx) { 
    MOSAL_spinlock_unlock(&hca_p->lock);    
    MTL_ERROR1("error: kernel wrap: The async_hndl_ctx (%d) != to context (%d) saved in get_hca_hndl \n",
               async_hndl_ctx, hca_p->rsct->async_ctx_hndl);   
    return VIP_EINVAL_PARAM;    
  }
  MOSAL_spinlock_unlock(&hca_p->lock);    
  //MTL_DEBUG1("VIPKL_RSCT_check_async_hndl_ctx: after spinlock s \n");
  return VIP_OK;
}


#define VIPKL_RSCT_DEC  MOSAL_spinlock_lock(&hca_rsct_arr[pi->hca_hndl].lock);  \
                            hca_rsct_arr[pi->hca_hndl].state--; \
                            MOSAL_spinlock_unlock(&hca_rsct_arr[pi->hca_hndl].lock);

#define RSCT_HNDL           hca_rsct_arr[pi->hca_hndl].rsct
                            
#define RSCT_LOCK   MOSAL_spinlock_lock(&hca_rsct_arr[pi->hca_hndl].lock);
#define RSCT_UNLOCK MOSAL_spinlock_unlock(&hca_rsct_arr[pi->hca_hndl].lock);



static VIP_ret_t VIPKL_destroy_hca_resources(VIP_hca_state_t* hca_p);


/*      *               *               *               *               *           */

/* These static functions decode parameters
 * and delegate each to the appropriate VIP KL API */

/*****************************************
  struct i_VIPKL_open_hca_ops_t {
    IN VAPI_hca_id_t hca_id;
    IN MT_bool profile_is_null;
    IN EVAPI_hca_profile_t profile;
  };
  struct o_VIPKL_open_hca_ops_t {
    OUT VIP_ret_t ret;
    OUT EVAPI_hca_profile_t sugg_profile;
    OUT VIP_hca_hndl_t hca_hndl;
  };
******************************************/
W_FUNC_START(VIPKL_open_hca)
    pi->hca_id[HCA_MAXNAME - 1]= 0;  /* In case string is not null terminated (FM #18035)*/
    W_RET = VIPKL_open_hca( pi->hca_id, 
                          ((pi->profile_is_null) ? NULL : &pi->profile),
                          ((pi->sugg_profile_is_null) ? NULL : &po->sugg_profile),
                          &po->hca_hndl );
W_FUNC_END

/*****************************************
  struct i_VIPKL_close_hca_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  };
  struct o_VIPKL_close_hca_ops_t {
    OUT VIP_ret_t ret;
  };
******************************************/
W_FUNC_START(VIPKL_close_hca)
    W_RET = VIPKL_close_hca(pi->hca_hndl );
W_FUNC_END

/*****************************************
  struct i_VIPKL_get_hca_hndl_ops_t {
    IN VAPI_hca_id_t hca_id; 
  };
  struct o_VIPKL_get_hca_hndl_ops_t {
    OUT VIP_ret_t ret;
    OUT VIP_hca_hndl_t 	hca_hndl;
    OUT HH_hca_hndl_t 	hh_hndl;
  };
******************************************/
W_FUNC_START(VIPKL_get_hca_hndl)
    pi->hca_id[HCA_MAXNAME - 1]= 0;  /* In case string is not null terminated (FM #18035)*/
    W_RET = VIPKL_get_hca_hndl( pi->hca_id, &po->hca_hndl, &po->hh_hndl );
W_FUNC_END_RC_SZ

/*****************************************
struct i_VIPKL_list_hcas_ops_t {
  IN u_int32_t         hca_id_buf_sz; 
};
struct o_VIPKL_list_hcas_ops_t {
  OUT VIP_ret_t ret;
  OUT u_int32_t       num_of_hcas;
  OUT VAPI_hca_id_t   hca_id_buf[1];
};
******************************************/
W_FUNC_START_WO_PARAM_SZ_CHECK(VIPKL_list_hcas)
  W_IPARAM_SZ_CHECK_DEFAULT(VIPKL_list_hcas);
  W_OPARAM_SZ_CHECK_TABLE(VIPKL_list_hcas,hca_id_buf,hca_id_buf_sz);

	MTL_DEBUG4("VIPKL_list_hcas_stat: in_size = %u\n", pi->hca_id_buf_sz);
	if (!pi->hca_id_buf_sz)
	  	W_RET = VIPKL_list_hcas( 0, &po->num_of_hcas, NULL );
  else  
      W_RET = VIPKL_list_hcas( pi->hca_id_buf_sz, &po->num_of_hcas, po->hca_id_buf );
  if (W_RET == VIP_OK || W_RET == VIP_EAGAIN) {
  	int num_to_copy = (pi->hca_id_buf_sz > 0) ? MIN(pi->hca_id_buf_sz,po->num_of_hcas) : 0;  		
		*ret_po_sz_p = FIELD_OFFSET(struct o_VIPKL_list_hcas_ops_t, hca_id_buf) + sizeof(VAPI_hca_id_t) * num_to_copy;	
	} else {
		*ret_po_sz_p = sizeof(VIP_ret_t);
	  MTL_ERROR2("VIPKL_list_hcas_stat: Error in VIPKL_list_hcas: %d\n", W_RET);
	}
W_FUNC_END

/*****************************************
  struct i_VIPKL_alloc_ul_resources_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  	IN MOSAL_protection_ctx_t prot_ctx;
    IN MT_size_t hca_ul_resources_sz;
  };
  struct o_VIPKL_alloc_ul_resources_ops_t {
    OUT VIP_ret_t ret;
    OUT EM_async_ctx_hndl_t async_hndl_ctx_p;
    OUT unsigned long hca_ul_resources[1];
  };
******************************************/
W_FUNC_START_WO_PRINT(VIPKL_alloc_ul_resources)
	MT_size_t         hca_ul_resources_sz;
 	HH_hca_hndl_t     hh_hndl;
	W_FUNC_NAME(VIPKL_alloc_ul_resources);
  
  W_IPARAM_SZ_CHECK_DEFAULT(VIPKL_alloc_ul_resources);
  W_OPARAM_SZ_CHECK_ULRES(VIPKL_alloc_ul_resources, hca_ul_resources);

	if (pi->hca_hndl > VIP_MAX_HCA) {
    MTL_ERROR1(" Invalid hca handle \n");
    W_RET = VIP_EINVAL_HCA_HNDL;
    goto exit;
  }

  RSCT_LOCK
  if (hca_rsct_arr[pi->hca_hndl].state != VIP_HCA_STATE_EMPTY) {
    MTL_ERROR1(MT_FLFMT("error: UL resources for this hca already allocated: hca_hdl=%u, state=%d\n"),
                        pi->hca_hndl, hca_rsct_arr[pi->hca_hndl].state);
    RSCT_UNLOCK
    W_RET = VIP_EINVAL_HCA_HNDL;
    goto exit;
  }
  hca_rsct_arr[pi->hca_hndl].state = VIP_HCA_STATE_CREATE;
  MTL_DEBUG1("state: %d $$ \n",hca_rsct_arr[pi->hca_hndl].state);   
  MTL_DEBUG1("hca: %d $$ \n",pi->hca_hndl); 
  RSCT_UNLOCK


    /* get size of qp ul resource buffer, and copy info from user space */
	W_RET = VIPKL_get_hh_hndl(pi->hca_hndl, &hh_hndl);
	if (W_RET != VIP_OK) {
   	MTL_ERROR2("VIPKL_alloc_ul_resources_stat: Cannot get HH handle.\n");
    RSCT_LOCK
    hca_rsct_arr[pi->hca_hndl].state = VIP_HCA_STATE_EMPTY;
    RSCT_UNLOCK
   	goto exit;
	}
	hca_ul_resources_sz = hh_hndl->hca_ul_resources_sz;

  /* check the size */
  if (pi->hca_ul_resources_sz != hca_ul_resources_sz) {
    MTL_ERROR2("VIPKL_alloc_ul_resources_stat: Incorrect hca_ul_resources size.\n");
    W_RET = VIP_EINVAL_PARAM;
    RSCT_LOCK
    hca_rsct_arr[pi->hca_hndl].state = VIP_HCA_STATE_EMPTY;
    RSCT_UNLOCK
    goto exit;
  }

  	/* when running through the kernel wrapper, ignore the prot_ctx parameter passed in. */
	/* Get the true protection context parameter here, and use that in the call to create_pd*/
	pi->prot_ctx = MOSAL_get_current_prot_ctx();
	
  W_RET = VIP_RSCT_create(&hca_rsct_arr[pi->hca_hndl].rsct,pi->hca_hndl,
                          pi->prot_ctx, pi->hca_ul_resources_sz, &po->hca_ul_resources[0],
                          &po->async_hndl_ctx);
  if (W_RET != VIP_OK) {
    RSCT_LOCK
    hca_rsct_arr[pi->hca_hndl].state = VIP_HCA_STATE_EMPTY;
    RSCT_UNLOCK
    goto exit;
  }
  *ret_po_sz_p = (u_int32_t)(FIELD_OFFSET( struct o_VIPKL_alloc_ul_resources_ops_t, hca_ul_resources ) + pi->hca_ul_resources_sz);  		
  
  RSCT_LOCK
  hca_rsct_arr[pi->hca_hndl].state = VIP_HCA_STATE_AVAIL;
  MTL_DEBUG1("state: %d $$ \n",hca_rsct_arr[pi->hca_hndl].state);   
  MTL_DEBUG1("hca: %d $$ \n",pi->hca_hndl); 
  RSCT_UNLOCK
  MTL_DEBUG3( "VIPKL_alloc_ul_resources_stat: user resources contents: 0x%x; HH hndl = %p\n", *(unsigned int *)&po->hca_ul_resources[0], hh_hndl);

exit:
W_FUNC_END_RC_SZ

/*****************************************
  struct i_VIPKL_free_ul_resources_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN MT_size_t hca_ul_resources_sz;
    IN EM_async_ctx_hndl_t async_hndl_ctx;
    IN unsigned long  hca_ul_resources[1];
  };
  struct o_VIPKL_free_ul_resources_ops_t {
    OUT VIP_ret_t ret;
  };
******************************************/
W_FUNC_START_WO_PRINT(VIPKL_free_ul_resources)
  HH_hca_hndl_t     hh_hndl;
	W_FUNC_NAME(VIPKL_free_ul_resources);

  W_IPARAM_SZ_CHECK_ULRES(VIPKL_free_ul_resources, hca_ul_resources);
  W_OPARAM_SZ_CHECK_DEFAULT(VIPKL_free_ul_resources);
	
  if (pi->hca_hndl > VIP_MAX_HCA) {
    MTL_ERROR1(" Invalid hca handle \n");
    W_RET = VIP_EINVAL_HCA_HNDL;
    goto exit;
  }

    /* get size of qp ul resource buffer, and copy info from user space */
	W_RET = VIPKL_get_hh_hndl(pi->hca_hndl, &hh_hndl);
	if (W_RET != VIP_OK) {
   	MTL_ERROR2("VIPKL_free_ul_resources_stat: Cannot get HH handle.\n");
 		goto exit;
	}
  	/* check the size */
	if (pi->hca_ul_resources_sz != hh_hndl->hca_ul_resources_sz) {
 		MTL_ERROR2("VIPKL_free_ul_resources_stat: Incorrect hca_ul_resources size.\n");
 		W_RET = VIP_EINVAL_PARAM;
 		goto exit;
	}
  	
  W_RET = VIPKL_destroy_hca_resources(&hca_rsct_arr[pi->hca_hndl]);
  if (W_RET != VIP_OK) {
     goto exit;
  }
exit:
    W_FUNC_END

/*****************************************
  struct i_VIPKL_query_hca_cap_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  };
  struct o_VIPKL_query_hca_cap_ops_t {
    OUT VIP_ret_t ret;
    OUT VAPI_hca_cap_t caps;
  };
******************************************/
W_FUNC_START(VIPKL_query_hca_cap)
  	W_RET = VIPKL_query_hca_cap(pi->hca_hndl, &po->caps );
W_FUNC_END_RC_SZ

/*****************************************
  struct i_VIPKL_query_port_prop_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN IB_port_t port_num;
  };
  struct o_VIPKL_query_port_prop_ops_t {
    OUT VIP_ret_t ret;
    OUT VAPI_hca_port_t port_props;
  };
******************************************/
W_FUNC_START(VIPKL_query_port_prop)
  	W_RET = VIPKL_query_port_prop(pi->hca_hndl, pi->port_num, &po->port_props );
W_FUNC_END_RC_SZ

/*****************************************
  struct i_VIPKL_query_port_pkey_tbl_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN IB_port_t port_num;
    IN u_int16_t tbl_len_in;
  };
  struct o_VIPKL_query_port_pkey_tbl_ops_t {
    OUT VIP_ret_t ret;
    OUT u_int16_t 	tbl_len_out;
    OUT VAPI_pkey_t pkey_tbl[1];
  };
******************************************/
W_FUNC_START_WO_PARAM_SZ_CHECK(VIPKL_query_port_pkey_tbl)
  W_IPARAM_SZ_CHECK_DEFAULT(VIPKL_query_port_pkey_tbl);
  W_OPARAM_SZ_CHECK_TABLE(VIPKL_query_port_pkey_tbl,pkey_tbl,tbl_len_in);
  
  W_RET = VIPKL_query_port_pkey_tbl( pi->hca_hndl, pi->port_num, 
            pi->tbl_len_in, &po->tbl_len_out, &po->pkey_tbl[0] );
  if (W_RET == VIP_OK || W_RET == VIP_EAGAIN) {
    *ret_po_sz_p = FIELD_OFFSET(struct o_VIPKL_query_port_pkey_tbl_ops_t,pkey_tbl) + MIN(pi->tbl_len_in,po->tbl_len_out)*sizeof(VAPI_pkey_t);  		
  } else {
    *ret_po_sz_p = sizeof(VIP_ret_t); /* tbl_len_out is invalid along with the pkey_tbl */
  }

W_FUNC_END

/*****************************************
  struct i_VIPKL_query_port_gid_tbl_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN IB_port_t port_num;
    IN u_int16_t tbl_len_in;
  };
  struct o_VIPKL_query_port_gid_tbl_ops_t {
    OUT VIP_ret_t ret;
    OUT u_int16_t  tbl_len_out;
    OUT VAPI_gid_t gid_tbl[1];
  };******************************************/
W_FUNC_START_WO_PARAM_SZ_CHECK(VIPKL_query_port_gid_tbl)
  W_IPARAM_SZ_CHECK_DEFAULT(VIPKL_query_port_gid_tbl);
  W_OPARAM_SZ_CHECK_TABLE(VIPKL_query_port_gid_tbl,gid_tbl,tbl_len_in);
  W_RET = VIPKL_query_port_gid_tbl(pi->hca_hndl, pi->port_num, 
            pi->tbl_len_in, &po->tbl_len_out, &po->gid_tbl[0] );
  	
  if (W_RET == VIP_OK || W_RET == VIP_EAGAIN) {
  		*ret_po_sz_p = FIELD_OFFSET(struct o_VIPKL_query_port_gid_tbl_ops_t,gid_tbl) + MIN(pi->tbl_len_in,po->tbl_len_out)*sizeof(VAPI_gid_t);  		
	}
  else 
		*ret_po_sz_p = sizeof(VIP_ret_t);
W_FUNC_END

/*****************************************
  struct i_VIPKL_modify_hca_attr_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN IB_port_t port_num;
    IN VAPI_hca_attr_t 		hca_attr;
    IN VAPI_hca_attr_mask_t hca_attr_mask;
  };
  struct o_VIPKL_modify_hca_attr_ops_t {
    OUT VIP_ret_t ret;
  };
******************************************/
W_FUNC_START(VIPKL_modify_hca_attr)
  	W_RET = VIPKL_modify_hca_attr(pi->hca_hndl, pi->port_num, &pi->hca_attr, &pi->hca_attr_mask );
W_FUNC_END


/*****************************************
  struct i_VIPKL_get_hh_hndl_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  };
  struct o_VIPKL_get_hh_hndl_ops_t {
    OUT VIP_ret_t ret;
    OUT HH_hca_hndl_t hh;
  };
******************************************/
W_FUNC_START(VIPKL_get_hh_hndl)
  	W_RET = VIPKL_get_hh_hndl(pi->hca_hndl, &po->hh );
W_FUNC_END_RC_SZ

/*****************************************
  struct i_VIPKL_get_hca_ul_info_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  };
  struct o_VIPKL_get_hca_ul_info_ops_t {
    OUT VIP_ret_t ret;
    OUT HH_hca_dev_t hca_ul_info;
  };
******************************************/
W_FUNC_START(VIPKL_get_hca_ul_info)
  	W_RET = VIPKL_get_hca_ul_info(pi->hca_hndl, &po->hca_ul_info );
W_FUNC_END_RC_SZ

/*****************************************
  struct i_VIPKL_get_hca_id_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  };
  struct o_VIPKL_get_hca_id_ops_t {
    OUT VIP_ret_t ret;
    OUT VAPI_hca_id_t hca_id;
  };
******************************************/
W_FUNC_START(VIPKL_get_hca_id)
  	W_RET = VIPKL_get_hca_id(pi->hca_hndl, &po->hca_id );
W_FUNC_END_RC_SZ

/*****************************************
  struct i_VIPKL_create_pd_ops_t {
  	IN VIP_hca_hndl_t hca_hndl;
	IN MOSAL_protection_ctx_t prot_ctx; 
  	IN MT_size_t pd_ul_resources_sz;
  	IN OUT unsigned long pd_ul_resources[1]; 
  };
  struct o_VIPKL_create_pd_ops_t {
       OUT VIP_ret_t ret;
  	OUT PDM_pd_hndl_t 	pd_hndl; 
  	OUT	HH_pd_hndl_t 	pd_num;
  	IN OUT unsigned long 	pd_ul_resources[1]; 
  };
******************************************/
W_FUNC_START_WO_PRINT(VIPKL_create_pd)
	MT_size_t         pd_ul_resources_sz;
  	HH_hca_hndl_t     hh_hndl;
	W_FUNC_NAME(VIPKL_create_pd);
	
  W_IPARAM_SZ_CHECK_ULRES(VIPKL_create_pd, pd_ul_resources);
  W_OPARAM_SZ_CHECK_ULRES(VIPKL_create_pd, pd_ul_resources);

	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
  if (W_RET != VIP_OK) {
    goto exit1;  
  }

  /* get size of qp ul resource buffer, and copy info from user space */
	W_RET = VIPKL_get_hh_hndl(pi->hca_hndl, &hh_hndl);
	if (W_RET != VIP_OK) {
	    	MTL_ERROR2("VIPKL_create_pd_stat: Cannot get HH handle.\n");
      		goto exit;
	}
	pd_ul_resources_sz = hh_hndl->pd_ul_resources_sz;
  	/* check the size */
	if (pi->pd_ul_resources_sz != pd_ul_resources_sz) {
   	MTL_ERROR2("VIPKL_create_pd_stat: Incorrect pd_ul_resources size.\n");
 		W_RET = VIP_EINVAL_PARAM;
 		goto exit;
	}
  	/* fix the parameters */
  	memcpy( (void*)&po->pd_ul_resources[0], (void*)&pi->pd_ul_resources[0], pi->pd_ul_resources_sz ); 
  	/* when running through the kernel wrapper, ignore the prot_ctx parameter passed in. */
	/* Get the true protection context parameter here, and use that in the call to create_pd*/
	pi->prot_ctx = MOSAL_get_current_prot_ctx();
	/* perform the function */
	W_RET = VIPKL_create_pd(RSCT_HNDL,pi->hca_hndl, pi->prot_ctx, pi->pd_ul_resources_sz, 
		&po->pd_ul_resources[0],&po->pd_hndl, &po->pd_num );
  	
    if (W_RET == VIP_OK) {
  		*ret_po_sz_p = (u_int32_t)(FIELD_OFFSET(struct o_VIPKL_create_pd_ops_t, pd_ul_resources) + pi->pd_ul_resources_sz);  		
	}
exit:
VIPKL_RSCT_DEC
exit1:
W_FUNC_END_RC_SZ

/*****************************************
  struct i_VIPKL_destroy_pd_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN PDM_pd_hndl_t pd_hndl;
  };
  struct o_VIPKL_destroy_pd_ops_t {
    OUT VIP_ret_t ret;
  };
******************************************/
W_FUNC_START(VIPKL_destroy_pd)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_destroy_pd(RSCT_HNDL,pi->hca_hndl, pi->pd_hndl);
    VIPKL_RSCT_DEC
exit1: 
        W_FUNC_END

/*****************************************
struct i_VIPKL_create_qp_ops_t {
  	IN VIP_hca_hndl_t hca_hndl;
  	VAPI_qp_hndl_t vapi_qp_hndl;
	IN QPM_qp_init_attr_t qp_init_attr;
    IN EM_async_ctx_hndl_t async_hndl_ctx;
  	IN MT_size_t qp_ul_resources_sz;
  	IN unsigned long	qp_ul_resources[1];
};
struct o_VIPKL_create_qp_ops_t {
	OUT VIP_ret_t ret;
  	OUT QPM_qp_hndl_t qp_hndl;
  	OUT VAPI_qp_num_t qp_num;
  	IN OUT unsigned long   qp_ul_resources[1]; 
};
******************************************/
W_FUNC_START_WO_PRINT(VIPKL_create_qp)
	MT_size_t         qp_ul_resources_sz;
  	HH_hca_hndl_t     hh_hndl;
	W_FUNC_NAME(VIPKL_create_qp);
	
  W_IPARAM_SZ_CHECK_ULRES(VIPKL_create_qp, qp_ul_resources);
  W_OPARAM_SZ_CHECK_ULRES(VIPKL_create_qp, qp_ul_resources);
	
  W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    /* check the the async_hndl_ctx is the same as the one assigned to this hca_hndl */
    W_RET = VIPKL_RSCT_check_async_hndl_ctx(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl], 
                                            pi->async_hndl_ctx);
    if (W_RET != VIP_OK) {
      MTL_ERROR2("VIPKL_create_qp_stat: VIPKL_RSCT_check_async_hndl_ctx failed.\n");
      goto exit;  
    }

    /* get size of qp ul resource buffer, and copy info from user space */
	W_RET = VIPKL_get_hh_hndl(pi->hca_hndl, &hh_hndl);
	if (W_RET != VIP_OK) {
	    	MTL_ERROR2("VIPKL_create_qp_stat: Cannot get HH handle.\n");
      		goto exit;
	}
	qp_ul_resources_sz = hh_hndl->qp_ul_resources_sz;
  	/* check the size */
  	if (pi->qp_ul_resources_sz != qp_ul_resources_sz) {
	      	MTL_ERROR2("VIPKL_create_qp_stat: Incorrect qp_ul_resources size.\n");
      		W_RET = VIP_EINVAL_PARAM;
      		goto exit;
  	}
  	/* fix the parameters */
  	memcpy( (void*)&po->qp_ul_resources[0], (void*)&pi->qp_ul_resources[0], pi->qp_ul_resources_sz ); 
  	
    /* perform the function */
    W_RET = VIPKL_create_qp(RSCT_HNDL,pi->hca_hndl,pi->vapi_qp_hndl,pi->async_hndl_ctx,
                            pi->qp_ul_resources_sz, &po->qp_ul_resources[0],
		&pi->qp_init_attr, &po->qp_hndl, &po->qp_num );
  	if (W_RET == VIP_OK) {
  		*ret_po_sz_p = (u_int32_t)(FIELD_OFFSET(struct o_VIPKL_create_qp_ops_t, qp_ul_resources) + pi->qp_ul_resources_sz);  		
	}
exit:
VIPKL_RSCT_DEC
exit1: 
W_FUNC_END_RC_SZ


/*****************************************
struct i_VIPKL_get_special_qp_ops_t {
  IN VIP_hca_hndl_t 	hca_hndl;
  IN VAPI_qp_hndl_t 	qp_hndl;
  IN IB_port_t 			port;
  IN VAPI_special_qp_t 	qp_type;
  IN EM_async_ctx_hndl_t async_hndl_ctx;
  IN QPM_qp_init_attr_t qp_init_attr;  
  IN MT_size_t 			qp_ul_resources_sz;
  IN OUT u_int8_t 		qp_ul_resources[1];
};                    
struct o_VIPKL_get_special_qp_ops_t {
  OUT QPM_qp_hndl_t	qp_hndl;
  OUT IB_wqpn_t		sqp_hndl;
  IN OUT u_int8_t 	qp_ul_resources[1];
};                    
******************************************/
W_FUNC_START_WO_PRINT(VIPKL_get_special_qp)
	MT_size_t         qp_ul_resources_sz;
  	HH_hca_hndl_t     hh_hndl;
	W_FUNC_NAME(VIPKL_get_special_qp);
	
  W_IPARAM_SZ_CHECK_ULRES(VIPKL_get_special_qp, qp_ul_resources);
  W_OPARAM_SZ_CHECK_ULRES(VIPKL_get_special_qp, qp_ul_resources);

	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit;  
    }

    /* check the the async_hndl_ctx is the same as the one assigned to this hca_hndl */
    W_RET = VIPKL_RSCT_check_async_hndl_ctx(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl], 
                                            pi->async_hndl_ctx);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    /* get size of qp ul resource buffer, and copy info from user space */
	W_RET = VIPKL_get_hh_hndl(pi->hca_hndl, &hh_hndl);
	if (W_RET != VIP_OK) {
	    	MTL_ERROR2("VIPKL_get_special_qp_stat: Cannot get HH handle.\n");
      		goto exit1;
	}
	qp_ul_resources_sz = hh_hndl->qp_ul_resources_sz;
  	/* check the size */
  	if (pi->qp_ul_resources_sz != qp_ul_resources_sz) {
	      	MTL_ERROR2("VIPKL_get_special_qp_stat: Incorrect qp_ul_resources size.\n");
	      	W_RET = VIP_EINVAL_PARAM;
      		goto exit1;
  	}
  	/* fix the parameters */
  	memcpy( (void*)&po->qp_ul_resources[0], (void*)&pi->qp_ul_resources[0], pi->qp_ul_resources_sz ); 
  	pi->qp_init_attr.ts_type=IB_TS_UD;

	/* perform the function */
	W_RET = VIPKL_get_special_qp(RSCT_HNDL, pi->hca_hndl, pi->qp_hndl, pi->async_hndl_ctx,
                                 pi->qp_ul_resources_sz, &po->qp_ul_resources[0], pi->port, 
                                 pi->qp_type, &pi->qp_init_attr, &po->qp_hndl, &po->sqp_hndl );
    if (W_RET == VIP_OK) {
  		*ret_po_sz_p = (u_int32_t)(FIELD_OFFSET(struct o_VIPKL_get_special_qp_ops_t, qp_ul_resources) + pi->qp_ul_resources_sz);  		
	}
    
exit1:
    VIPKL_RSCT_DEC
exit:     
W_FUNC_END_RC_SZ

/*****************************************
  struct i_VIPKL_destroy_qp_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN QPM_qp_hndl_t qp_hndl;
  };
  struct o_VIPKL_destroy_qp_ops_t {
	OUT VIP_ret_t ret;
  };
******************************************/
W_FUNC_START(VIPKL_destroy_qp)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_destroy_qp(RSCT_HNDL, pi->hca_hndl, pi->qp_hndl,FALSE );
    VIPKL_RSCT_DEC
exit1: 
W_FUNC_END

/*****************************************
  struct i_VIPKL_modify_qp_ops_t {
    IN VIP_hca_hndl_t 		hca_hndl;
    IN QPM_qp_hndl_t 		qp_hndl;
    IN VAPI_qp_attr_mask_t 	qp_mod_mask;
    IN VAPI_qp_attr_t 		qp_mod_attr;
  };
  struct o_VIPKL_modify_qp_ops_t {
	OUT VIP_ret_t ret;
  };
******************************************/
W_FUNC_START(VIPKL_modify_qp)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }


    W_RET = VIPKL_modify_qp(RSCT_HNDL,pi->hca_hndl, pi->qp_hndl, &pi->qp_mod_mask, &pi->qp_mod_attr );
    VIPKL_RSCT_DEC
exit1: 
W_FUNC_END

/*****************************************
  struct i_VIPKL_query_qp_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN QPM_qp_hndl_t qp_hndl;
  };
  struct o_VIPKL_query_qp_ops_t {
    OUT VIP_ret_t ret;
    OUT QPM_qp_query_attr_t qp_query_prop;
    OUT VAPI_qp_attr_mask_t qp_mod_mask;
  };
******************************************/
W_FUNC_START(VIPKL_query_qp)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }


    W_RET = VIPKL_query_qp(RSCT_HNDL, pi->hca_hndl, pi->qp_hndl, &po->qp_query_prop, &po->qp_mod_mask );
    VIPKL_RSCT_DEC
exit1: 
W_FUNC_END_RC_SZ


/******************************************************************************
 *  Function: VIPKL_create_srq <==> SRQM_create_srq
 *****************************************************************************/
/*
struct i_VIPKL_create_srq_ops_t {
                  VIP_hca_hndl_t hca_hndl;
                  VAPI_srq_hndl_t   vapi_srq_hndl;
                  PDM_pd_hndl_t     pd_hndl;
                  EM_async_ctx_hndl_t async_hndl_ctx;
                  MT_size_t srq_ul_resources_sz;
                  MT_ulong_ptr_t srq_ul_resources[1];
};

struct o_VIPKL_create_srq_ops_t {
                  VIP_ret_t ret;
                  SRQM_srq_hndl_t srq_hndl;
                  MT_ulong_ptr_t srq_ul_resources[1]; 
              };
*/              
W_FUNC_START_WO_PRINT(VIPKL_create_srq)
	MT_size_t         srq_ul_resources_sz;
  HH_hca_hndl_t     hh_hndl;
	W_FUNC_NAME(VIPKL_create_srq);
	
  W_IPARAM_SZ_CHECK_ULRES(VIPKL_create_srq, srq_ul_resources);
  W_OPARAM_SZ_CHECK_ULRES(VIPKL_create_srq, srq_ul_resources);
	
  W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
  if (W_RET != VIP_OK) {
    goto exit1;  
  }

  /* check the the async_hndl_ctx is the same as the one assigned to this hca_hndl */
  W_RET = VIPKL_RSCT_check_async_hndl_ctx(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl], 
                                          pi->async_hndl_ctx);
  if (W_RET != VIP_OK) {
    MTL_ERROR2("VIPKL_create_srq_stat: VIPKL_RSCT_check_async_hndl_ctx failed.\n");
    goto exit;  
  }

    /* get size of srq ul resource buffer, and copy info from user space */
	W_RET = VIPKL_get_hh_hndl(pi->hca_hndl, &hh_hndl);
	if (W_RET != VIP_OK) {
	    	MTL_ERROR2("VIPKL_create_srq_stat: Cannot get HH handle.\n");
      		goto exit;
	}
	srq_ul_resources_sz = hh_hndl->srq_ul_resources_sz;
  	/* check the size */
  	if (pi->srq_ul_resources_sz != srq_ul_resources_sz) {
	      	MTL_ERROR2(MT_FLFMT("VIPKL_create_srq_stat: Incorrect srq_ul_resources_sz "
                              "(got "SIZE_T_DFMT" B while expects "SIZE_T_DFMT" B)."),
                     pi->srq_ul_resources_sz, srq_ul_resources_sz);
      		W_RET = VIP_EINVAL_PARAM;
      		goto exit;
  	}
  	/* fix the parameters */
  	memcpy( (void*)&po->srq_ul_resources[0], (void*)&pi->srq_ul_resources[0], 
            pi->srq_ul_resources_sz ); 
  	
    /* perform the function */
    W_RET = VIPKL_create_srq(RSCT_HNDL,pi->hca_hndl,pi->vapi_srq_hndl,pi->pd_hndl,
                             pi->async_hndl_ctx, srq_ul_resources_sz, &po->srq_ul_resources[0], 
                             &po->srq_hndl );
  	if (W_RET == VIP_OK) {
  		*ret_po_sz_p = (u_int32_t)(FIELD_OFFSET(struct o_VIPKL_create_srq_ops_t, srq_ul_resources) + 
        pi->srq_ul_resources_sz);  		
	}
exit:
VIPKL_RSCT_DEC
exit1: 
W_FUNC_END_RC_SZ


/******************************************************************************
 *  Function: VIPKL_destroy_srq <==> SRQM_destroy_srq
 *****************************************************************************/
/*
struct i_VIPKL_destroy_srq_ops_t {
                  VIP_hca_hndl_t hca_hndl;
                  SRQM_srq_hndl_t srq_hndl;
};

struct o_VIPKL_destroy_srq_ops_t {
  VIP_ret_t ret;
};
*/
W_FUNC_START(VIPKL_destroy_srq)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_destroy_srq(RSCT_HNDL, pi->hca_hndl, pi->srq_hndl);
    VIPKL_RSCT_DEC
exit1: 
W_FUNC_END


/******************************************************************************
 *  Function: VIPKL_query_srq <==> SRQM_query_srq
 *****************************************************************************/
/*
struct i_VIPKL_query_srq_ops_t {
                VIP_hca_hndl_t hca_hndl;
                SRQM_srq_hndl_t srq_hndl;
}
                
struct o_VIPKL_query_srq_ops_t {
                VIP_ret_t ret;
                u_int32_t limit;
};
*/
VIP_ret_t
VIPKL_query_srq(VIP_RSCT_t usr_ctx,
                VIP_hca_hndl_t hca_hndl,
                SRQM_srq_hndl_t srq_hndl,
                u_int32_t *limit_p);
W_FUNC_START(VIPKL_query_srq)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_query_srq(RSCT_HNDL, pi->hca_hndl, pi->srq_hndl, &po->limit);
    VIPKL_RSCT_DEC
exit1: 
W_FUNC_END_RC_SZ


/*****************************************
  struct i_VIPKL_attach_to_multicast_ops_t {
     IN	VAPI_hca_hndl_t     hca_hndl;
     IN IB_gid_t            mcg_dgid;
     IN VAPI_qp_hndl_t      qp_hndl;
  };
  struct o_VIPKL_attach_to_multicast_ops_t {
	OUT VIP_ret_t ret;
  };
******************************************/
W_FUNC_START(VIPKL_attach_to_multicast)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_attach_to_multicast(RSCT_HNDL, pi->hca_hndl, pi->mcg_dgid, pi->qp_hndl );
    VIPKL_RSCT_DEC
exit1: W_FUNC_END

/*****************************************
  struct i_VIPKL_detach_from_multicast_ops_t 
     IN      VAPI_hca_hndl_t     hca_hndl;
     IN      IB_gid_t            mcg_dgid;
     IN      VAPI_qp_hndl_t      qp_hndl;
  };
  struct o_VIPKL_detach_from_multicast_ops_t {
	OUT VIP_ret_t ret;
  };
******************************************/
W_FUNC_START(VIPKL_detach_from_multicast)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_detach_from_multicast(RSCT_HNDL, pi->hca_hndl, pi->mcg_dgid, pi->qp_hndl );
    VIPKL_RSCT_DEC
exit1: W_FUNC_END


/*****************************************
struct i_VIPKL_create_cq_ops_t {
  IN VIP_hca_hndl_t hca_hndl;
  IN VAPI_cq_hndl_t vapi_cq_hndl;
  IN MOSAL_protection_ctx_t  usr_prot_ctx;
  IN EM_async_ctx_hndl_t async_hndl_ctx,
  IN MT_size_t cq_ul_resources_sz;
  IN OUT unsigned long 	cq_ul_resources[1];
};
struct o_VIPKL_create_cq_ops_t {
  OUT VIP_ret_t ret;
  OUT CQM_cq_hndl_t cq;
  OUT HH_cq_hndl_t	cq_id;
  IN OUT unsigned long 	cq_ul_resources[1];
};
******************************************/
W_FUNC_START_WO_PRINT(VIPKL_create_cq)
	MT_size_t         cq_ul_resources_sz;
  	HH_hca_hndl_t     hh_hndl;
	W_FUNC_NAME(VIPKL_create_cq);
	
  W_IPARAM_SZ_CHECK_ULRES(VIPKL_create_cq, cq_ul_resources);
  W_OPARAM_SZ_CHECK_ULRES(VIPKL_create_cq, cq_ul_resources);
	
  W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    /* check the the async_hndl_ctx is the same as the one assigned to this hca_hndl */
    W_RET = VIPKL_RSCT_check_async_hndl_ctx(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl], 
                                            pi->async_hndl_ctx);
    if (W_RET != VIP_OK) {
      goto exit;  
    }

    /* get size of qp ul resource buffer, and copy info from user space */
	W_RET = VIPKL_get_hh_hndl(pi->hca_hndl, &hh_hndl);
	if (W_RET != VIP_OK) {
	    	MTL_ERROR2("VIPKL_create_cq_stat: Cannot get HH handle.\n");
      		goto exit;
	}
	cq_ul_resources_sz = hh_hndl->cq_ul_resources_sz;
  	/* check the size */
  	if (pi->cq_ul_resources_sz != cq_ul_resources_sz) {
	      	MTL_ERROR2("VIPKL_create_cq_stat: Incorrect cq_ul_resources size.\n");
	      	W_RET = VIP_EINVAL_PARAM;
      		goto exit;
  	}
  	/* fix the parameters */
  	memcpy( (void*)&po->cq_ul_resources[0], (void*)&pi->cq_ul_resources[0], pi->cq_ul_resources_sz ); 
  	/* when running through the kernel wrapper, ignore the prot_ctx parameter passed in. */
  	/* Get the true protection context parameter here, and use that in the call to create_pd*/
  	pi->usr_prot_ctx = MOSAL_get_current_prot_ctx();
  	
	/* perform the function */
	W_RET = VIPKL_create_cq(RSCT_HNDL, pi->hca_hndl,pi->vapi_cq_hndl,pi->usr_prot_ctx, 
                            pi->async_hndl_ctx, pi->cq_ul_resources_sz, 
		                    &po->cq_ul_resources[0], &po->cq, &po->cq_id );
    if (W_RET == VIP_OK) {
  		*ret_po_sz_p = (u_int32_t)(FIELD_OFFSET(struct o_VIPKL_create_cq_ops_t, cq_ul_resources) + pi->cq_ul_resources_sz);  		
	}
exit:
VIPKL_RSCT_DEC
exit1: W_FUNC_END_RC_SZ

/*****************************************
  struct i_VIPKL_destroy_cq_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN CQM_cq_hndl_t cq;
  };
  struct o_VIPKL_destroy_cq_ops_t {
    OUT VIP_ret_t ret;
  };
******************************************/
W_FUNC_START(VIPKL_destroy_cq)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_destroy_cq(RSCT_HNDL, pi->hca_hndl, pi->cq, FALSE );
    VIPKL_RSCT_DEC
exit1: W_FUNC_END

/*****************************************
  struct i_VIPKL_get_cq_props_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN CQM_cq_hndl_t cq;
  };
  struct o_VIPKL_get_cq_props_ops_t {
    OUT VIP_ret_t ret;
    OUT HH_cq_hndl_t	cq_id;
    OUT VAPI_cqe_num_t	num_o_entries;
  };
******************************************/
W_FUNC_START(VIPKL_get_cq_props)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_get_cq_props(RSCT_HNDL,pi->hca_hndl, pi->cq, &po->cq_id, &po->num_o_entries );
    VIPKL_RSCT_DEC
exit1: W_FUNC_END



/*****************************************
  struct i_VIPKL_resize_cq_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN CQM_cq_hndl_t cq;
    IN MT_size_t cq_ul_resources_sz;
    IN OUT unsigned long cq_ul_resources[1];
  };
  struct o_VIPKL_resize_cq_ops_t {
    OUT VIP_ret_t ret;
    IN OUT unsigned long cq_ul_resources[1];
  };
******************************************/
W_FUNC_START_WO_PRINT(VIPKL_resize_cq)
  HH_hca_hndl_t     hh_hndl;
	W_FUNC_NAME(VIPKL_resize_cq);
	
  W_IPARAM_SZ_CHECK_ULRES(VIPKL_resize_cq, cq_ul_resources);
  W_OPARAM_SZ_CHECK_ULRES(VIPKL_resize_cq, cq_ul_resources);
  
  W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
  if (W_RET != VIP_OK) {
    goto exit1;  
  }

    /* verify size of given cq_ul_resources */
	W_RET = VIPKL_get_hh_hndl(pi->hca_hndl, &hh_hndl);
	if (W_RET != VIP_OK) {
	    	MTL_ERROR2("VIPKL_resize_cq_stat: Cannot get HH handle.\n");
      		goto exit;
	}
	
	/* check the size */
 	if (pi->cq_ul_resources_sz != hh_hndl->cq_ul_resources_sz) {
   	MTL_ERROR2("VIPKL_create_cq_stat: Incorrect cq_ul_resources size.\n");
	  W_RET = VIP_EINVAL_PARAM;
  	goto exit;
  }
  /* fix the In/OUT parameters */
  memcpy( (void*)&po->cq_ul_resources[0], (void*)&pi->cq_ul_resources[0], pi->cq_ul_resources_sz ); 
  	
	/* perform the function */
	W_RET = VIPKL_resize_cq(RSCT_HNDL, pi->hca_hndl,pi->cq,
                          pi->cq_ul_resources_sz,&po->cq_ul_resources[0]);
  if (W_RET == VIP_OK) {
  	*ret_po_sz_p = (u_int32_t)(FIELD_OFFSET(struct o_VIPKL_create_cq_ops_t, cq_ul_resources) + pi->cq_ul_resources_sz);  		
	}
  
exit:
    VIPKL_RSCT_DEC 
exit1:
    W_FUNC_END_RC_SZ

/*****************************************
  struct i_VIPKL_create_mr_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN VAPI_mrw_t mrw_req;
    IN PDM_pd_hndl_t pd_hndl;
    IN VAPI_phy_buf_t pbuf_list[1];
  };
  struct o_VIPKL_create_mr_ops_t {
    OUT VIP_ret_t ret;
    OUT MM_mrw_hndl_t mrw_hndl;
    OUT MM_VAPI_mro_t mr_prop;
  };
******************************************/
W_FUNC_START_WO_PARAM_SZ_CHECK(VIPKL_create_mr)

    if (pi->mrw_req.type == VAPI_MPR) {
      W_RET = VAPI_EPERM; 
      MTL_ERROR1(MT_FLFMT("%s[PID="MT_PID_FMT
                          "]: Physical memory registration is not allowed from user space"), 
                 __func__, MOSAL_getpid());
      goto exit1;
    } else {
      W_IPARAM_SZ_CHECK_VARSIZE_BASE(VIPKL_create_mr, pbuf_list);
    }
    W_OPARAM_SZ_CHECK_DEFAULT(VIPKL_create_mr);

    MTL_DEBUG1("VIPKL_create_mr_stat: acl:0x%x start:"U64_FMT" size:"U64_FMT" pd hndl:0x%x\n",
             pi->mrw_req.acl,pi->mrw_req.start,pi->mrw_req.size,pi->pd_hndl);
    W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_create_mr(RSCT_HNDL,pi->hca_hndl,&pi->mrw_req ,pi->pd_hndl,&po->mrw_hndl,&po->mr_prop );
    VIPKL_RSCT_DEC
exit1: W_FUNC_END_RC_SZ

/*****************************************
  struct i_VIPKL_create_smr_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN MM_mrw_hndl_t     orig_mr_hndl;
    IN VAPI_mrw_t     mrw_req;
    IN PDM_pd_hndl_t pd_hndl;
  };
  struct o_VIPKL_create_smr_ops_t {
    OUT VIP_ret_t ret;
    OUT MM_mrw_hndl_t mrw_hndl;
    OUT MM_VAPI_mro_t mr_prop;
  };
 ******************************************/
W_FUNC_START(VIPKL_create_smr)
	MTL_DEBUG1("VIPKL_create_smr_stat: acl:0x%x start:"U64_FMT" pd hndl:0x%x\n",pi->mrw_req.acl,
       	pi->mrw_req.start,pi->pd_hndl);
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_create_smr(RSCT_HNDL,pi->hca_hndl,pi->orig_mr_hndl, &pi->mrw_req ,pi->pd_hndl, &po->mrw_hndl,&po->mr_prop );
    VIPKL_RSCT_DEC
exit1: W_FUNC_END_RC_SZ

/*****************************************
struct i_VIPKL_reregister_mr_ops_t {
    IN  VAPI_hca_hndl_t       hca_hndl;
    IN  VAPI_mr_hndl_t        mr_hndl;
    IN  VAPI_mr_change_t      change_type;
    IN  VAPI_mrw_t           mrw_req;
    IN PDM_pd_hndl_t       pd_hndl;
    IN VAPI_phy_buf_t pbuf_list[1];
};
struct o_VIPKL_reregister_mr_ops_t {
    OUT VIP_ret_t ret;
    OUT  MM_mrw_hndl_t       rep_mr_hndl;
    OUT MM_VAPI_mro_t        rep_mrw;
};
******************************************/
W_FUNC_START_WO_PARAM_SZ_CHECK(VIPKL_reregister_mr)
    if (pi->mrw_req.type == VAPI_MPR) {
      W_RET = VAPI_EPERM; 
      MTL_ERROR1(MT_FLFMT("%s[PID="MT_PID_FMT
                          "]: Physical memory registration is not allowed from user space"), 
                 __func__, MOSAL_getpid());
      goto exit1;
    } else {
      W_IPARAM_SZ_CHECK_VARSIZE_BASE(VIPKL_reregister_mr, pbuf_list);
    }
    W_OPARAM_SZ_CHECK_DEFAULT(VIPKL_reregister_mr);
    
    W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_reregister_mr(RSCT_HNDL,pi->hca_hndl, pi->mr_hndl, pi->change_type, &pi->mrw_req, pi->pd_hndl, 
	  	&po->rep_mr_hndl, &po->rep_mrw );
    VIPKL_RSCT_DEC
exit1: W_FUNC_END_RC_SZ
	
/*****************************************
  struct i_VIPKL_create_mw_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN PDM_pd_hndl_t pd_hndl;
  };
  struct o_VIPKL_create_mw_ops_t {
    OUT VIP_ret_t ret;
    OUT MM_key_t		r_key;
  };
******************************************/
W_FUNC_START(VIPKL_create_mw)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_create_mw(RSCT_HNDL,pi->hca_hndl, pi->pd_hndl, &po->r_key );
    VIPKL_RSCT_DEC
    
exit1: W_FUNC_END_RC_SZ


#if 0
/* FMRs are allowed in kernel only */

/*****************************************
struct i_VIPKL_alloc_fmr_ops_t {
    IN  VIP_hca_hndl_t      hca_hndl;
    IN  EVAPI_fmr_t         fmr_props;
    IN  PDM_pd_hndl_t       pd_hndl;
};
struct o_VIPKL_alloc_fmr_ops_t {
    OUT VIP_ret_t ret;
    OUT MM_mrw_hndl_t      fmr_hndl;
};
******************************************/
W_FUNC_START(VIPKL_alloc_fmr)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_alloc_fmr(RSCT_HNDL,pi->hca_hndl,&pi->fmr_props, pi->pd_hndl, &po->fmr_hndl );
    VIPKL_RSCT_DEC
exit1: W_FUNC_END_RC_SZ

/*****************************************
struct i_VIPKL_map_fmr_ops_t {
    IN   VAPI_hca_hndl_t      hca_hndl;
    IN   EVAPI_fmr_hndl_t       fmr_hndl;
    IN   EVAPI_fmr_map_t      map;
    IN   VAPI_phy_addr_t	page_array[1];
};
struct o_VIPKL_map_fmr_ops_t {
    OUT VIP_ret_t ret;
    OUT  VAPI_lkey_t          l_key; 
    OUT  VAPI_rkey_t          r_key;
};
******************************************/
W_FUNC_START_WO_PARAM_SZ_CHECK(VIPKL_map_fmr)
  /* Assure the map data is valid then verify given page_array size matches given len (FM #18007) */
  W_IPARAM_SZ_CHECK_TABLE(VIPKL_map_fmr, page_array, map.page_array_len);
  W_OPARAM_SZ_CHECK_DEFAULT(VIPKL_map_fmr);
  if (pi->map.page_array_len > 0) {
    /* fix pointer to page_array */
    pi->map.page_array = pi->page_array;
  }
  
  W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
  if (W_RET != VIP_OK) {
    goto exit1;  
  }

  W_RET = VIPKL_map_fmr(RSCT_HNDL,pi->hca_hndl,pi->fmr_hndl, &pi->map,&po->l_key, &po->r_key );
  VIPKL_RSCT_DEC
exit1: W_FUNC_END_RC_SZ

/*****************************************
struct i_VIPKL_unmap_fmr_ops_t {
    IN   VAPI_hca_hndl_t      hca_hndl;
    IN   MT_size_t            num_of_fmr_to_unmap;
    IN   EVAPI_fmr_hndl_t       fmr_hndl_array[1];
};
struct o_VIPKL_unmap_fmr_ops_t {
    OUT VIP_ret_t ret;
};
******************************************/
W_FUNC_START_WO_PARAM_SZ_CHECK(VIPKL_unmap_fmr)
 /* Assure the num_of_fmr_to_unmap data is valid then verify that
  * given fmr_hndl_array size matches given num_of_fmr_to_unmap (FM #18007)           */
  W_IPARAM_SZ_CHECK_TABLE(VIPKL_unmap_fmr, fmr_hndl_array, num_of_fmr_to_unmap);
  W_OPARAM_SZ_CHECK_DEFAULT(VIPKL_unmap_fmr);
  	
  W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_unmap_fmr(RSCT_HNDL,pi->hca_hndl,pi->num_of_fmr_to_unmap, pi->fmr_hndl_array);
    VIPKL_RSCT_DEC
exit1: W_FUNC_END
	
/*****************************************
struct i_VIPKL_free_fmr_ops_t {
    IN   VAPI_hca_hndl_t      hca_hndl;
    IN   EVAPI_fmr_hndl_t    fmr_hndl;
};
struct o_VIPKL_free_fmr_ops_t {
    OUT VIP_ret_t ret;
};
******************************************/
W_FUNC_START(VIPKL_free_fmr)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_free_fmr(RSCT_HNDL,pi->hca_hndl, pi->fmr_hndl);
    VIPKL_RSCT_DEC
exit1: W_FUNC_END

#endif /*FMRs*/


/*****************************************
  struct i_VIPKL_destroy_mr_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN MM_mrw_hndl_t mrw_hndl;
  };
struct o_VIPKL_destroy_mr_ops_t {
    OUT VIP_ret_t ret;
};
******************************************/
W_FUNC_START(VIPKL_destroy_mr)
  	MTL_DEBUG2("alloc ul : hca hndl: %x \n",pi->hca_hndl); 
    W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_destroy_mr(RSCT_HNDL,pi->hca_hndl, pi->mrw_hndl);
    VIPKL_RSCT_DEC
exit1: W_FUNC_END

/*****************************************
  struct i_VIPKL_destroy_mw_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN MM_mrw_hndl_t mrw_hndl;
  };
struct o_VIPKL_destroy_mw_ops_t {
    OUT VIP_ret_t ret;
};
******************************************/
W_FUNC_START(VIPKL_destroy_mw)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_destroy_mw(RSCT_HNDL,pi->hca_hndl, pi->mrw_hndl);
    VIPKL_RSCT_DEC
exit1: W_FUNC_END

/*****************************************
  struct i_VIPKL_query_mr_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN MM_mrw_hndl_t mrw_hndl;
  };
  struct o_VIPKL_query_mr_ops_t {
    OUT VIP_ret_t ret;
    OUT MM_VAPI_mro_t mr_prop;
  };
*******************************************/
W_FUNC_START(VIPKL_query_mr)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_query_mr(RSCT_HNDL,pi->hca_hndl, pi->mrw_hndl, &po->mr_prop );
    VIPKL_RSCT_DEC
exit1: W_FUNC_END_RC_SZ


/*****************************************
 struct i_VIPKL_query_mw_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN IB_rkey_t initial_key;
  };
  struct o_VIPKL_query_mw_ops_t {
    OUT VIP_ret_t ret;
    OUT VAPI_rkey_t current_key;
  };
******************************************/
W_FUNC_START(VIPKL_query_mw)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_query_mw(RSCT_HNDL,pi->hca_hndl,pi->initial_key,&po->current_key);
    VIPKL_RSCT_DEC
exit1: W_FUNC_END_RC_SZ



/*************************************************************************
 * VIPKL_EQ functions 
 *************************************************************************/ 

/***************************************************
struct i_VIPKL_EQ_new_proc_ops_t {
  IN VAPI_hca_hndl_t hca;
  IN VIP_EQ_cbk_type_t cbk_type,
  IN EM_async_ctx_hndl_t async_ctx
};
struct o_VIPKL_EQ_new_proc_ops_t {
  OUT VIP_ret_t ret;
  OUT VIPKL_EQ_hndl_t vipkl_eq_h;
};
****************************************************/
W_FUNC_START(VIPKL_EQ_new)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    /* check the the async_hndl_ctx is the same as the one assigned to this hca_hndl */
    W_RET = VIPKL_RSCT_check_async_hndl_ctx(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl], 
                                            pi->async_ctx);
    if (W_RET != VIP_OK) {
      goto exit;  
    }

    W_RET = VIPKL_EQ_new(RSCT_HNDL,pi->hca_hndl, pi->cbk_type, pi->async_ctx, &po->vipkl_eq_h );

  exit:  VIPKL_RSCT_DEC
  exit1: W_FUNC_END_RC_SZ

/***************************************************
struct i_VIPKL_EQ_del_proc_ops_t {
  IN  VIPKL_EQ_hndl_t vipkl_eq;
};
struct o_VIPKL_EQ_del_proc_ops_t {
  OUT VIP_ret_t ret;
};
****************************************************/
W_FUNC_START(VIPKL_EQ_del)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_EQ_del(RSCT_HNDL,pi->hca_hndl,pi->vipkl_eq );
    VIPKL_RSCT_DEC
exit1: W_FUNC_END

/***************************************************
struct i_VIPKL_EQ_evapi_set_comp_eventh_ops_t {
  IN  VIPKL_EQ_hndl_t                  vipkl_eq;
  IN  CQM_cq_hndl_t                    vipkl_cq;
  IN  VAPI_completion_event_handler_t  completion_handler;
  IN  void *                           private_data;
};
struct o_VIPKL_EQ_evapi_set_comp_eventh_ops_t {
  OUT VIP_ret_t ret;
};
****************************************************/
W_FUNC_START(VIPKL_EQ_evapi_set_comp_eventh)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_EQ_evapi_set_comp_eventh(RSCT_HNDL,pi->hca_hndl,pi->vipkl_eq, pi->vipkl_cq, pi->completion_handler, pi->private_data );
    VIPKL_RSCT_DEC
    exit1:        
    W_FUNC_END_RC_SZ

	
/****************************************************
struct i_VIPKL_EQ_evapi_clear_comp_eventh_ops_t {
  IN  VIPKL_EQ_hndl_t                  vipkl_eq;
  IN  CQM_cq_hndl_t                    vipkl_cq;
};
struct o_VIPKL_EQ_evapi_clear_comp_eventh_ops_t {
  OUT VIP_ret_t ret;
};
****************************************************/
W_FUNC_START(VIPKL_EQ_evapi_clear_comp_eventh)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_EQ_evapi_clear_comp_eventh(RSCT_HNDL,pi->hca_hndl,pi->vipkl_eq, pi->vipkl_cq );
    
    VIPKL_RSCT_DEC
    exit1:        
    W_FUNC_END_RC_SZ

/***************************************************
struct i_VIPKL_EQ_set_async_event_handler_ops_t {
  IN VIP_RSCT_t usr_ctx;
  IN VAPI_hca_hndl_t hca;
  IN VIPKL_EQ_hndl_t                  vipkl_eq;
  IN VAPI_async_event_handler_t       async_eventh;
  IN void *                           private_data;
};
struct o_VIPKL_EQ_set_async_event_handler_ops_t {
  OUT VIP_ret_t ret;
  OUT EVAPI_async_handler_hndl_t      async_handler_hndl;
};
****************************************************/
W_FUNC_START(VIPKL_EQ_set_async_event_handler)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_EQ_set_async_event_handler(RSCT_HNDL,pi->hca_hndl,pi->vipkl_eq, 
                                             pi->async_eventh, pi->private_data, &po->async_handler_hndl);
    
    VIPKL_RSCT_DEC
    exit1:        
    W_FUNC_END_RC_SZ

/***************************************************
struct i_VIPKL_EQ_clear_async_event_handler_ops_t {
  IN VIP_RSCT_t usr_ctx;
  IN VAPI_hca_hndl_t hca;
  IN VIPKL_EQ_hndl_t                  vipkl_eq;
  IN EVAPI_async_handler_hndl_t       async_handler_hndl;
}
struct o_VIPKL_EQ_clear_async_event_handler_ops_t {
  OUT VIP_ret_t ret;
};
****************************************************/
W_FUNC_START(VIPKL_EQ_clear_async_event_handler)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_EQ_clear_async_event_handler(RSCT_HNDL,pi->hca_hndl,pi->vipkl_eq, pi->async_handler_hndl);
    
    VIPKL_RSCT_DEC
    exit1:        
    W_FUNC_END_RC_SZ



/***************************************************
struct i_VIPKL_EQ_poll_ops_t {
  IN  VAPI_hca_hndl_t hca_hndl;
  IN  VIPKL_EQ_hndl_t vipkl_eq;
};
struct o_VIPKL_EQ_poll_ops_t {
  OUT VIP_ret_t ret;
  OUT VIPKL_EQ_event_t eqe;
};
****************************************************/
W_FUNC_START(VIPKL_EQ_poll)
    W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }
      W_RET = VIPKL_EQ_poll(RSCT_HNDL,pi->hca_hndl,pi->vipkl_eq, &po->eqe);
    
    VIPKL_RSCT_DEC
        exit1:        
        W_FUNC_END_RC_SZ


	
/*************************************************************************
 * VIPKL_cqblk functions 
 *************************************************************************/ 

/***************************************************
struct i_VIPKL_cqblk_alloc_ctx_ops_t {
  IN	VIP_hca_hndl_t   vip_hca;
  IN	CQM_cq_hndl_t    vipkl_cq;
};
struct o_VIPKL_cqblk_alloc_ctx_ops_t {
  OUT VIP_ret_t ret;
  OUT VIPKL_cqblk_hndl_t cqblk_hndl;
};
****************************************************/
W_FUNC_START(VIPKL_cqblk_alloc_ctx)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    MTL_DEBUG1("before VIPKL_cqblkkkkkkkkkkkkkkkkk \n");
    W_RET = VIPKL_cqblk_alloc_ctx(RSCT_HNDL,pi->hca_hndl,pi->vipkl_cq, &po->cqblk_hndl );
    VIPKL_RSCT_DEC
    exit1:        
        W_FUNC_END_RC_SZ


/***************************************************
struct i_VIPKL_cqblk_free_ctx_ops_t {
  IN	VIPKL_cqblk_hndl_t cqblk_hndl;
};
struct o_VIPKL_cqblk_free_ctx_ops_t {
  OUT VIP_ret_t ret;
};
****************************************************/
W_FUNC_START(VIPKL_cqblk_free_ctx)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_cqblk_free_ctx(RSCT_HNDL,pi->hca_hndl,pi->cqblk_hndl );
    VIPKL_RSCT_DEC
    exit1:        
        W_FUNC_END_RC_SZ


/***************************************************
struct i_VIPKL_cqblk_wait_ops_t {
  IN	VIPKL_cqblk_hndl_t cqblk_hndl;
  IN	MT_size_t timeout_usec;
};
struct o_VIPKL_cqblk_wait_ops_t {
  OUT VIP_ret_t ret;
};
****************************************************/
W_FUNC_START(VIPKL_cqblk_wait)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_cqblk_wait(RSCT_HNDL,pi->hca_hndl,pi->cqblk_hndl, pi->timeout_usec );
    VIPKL_RSCT_DEC
exit1:        
    W_FUNC_END_RC_SZ


/***************************************************
struct i_VIPKL_cqblk_signal_ops_t {
  IN	VIPKL_cqblk_hndl_t cqblk_hndl;
};
struct o_VIPKL_cqblk_signal_ops_t {
  OUT VIP_ret_t ret;
};
****************************************************/
W_FUNC_START(VIPKL_cqblk_signal)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_cqblk_signal(RSCT_HNDL,pi->hca_hndl,pi->cqblk_hndl );
    VIPKL_RSCT_DEC
exit1:        
    W_FUNC_END_RC_SZ

/***************************************************
struct i_VIPKL_process_local_mad_ops_t {
  IN    VIP_hca_hndl_t hca_hndl;
  IN    IB_port_t      port_num;
  IN    IB_lid_t       slid,
  IN	  EVAPI_proc_mad_opt_t	 proc_mad_opts;
  IN    u_int8_t       mad_in[IB_MAD_LEN];
};
struct o_VIPKL_process_local_mad_ops_t {
  OUT VIP_ret_t ret;
  OUT   u_int8_t       mad_out[IB_MAD_LEN];
};
****************************************************/
W_FUNC_START(VIPKL_process_local_mad)
  	W_RET = VIPKL_RSCT_check(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_process_local_mad(pi->hca_hndl, pi->port_num, pi->slid, pi->proc_mad_opts,
  		&pi->mad_in[0], &po->mad_out[0] );
exit1: W_FUNC_END_RC_SZ

/***************************************************
struct i_VIPKL_alloc_devmem_ops_t {
  IN   VIP_hca_hndl_t 	hca_hndl;
  IN   EVAPI_devmem_type_t mem_type;
  IN   MT_size_t           bsize;
  IN   u_int8_t            align_shift;
};
struct o_VIPKL_alloc_devmem_ops_t {
  OUT VIP_ret_t ret;
  OUT VAPI_phy_addr_t     buf;
  OUT void*               virt_addr;
  OUT DEVMM_dm_hndl_t     dm_hndl;
};

****************************************************/
W_FUNC_START(VIPKL_alloc_map_devmem)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    if (!pi->map) {
        W_RET = VIPKL_alloc_map_devmem(RSCT_HNDL,pi->hca_hndl,pi->mem_type, pi->bsize, pi->align_shift,
                                    &po->buf,NULL,&po->dm_hndl);
    }else{
        W_RET = VIPKL_alloc_map_devmem(RSCT_HNDL,pi->hca_hndl,pi->mem_type, pi->bsize, pi->align_shift,
                                    &po->buf,&po->virt_addr,&po->dm_hndl);
    }
    VIPKL_RSCT_DEC
exit1: W_FUNC_END_RC_SZ
	
/***************************************************
struct i_VIPKL_query_devmem_ops_t {
  IN   VIP_hca_hndl_t 	hca_hndl;
  IN   EVAPI_devmem_type_t mem_type;
};
struct o_VIPKL_query_devmem_ops_t {
  OUT VIP_ret_t ret;
  OUT  EVAPI_devmem_info_t  devmem_info;
};

****************************************************/
W_FUNC_START(VIPKL_query_devmem)
  	W_RET = VIPKL_RSCT_check(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_query_devmem(RSCT_HNDL,pi->hca_hndl,pi->mem_type,pi->align_shift,&po->devmem_info);
exit1: W_FUNC_END_RC_SZ

/***************************************************
struct i_VIPKL_free_unmap_devmem_ops_t {
  IN   VIP_hca_hndl_t 	hca_hndl;
  };
struct o_VIPKL_free_unmap_devmem_ops_t {
  OUT VIP_ret_t ret;
 };

****************************************************/
W_FUNC_START(VIPKL_free_unmap_devmem)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_free_unmap_devmem(RSCT_HNDL,pi->hca_hndl,pi->dm_hndl);
    VIPKL_RSCT_DEC    
exit1: W_FUNC_END
	
#if defined(MT_SUSPEND_QP)
/*****************************************
    struct i_VIPKL_suspend_qp_ops_t {
      IN VIP_hca_hndl_t 		hca_hndl;
      IN QPM_qp_hndl_t 		qp_hndl;
      IN MT_bool              suspend_flag;
    };
    struct o_VIPKL_suspend_qp_ops_t {
      OUT VIP_ret_t ret;
    };
******************************************/
W_FUNC_START(VIPKL_suspend_qp)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_suspend_qp(RSCT_HNDL,pi->hca_hndl, pi->qp_hndl, pi->suspend_flag);
    VIPKL_RSCT_DEC
exit1: 
W_FUNC_END

/*****************************************
    struct i_VIPKL_suspend_cq_ops_t {
      IN VIP_hca_hndl_t 	 hca_hndl;
      IN CQM_cq_hndl_t 		 cq_hndl;
      IN MT_bool             do_suspend;
    };
    struct o_VIPKL_suspend_qp_ops_t {
      OUT VIP_ret_t ret;
    };
******************************************/
W_FUNC_START(VIPKL_suspend_cq)
  	W_RET = VIPKL_RSCT_check_inc(pi->hca_hndl,&hca_rsct_arr[pi->hca_hndl]);
    if (W_RET != VIP_OK) {
      goto exit1;  
    }

    W_RET = VIPKL_suspend_cq(RSCT_HNDL,pi->hca_hndl, pi->cq_hndl, pi->do_suspend);
    VIPKL_RSCT_DEC
exit1: 
W_FUNC_END
#endif

/*************************************************************************
 * Implement system call by calling appropriate static function 
 *************************************************************************/
#define VIPKL_INVOKE_STAT_FUNC(func) ret= func##_stat(rsct_arr,(struct i_##func##_ops_t*)pi,pi_sz,(struct o_##func##_ops_t*)po,po_sz,ret_po_sz_p)

VIP_ret_t VIPKL_ioctl(VIPKL_ops_t ops, VIP_hca_state_t* rsct_arr, void *pi, u_int32_t pi_sz, void *po, u_int32_t po_sz, u_int32_t* ret_po_sz_p )
{
  VIP_ret_t ret = VIP_OK;;
  MTL_TRACE1("CALLED VIPKL_ioctl (ops %d, pi_sz %d, po_sz %d)\n", ops,  pi_sz, po_sz);
  *ret_po_sz_p = po_sz;
  switch (ops) {
    case VIPKL_OPEN_HCA:  VIPKL_INVOKE_STAT_FUNC(VIPKL_open_hca); break;
    case VIPKL_CLOSE_HCA:  VIPKL_INVOKE_STAT_FUNC(VIPKL_close_hca); break;
    case VIPKL_GET_HCA_HNDL:  VIPKL_INVOKE_STAT_FUNC(VIPKL_get_hca_hndl); break;
    case VIPKL_LIST_HCAS:  VIPKL_INVOKE_STAT_FUNC(VIPKL_list_hcas); break;
    
    case VIPKL_ALLOC_UL_RESOURCES:  VIPKL_INVOKE_STAT_FUNC(VIPKL_alloc_ul_resources); break;
    case VIPKL_FREE_UL_RESOURCES:  VIPKL_INVOKE_STAT_FUNC(VIPKL_free_ul_resources); break;
    
    case VIPKL_QUERY_HCA_CAP:  VIPKL_INVOKE_STAT_FUNC(VIPKL_query_hca_cap); break;
    case VIPKL_QUERY_HCA_PORT_PROP:  VIPKL_INVOKE_STAT_FUNC(VIPKL_query_port_prop); break;
    case VIPKL_QUERY_HCA_PKEY_TBL:  VIPKL_INVOKE_STAT_FUNC(VIPKL_query_port_pkey_tbl); break;
    case VIPKL_QUERY_HCA_GID_TBL:  VIPKL_INVOKE_STAT_FUNC(VIPKL_query_port_gid_tbl); break;
    case VIPKL_MODIFY_HCA_ATTR:  VIPKL_INVOKE_STAT_FUNC(VIPKL_modify_hca_attr); break;
    
    case VIPKL_GET_HH_HNDL:  VIPKL_INVOKE_STAT_FUNC(VIPKL_get_hh_hndl); break;
    case VIPKL_GET_HCA_UL_INFO:  VIPKL_INVOKE_STAT_FUNC(VIPKL_get_hca_ul_info); break;
    case VIPKL_GET_HCA_ID:  VIPKL_INVOKE_STAT_FUNC(VIPKL_get_hca_id); break;

    case VIPKL_ALLOC_PD:  VIPKL_INVOKE_STAT_FUNC(VIPKL_create_pd); break;
    case VIPKL_DEALLOC_PD:  VIPKL_INVOKE_STAT_FUNC(VIPKL_destroy_pd); break;

    case VIPKL_CREATE_QP:  VIPKL_INVOKE_STAT_FUNC(VIPKL_create_qp); break;
    case VIPKL_MODIFY_QP:  VIPKL_INVOKE_STAT_FUNC(VIPKL_modify_qp); break;
    case VIPKL_QUERY_QP:  VIPKL_INVOKE_STAT_FUNC(VIPKL_query_qp); break;
    case VIPKL_DESTROY_QP:  VIPKL_INVOKE_STAT_FUNC(VIPKL_destroy_qp); break;
    case VIPKL_GET_SPECIAL_QP:  VIPKL_INVOKE_STAT_FUNC(VIPKL_get_special_qp); break;
    case VIPKL_ATTACH_TO_MULTICAST:  VIPKL_INVOKE_STAT_FUNC(VIPKL_attach_to_multicast); break;
    case VIPKL_DETACH_FROM_MULTICAST:  VIPKL_INVOKE_STAT_FUNC(VIPKL_detach_from_multicast); break;

    case VIPKL_CREATE_SRQ:     VIPKL_INVOKE_STAT_FUNC(VIPKL_create_srq) ; break;
    case VIPKL_DESTROY_SRQ:    VIPKL_INVOKE_STAT_FUNC(VIPKL_destroy_srq); break;
    case VIPKL_QUERY_SRQ:      VIPKL_INVOKE_STAT_FUNC(VIPKL_query_srq)  ; break;
    
    case VIPKL_CREATE_CQ:  VIPKL_INVOKE_STAT_FUNC(VIPKL_create_cq); break;
    case VIPKL_QUERY_CQ:  VIPKL_INVOKE_STAT_FUNC(VIPKL_get_cq_props); break;
    case VIPKL_RESIZE_CQ:  VIPKL_INVOKE_STAT_FUNC(VIPKL_resize_cq); break;
    case VIPKL_DESTROY_CQ:  VIPKL_INVOKE_STAT_FUNC(VIPKL_destroy_cq); break;
  /*  case VIPKL_GET_CQ_BUF_SZ:  VIPKL_query_cq_memory_size_stat(pi,po,ret_po_sz_p); break; */

    case VIPKL_CREATE_MR:  VIPKL_INVOKE_STAT_FUNC(VIPKL_create_mr); break;
    case VIPKL_CREATE_SMR:  VIPKL_INVOKE_STAT_FUNC(VIPKL_create_smr); break;
    case VIPKL_REREGISTER_MR:  VIPKL_INVOKE_STAT_FUNC(VIPKL_reregister_mr); break;
    case VIPKL_DESTROY_MR:  VIPKL_INVOKE_STAT_FUNC(VIPKL_destroy_mr); break;
    case VIPKL_QUERY_MR:  VIPKL_INVOKE_STAT_FUNC(VIPKL_query_mr); break;
    
    case VIPKL_CREATE_MW:  VIPKL_INVOKE_STAT_FUNC(VIPKL_create_mw); break;
    case VIPKL_DESTROY_MW:  VIPKL_INVOKE_STAT_FUNC(VIPKL_destroy_mw); break;
    case VIPKL_QUERY_MW:  VIPKL_INVOKE_STAT_FUNC(VIPKL_query_mw); break;
    
#if 0 /* FMRs allowed in kernel only */
    case VIPKL_ALLOC_FMR:  VIPKL_INVOKE_STAT_FUNC(VIPKL_alloc_fmr); break;
    case VIPKL_MAP_FMR:  VIPKL_INVOKE_STAT_FUNC(VIPKL_map_fmr); break;
    case VIPKL_UNMAP_FMR:  VIPKL_INVOKE_STAT_FUNC(VIPKL_unmap_fmr); break;
    case VIPKL_FREE_FMR:  VIPKL_INVOKE_STAT_FUNC(VIPKL_free_fmr); break;
#endif
    
    case VIPKL_EQ_NEW:  VIPKL_INVOKE_STAT_FUNC(VIPKL_EQ_new); break;
    case VIPKL_EQ_DEL:  VIPKL_INVOKE_STAT_FUNC(VIPKL_EQ_del); break;
    case VIPKL_EQ_EVAPI_SET_COMP_EVENTH:  
      VIPKL_INVOKE_STAT_FUNC(VIPKL_EQ_evapi_set_comp_eventh); break;
    case VIPKL_EQ_EVAPI_CLEAR_COMP_EVENTH:  
      VIPKL_INVOKE_STAT_FUNC(VIPKL_EQ_evapi_clear_comp_eventh); break;
    case VIPKL_EQ_SET_ASYNC_EVENT_HANDLER:  
      VIPKL_INVOKE_STAT_FUNC(VIPKL_EQ_set_async_event_handler); break;
    case VIPKL_EQ_CLEAR_ASYNC_EVENT_HANDLER:
      VIPKL_INVOKE_STAT_FUNC(VIPKL_EQ_clear_async_event_handler); break;
    case VIPKL_EQ_POLL:  VIPKL_INVOKE_STAT_FUNC(VIPKL_EQ_poll); break;
    
    case VIPKL_CQBLK_ALLOC_CTX:   VIPKL_INVOKE_STAT_FUNC(VIPKL_cqblk_alloc_ctx); break;
    case VIPKL_CQBLK_FREE_CTX:    VIPKL_INVOKE_STAT_FUNC(VIPKL_cqblk_free_ctx); break;
    case VIPKL_CQBLK_WAIT:        VIPKL_INVOKE_STAT_FUNC(VIPKL_cqblk_wait); break;
    case VIPKL_CQBLK_SIGNAL:      VIPKL_INVOKE_STAT_FUNC(VIPKL_cqblk_signal); break;
    
    case VIPKL_PROCESS_LOCAL_MAD:  VIPKL_INVOKE_STAT_FUNC(VIPKL_process_local_mad); break;
    case VIPKL_ALLOC_MAP_DEVMEM:   VIPKL_INVOKE_STAT_FUNC(VIPKL_alloc_map_devmem); break;
    case VIPKL_QUERY_DEVMEM:  VIPKL_INVOKE_STAT_FUNC(VIPKL_query_devmem); break;
    case VIPKL_FREE_UNMAP_DEVMEM:  VIPKL_INVOKE_STAT_FUNC(VIPKL_free_unmap_devmem); break;
#if defined(MT_SUSPEND_QP)
    case VIPKL_SUSPEND_QP:         VIPKL_INVOKE_STAT_FUNC(VIPKL_suspend_qp); break;
    case VIPKL_SUSPEND_CQ:         VIPKL_INVOKE_STAT_FUNC(VIPKL_suspend_cq); break;
#endif
    default: ret = VIP_EINVAL_PARAM; break;
  }
  return ret;
}


/*  VIPKL_open
 *
 */ 
VIP_hca_state_t* VIPKL_open()
{
    int i;
    VIP_hca_state_t* hca_state_arr = (VIP_hca_state_t*)MALLOC(VIP_MAX_HCA * sizeof(VIP_hca_state_t));

    if (!hca_state_arr) {
        MTL_ERROR1("VIPKL open: failed allocating hca state object !!\n");
        return NULL;
    }
    
    for (i=0; i< VIP_MAX_HCA; i++) {
        hca_state_arr[i].rsct = NULL;
        hca_state_arr[i].state = VIP_HCA_STATE_EMPTY;
        MOSAL_spinlock_init(&hca_state_arr[i].lock);
    }

    
    MTL_DEBUG1("VIPKL_open \n");
    return hca_state_arr;
}

/*
 *  VIPKL_close
 */
void VIPKL_close(VIP_hca_state_t* hca_state_arr)
{
    int i;
    VIP_ret_t ret = VIP_OK;

    MTL_DEBUG1("-->VIPKL_close \n");
    for (i=0; i< VIP_MAX_HCA; i++) {
            ret = VIPKL_destroy_hca_resources(&hca_state_arr[i]);
            if (ret == VIP_EBUSY) {
                MTL_ERROR1("VIPKL close: FATAL ERROR: some resources are still alocated in kernel\n"); 
            }
    }
    FREE(hca_state_arr);
    MTL_DEBUG1("<--VIPKL_close \n");

}

void VIPKL_wrap_kernel_init(void)
{
  /* init_handlers_pool; - handler pool is not used anymore */
}

static VIP_ret_t VIPKL_destroy_hca_resources(VIP_hca_state_t* hca_p)
{
    VIP_ret_t ret = VIP_OK;
    
    MOSAL_spinlock_lock(&hca_p->lock);
    if ( hca_p->state == VIP_HCA_STATE_AVAIL) 
    {
        hca_p->state = VIP_HCA_STATE_DESTROY;
        MOSAL_spinlock_unlock(&hca_p->lock);
        VIP_RSCT_destroy(hca_p->rsct);    
        MOSAL_spinlock_lock(&hca_p->lock);
        hca_p->state = VIP_HCA_STATE_EMPTY;
        MOSAL_spinlock_unlock(&hca_p->lock);
    }else{
        if (hca_p->state == VIP_HCA_STATE_EMPTY) {
            ret= VIP_EINVAL_HCA_HNDL;
        }else {
           MTL_ERROR1("!! error: VIPKL_destroy_hca_resources: failed sanity check  \n");
           MTL_ERROR1("!! hca state: %d  \n",hca_p->state);
           ret= VIP_EBUSY;
        }
        MOSAL_spinlock_unlock(&hca_p->lock);
    }
           
    return ret;
}

