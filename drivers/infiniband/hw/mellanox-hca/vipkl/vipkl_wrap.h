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

#ifndef VIPKL_WRAP_H
#define VIPKL_WRAP_H

#include <vipkl.h>
#include <mosal.h>
#include <VIP_rsct.h>
/* OS-dependent stuff, if any */
#include <vipkl_sys.h>		


/*  make IOCTL */
#define VIPKL_IOCTL_CALL _IOR('x',1,void *)

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

#ifndef FIELD_OFFSET
#define FIELD_OFFSET(type, field)    ((long)(int *)&(((type *)0)->field))
#endif


typedef enum {  /* Function selector for VIPKL system call */
  /* For each selector there is a function in VIP(KL) which the wrapper calls */
                        /* The called function */
                        /***********************/
  VIPKL_OPEN_HCA=VAPI_FUNC_BASE,       /* VIPKL_open_hca */
  VIPKL_CLOSE_HCA,      /* VIPKL_close_hca */
  VIPKL_GET_HCA_HNDL,   /* VIPKL_get_hca_hndl */
  VIPKL_LIST_HCAS,      /* VIPKL_list_hcas */
  VIPKL_ALLOC_UL_RESOURCES, /* VIPKL_alloc_ul_resources */
  VIPKL_FREE_UL_RESOURCES, /* VIPKL_free_ul_resources */
  VIPKL_QUERY_HCA_CAP,  /* VIPKL_query_hca_cap */
  VIPKL_QUERY_HCA_PORT_PROP, /* VIPKL_query_port_prop */
  VIPKL_QUERY_HCA_PKEY_TBL, /* VIPKL_query_port_pkey_tbl */
  VIPKL_QUERY_HCA_GID_TBL, /* VIPKL_query_port_gid_tbl */
  VIPKL_MODIFY_HCA_ATTR,/* VIPKL_modify_hca_attr */
  VIPKL_GET_HH_HNDL,    /* VIPKL_get_hh_hndl */
  VIPKL_GET_HCA_UL_INFO,  /* VIPKL_get_hca_ul_info */
  VIPKL_GET_HCA_ID,     /* VIPKL_get_hca_id */

  VIPKL_ALLOC_PD,       /* VIPKL_create_pd */
  VIPKL_DEALLOC_PD,     /* VIPKL_destroy_pd */

  VIPKL_CREATE_QP,      /* VIPKL_create_qp */
  VIPKL_MODIFY_QP,      /* VIPKL_modify_qp */
  VIPKL_QUERY_QP,       /* VIPKL_query_qp  */
  VIPKL_DESTROY_QP,     /* VIPKL_destroy_qp */
  VIPKL_GET_SPECIAL_QP, /* VIPKL_get_special_qp */
  VIPKL_GET_QP_BUF_SZ,  /* VIPKL_get_qp_buf_size */
  VIPKL_ATTACH_TO_MULTICAST,   /* VIPKL_attach_to_multicast */
  VIPKL_DETACH_FROM_MULTICAST, /* VIPKL_detach_from_multicast */

  VIPKL_CREATE_SRQ,     /* VIPKL_create_srq */
  VIPKL_DESTROY_SRQ,    /* VIPKL_destroy_srq*/
  VIPKL_QUERY_SRQ,      /* VIPKL_modify_srq */

  VIPKL_CREATE_CQ,      /* VIPKL_create_cq */
  VIPKL_QUERY_CQ,       /* VIPKL_get_cq_props */
  VIPKL_RESIZE_CQ,      /* VIPKL_resize_cq */
  VIPKL_DESTROY_CQ,     /* VIPKL_destroy_cq */
  VIPKL_GET_CQ_BUF_SZ,  /* VIPKL_query_cq_memory_size */

  VIPKL_CREATE_MR,    /* VIPKL_create_mr */
  VIPKL_CREATE_SMR,    /* VIPKL_create_smr */
  VIPKL_REREGISTER_MR,
  VIPKL_DESTROY_MR,   /* VIPKL_destroy_mr */           /* Physical MR */
  VIPKL_QUERY_MR,     /* VIPKL_query_mr  */
  VIPKL_CREATE_MW,    /* VIPKL_create_mw */
  VIPKL_DESTROY_MW,   /* VIPKL_destroy_mw */           /* Physical MR */
  VIPKL_QUERY_MW,     /* VIPKL_query_mw  */
  VIPKL_ALLOC_FMR,
  VIPKL_MAP_FMR,
  VIPKL_UNMAP_FMR,
  VIPKL_FREE_FMR,

#if 0
  /* Obsolete for user space. Use VIPKL_EQ functions instead */
  VIPKL_SET_COMP_EVENTH,        /* VIPKL_bind_completion_event_handler */
  VIPKL_SET_ASYNC_EVENTH,       /* VIPKL_bind_async_event_handler */
#endif
  
  /* VIPKL_EQ */
  VIPKL_EQ_NEW,
  VIPKL_EQ_DEL,
  VIPKL_EQ_EVAPI_SET_COMP_EVENTH,        /* VIPKL_EQ_evapi_set_comp_eventh */
  VIPKL_EQ_EVAPI_CLEAR_COMP_EVENTH,      /* VIPKL_EQ_evapi_clear_comp_eventh */
  VIPKL_EQ_SET_ASYNC_EVENT_HANDLER,      /* VIPKL_EQ_set_async_event_handler */
  VIPKL_EQ_CLEAR_ASYNC_EVENT_HANDLER,    /* VIPKL_EQ_clear_async_event_handler */
  VIPKL_EQ_POLL,

  /* VIPKL_cqblk */
  VIPKL_CQBLK_ALLOC_CTX,      /* VIPKL_cqblk_alloc_ctx  */
  VIPKL_CQBLK_FREE_CTX,       /* VIPKL_cqblk_free_ctx   */
  VIPKL_CQBLK_WAIT,           /* VIPKL_cqblk_waitc      */
  VIPKL_CQBLK_SIGNAL,         /* VIPKL_cqblk_signal     */

  VIPKL_PROCESS_LOCAL_MAD,      /* VIPKL_process_local_mad */
  
  VIPKL_ALLOC_MAP_DEVMEM,
  VIPKL_QUERY_DEVMEM,
  VIPKL_FREE_UNMAP_DEVMEM,
#if defined(MT_SUSPEND_QP)
  VIPKL_SUSPEND_QP,
  VIPKL_SUSPEND_CQ,
#endif
  /* The following code must always be the last */
  VIPKL_MAX_OP
} VIPKL_ops_t;

/* defintions & struct of hca resource tracking */
#define VIP_HCA_STATE_EMPTY -2
#define VIP_HCA_STATE_DESTROY -3
#define VIP_HCA_STATE_CREATE -1
#define VIP_HCA_STATE_AVAIL 0
typedef struct VIP_hca_state
{
    VIP_RSCT_t          rsct;
    MOSAL_spinlock_t    lock;
    int                 state;
}VIP_hca_state_t;   

#ifdef MT_KERNEL
/* Return value != VIP_OK (0) on failure (VIP_EINVAL_PARAM for invalid po_sz/pi_sz) */
VIP_ret_t VIPKL_ioctl(VIPKL_ops_t ops, VIP_hca_state_t* rsct_arr, void *pi, u_int32_t isz, void *po, 
	u_int32_t osz, u_int32_t* bs_p );

VIP_hca_state_t* VIPKL_open(void);
void VIPKL_close(VIP_hca_state_t* hca_stat_p);
#else
int vip_ioctl_wrapper(VIPKL_ops_t ops,void *pi, u_int32_t pi_sz,void *po, u_int32_t po_sz);
int vip_ioctl_open(void);
#endif

/*************************************************************************
 * Function: VIPKL_open_hca
 *************************************************************************/ 
  struct i_VIPKL_open_hca_ops_t {
    IN VAPI_hca_id_t hca_id;
    IN MT_bool profile_is_null;
    IN MT_bool sugg_profile_is_null;
    IN EVAPI_hca_profile_t  profile;
  };
  struct o_VIPKL_open_hca_ops_t {
    OUT VIP_ret_t ret;
    OUT EVAPI_hca_profile_t  sugg_profile;
    OUT VIP_hca_hndl_t hca_hndl;
  };

/*************************************************************************
 * Function: VIPKL_close_hca
 *************************************************************************/ 
  struct i_VIPKL_close_hca_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  };
  struct o_VIPKL_close_hca_ops_t {
    OUT VIP_ret_t ret;
  };


/*************************************************************************
 * Function: VIPKL_get_hca_hndl
 *************************************************************************/ 
  struct i_VIPKL_get_hca_hndl_ops_t {
    IN VAPI_hca_id_t hca_id; 
  };
  struct o_VIPKL_get_hca_hndl_ops_t {
    OUT VIP_ret_t ret;
    OUT VIP_hca_hndl_t 	hca_hndl;
    OUT HH_hca_hndl_t 	hh_hndl;
  };

/*************************************************************************
 * Function: VIPKL_list_hcas
 *************************************************************************/ 
struct i_VIPKL_list_hcas_ops_t {
  IN u_int32_t         hca_id_buf_sz; 
};
struct o_VIPKL_list_hcas_ops_t {
  OUT VIP_ret_t ret;
  OUT u_int32_t       num_of_hcas;
  OUT VAPI_hca_id_t   hca_id_buf[1];
};



/*************************************************************************
                        
                        HOBKL functions
 
 *************************************************************************/ 
/*************************************************************************
 * Function: VIPKL_alloc_ul_resources <==> HOBKL_alloc_ul_resources
 *
 *************************************************************************/ 
  struct i_VIPKL_alloc_ul_resources_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  	IN MOSAL_protection_ctx_t prot_ctx;
    IN MT_size_t hca_ul_resources_sz;
  };
  struct o_VIPKL_alloc_ul_resources_ops_t {
    OUT VIP_ret_t ret;
    OUT EM_async_ctx_hndl_t async_hndl_ctx;
    OUT MT_ulong_ptr_t hca_ul_resources[1];
  };

/*************************************************************************
 * Function: VIPKL_free_ul_resources <==> HOBKL_free_ul_resources
 *
 *************************************************************************/ 
  struct i_VIPKL_free_ul_resources_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN MT_size_t hca_ul_resources_sz;
    IN EM_async_ctx_hndl_t async_hndl_ctx;
    IN MT_ulong_ptr_t hca_ul_resources[1];
  };
  struct o_VIPKL_free_ul_resources_ops_t {
    OUT VIP_ret_t ret;
  };

/*************************************************************************
 * Function: VIPKL_query_hca_cap <==> HOBKL_query_hca_cap
 *
 *************************************************************************/ 
  struct i_VIPKL_query_hca_cap_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  };
  struct o_VIPKL_query_hca_cap_ops_t {
    OUT VIP_ret_t ret;
    OUT VAPI_hca_cap_t caps;
  };

/*************************************************************************
 * Function: VIPKL_query_port_prop  <==> HOBKL_query_port_prop
 *
 *************************************************************************/ 
  struct i_VIPKL_query_port_prop_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN IB_port_t port_num;
  };
  struct o_VIPKL_query_port_prop_ops_t {
    OUT VIP_ret_t ret;
    OUT VAPI_hca_port_t port_props;
  };

/*************************************************************************
 * Function: VIPKL_query_port_pkey_tbl  <==> HOBKL_query_port_pkey_tbl
 *
 *************************************************************************/ 
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

/*************************************************************************
 * Function: VIPKL_query_port_gid_tbl  <==> HOBKL_query_port_gid_tbl
 *
 *************************************************************************/ 
  struct i_VIPKL_query_port_gid_tbl_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN IB_port_t port_num;
    IN u_int16_t tbl_len_in;
  };
  struct o_VIPKL_query_port_gid_tbl_ops_t {
    OUT VIP_ret_t ret;
    OUT u_int16_t  tbl_len_out;
    OUT VAPI_gid_t gid_tbl[1];
  };

/*************************************************************************
 * Function: VIP_modify_hca_attr <==> HOBKL_modify_hca_attr
 *
 *************************************************************************/ 
  struct i_VIPKL_modify_hca_attr_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN IB_port_t port_num;
    IN VAPI_hca_attr_t 		hca_attr;
    IN VAPI_hca_attr_mask_t hca_attr_mask;
  };
  struct o_VIPKL_modify_hca_attr_ops_t {
    OUT VIP_ret_t ret;
  };
/*************************************************************************
 * Function: VIPKL_get_hh_hndl <==> HOBKL_get_hh_hndl
 *
 *************************************************************************/ 
  struct i_VIPKL_get_hh_hndl_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  };
  struct o_VIPKL_get_hh_hndl_ops_t {
    OUT VIP_ret_t ret;
    OUT HH_hca_hndl_t hh;
  };

/*************************************************************************
 * Function: VIPKL_get_hca_ul_info <==> HOBKL_get_hca_ul_info
 *
 *************************************************************************/ 
  struct i_VIPKL_get_hca_ul_info_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  };
  struct o_VIPKL_get_hca_ul_info_ops_t {
    OUT VIP_ret_t ret;
    OUT HH_hca_dev_t hca_ul_info;
  };

/*************************************************************************
 * Function: VIPKL_get_hca_id <==> HOBKL_get_hca_id
 *
 *************************************************************************/ 
  struct i_VIPKL_get_hca_id_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
  };
  struct o_VIPKL_get_hca_id_ops_t {
    OUT VIP_ret_t ret;
    OUT VAPI_hca_id_t hca_id;
  };

/*************************************************************************
                        PDM functions
 *************************************************************************/ 

/*************************************************************************
 * Function: VIP_create_pd <==> PDM_create_pd
 *
 *************************************************************************/ 
  struct i_VIPKL_create_pd_ops_t {
  	IN VIP_hca_hndl_t hca_hndl;
	IN MOSAL_protection_ctx_t prot_ctx; 
  	IN MT_size_t pd_ul_resources_sz;
  	IN OUT MT_ulong_ptr_t pd_ul_resources[1]; 
  };
  struct o_VIPKL_create_pd_ops_t {
       OUT VIP_ret_t ret;
  	OUT PDM_pd_hndl_t 	pd_hndl; 
  	OUT	HH_pd_hndl_t 	pd_num;
  	IN OUT MT_ulong_ptr_t pd_ul_resources[1]; 
  };


/*************************************************************************
 * Function: VIP_destroy_pd <==> PDM_destroy_pd
 *
 *************************************************************************/ 
  struct i_VIPKL_destroy_pd_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN PDM_pd_hndl_t pd_hndl;
  };
  struct o_VIPKL_destroy_pd_ops_t {
    OUT VIP_ret_t ret;
  };




/*************************************************************************
                        
                        QPM functions
 
 *************************************************************************/ 

/******************************************************************************
 *  Function: VIPKL_create_qp <==> QPM_create_qp
 *****************************************************************************/
struct i_VIPKL_create_qp_ops_t {
  	IN VIP_hca_hndl_t hca_hndl;
  	VAPI_qp_hndl_t vapi_qp_hndl;
	IN QPM_qp_init_attr_t qp_init_attr;
    IN EM_async_ctx_hndl_t async_hndl_ctx;
  	IN MT_size_t qp_ul_resources_sz;
  	IN MT_ulong_ptr_t qp_ul_resources[1];
};
struct o_VIPKL_create_qp_ops_t {
	OUT VIP_ret_t ret;
  	OUT QPM_qp_hndl_t qp_hndl;
  	OUT VAPI_qp_num_t qp_num;
  	IN OUT MT_ulong_ptr_t qp_ul_resources[1]; 
};

/******************************************************************************
 *  Function: VIPKL_get_special_qp <==> QPM_get_special_qp
 *****************************************************************************/
struct i_VIPKL_get_special_qp_ops_t {
  IN VIP_hca_hndl_t 	hca_hndl;
  IN VAPI_qp_hndl_t     qp_hndl;
  IN EM_async_ctx_hndl_t async_hndl_ctx;
  IN IB_port_t 			port;
  IN VAPI_special_qp_t 	qp_type;
  IN QPM_qp_init_attr_t qp_init_attr;  
  IN MT_size_t 			qp_ul_resources_sz;
  IN OUT MT_ulong_ptr_t qp_ul_resources[1];
};                    
struct o_VIPKL_get_special_qp_ops_t {
  OUT VIP_ret_t ret;
  OUT QPM_qp_hndl_t	qp_hndl;
  OUT IB_wqpn_t		sqp_hndl;
  IN OUT MT_ulong_ptr_t qp_ul_resources[1];
};                    

/******************************************************************************
 *  Function: VIPKL_destroy_qp <==> QPM_destroy_qp
 *****************************************************************************/
  struct i_VIPKL_destroy_qp_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN QPM_qp_hndl_t qp_hndl;
  };
  struct o_VIPKL_destroy_qp_ops_t {
	OUT VIP_ret_t ret;
  };

/******************************************************************************
 *  Function: VIPKL_modify_qp <==> QPM_modify_qp
 *****************************************************************************/
  struct i_VIPKL_modify_qp_ops_t {
    IN VIP_hca_hndl_t 		hca_hndl;
    IN QPM_qp_hndl_t 		qp_hndl;
    IN VAPI_qp_attr_mask_t 	qp_mod_mask;
    IN VAPI_qp_attr_t 		qp_mod_attr;
  };
  struct o_VIPKL_modify_qp_ops_t {
	OUT VIP_ret_t ret;
  };

/******************************************************************************
 *  Function:  VIPKL_query_qp <==> QPM_query_qp
 *****************************************************************************/
  struct i_VIPKL_query_qp_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN QPM_qp_hndl_t qp_hndl;
  };
  struct o_VIPKL_query_qp_ops_t {
    OUT VIP_ret_t ret;
    OUT QPM_qp_query_attr_t qp_query_prop;
    OUT VAPI_qp_attr_mask_t qp_mod_mask;
  };

/******************************************************************************
 *  Function:  VIPKL_get_qp_buf_size <==> QPM_get_buf_size
 *****************************************************************************/
struct VIPKL_get_qp_buf_size_ops_t {
  /*IN*/ VIP_hca_hndl_t hca_hndl;
  /*IN*/ VAPI_ts_type_t  ts;
  /*IN*/ VAPI_qp_cap_t *qp_cap_in_p;
  /*OUT*/ MT_size_t *buf_sz_p;
};

/*******************************************************************
 * FUNCTION: VIPKL_attach_to_multicast <==> QPM_attach_to_multicast
 *******************************************************************/ 
  struct i_VIPKL_attach_to_multicast_ops_t {
     IN	VAPI_hca_hndl_t     hca_hndl;
     IN IB_gid_t            mcg_dgid;
     IN VAPI_qp_hndl_t      qp_hndl;
  };
  struct o_VIPKL_attach_to_multicast_ops_t {
	OUT VIP_ret_t ret;
  };

/******************************************************************************
 *  Function:  VIPKL_detach_from_multicast <==> QPM_detach_from_multicast
 *****************************************************************************/
  struct i_VIPKL_detach_from_multicast_ops_t {
     IN      VAPI_hca_hndl_t     hca_hndl;
     IN      IB_gid_t            mcg_dgid;
     IN      VAPI_qp_hndl_t      qp_hndl;
  };
  struct o_VIPKL_detach_from_multicast_ops_t {
	OUT VIP_ret_t ret;
  };


/*************************************************************************
                        
                        SRQM functions
 
 *************************************************************************/ 

/******************************************************************************
 *  Function: VIPKL_create_srq <==> SRQM_create_srq
 *****************************************************************************/
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

/******************************************************************************
 *  Function: VIPKL_destroy_srq <==> SRQM_destroy_srq
 *****************************************************************************/
struct i_VIPKL_destroy_srq_ops_t {
                  VIP_hca_hndl_t hca_hndl;
                  SRQM_srq_hndl_t srq_hndl;
};

struct o_VIPKL_destroy_srq_ops_t {
  VIP_ret_t ret;
};

/******************************************************************************
 *  Function: VIPKL_query_srq <==> SRQM_query_srq
 *****************************************************************************/
struct i_VIPKL_query_srq_ops_t {
                VIP_hca_hndl_t hca_hndl;
                SRQM_srq_hndl_t srq_hndl;
};
                
struct o_VIPKL_query_srq_ops_t {
                VIP_ret_t ret;
                u_int32_t limit;
};


/*************************************************************************
                        
                        CQM functions
 
 *************************************************************************/ 

/*************************************************************************
 * Function: VIPKL_query_cq_memory_size <==> CQM_query_memory_size
 *************************************************************************/ 
struct VIPKL_query_cq_memory_size_ops_t {
  /*IN*/ VIP_hca_hndl_t hca_hndl;
  
  /*IN*/ MT_size_t num_o_entries;

  /*OUT*/ MT_size_t* cq_bytes;
};

/*************************************************************************
 * Function: VIPKL_create_cq <==> CQM_create_cq
 *************************************************************************/ 
struct i_VIPKL_create_cq_ops_t {
  IN VIP_hca_hndl_t hca_hndl;
  IN VAPI_cq_hndl_t vapi_cq_hndl;
  IN MOSAL_protection_ctx_t  usr_prot_ctx;
  IN MT_size_t cq_ul_resources_sz;
  IN EM_async_ctx_hndl_t async_hndl_ctx;
  IN OUT MT_ulong_ptr_t cq_ul_resources[1];
};
struct o_VIPKL_create_cq_ops_t {
  OUT VIP_ret_t ret;
  OUT CQM_cq_hndl_t cq;
  OUT HH_cq_hndl_t	cq_id;
  IN OUT MT_ulong_ptr_t cq_ul_resources[1];
};
/*************************************************************************
 * Function: VIPKL_destroy_cq <==> CQM_destroy_cq
 *************************************************************************/ 
  struct i_VIPKL_destroy_cq_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN CQM_cq_hndl_t cq;
  };
  struct o_VIPKL_destroy_cq_ops_t {
    OUT VIP_ret_t ret;
  };

/*************************************************************************
 * Function: VIPKL_get_cq_props <==> CQM_get_cq_props
 *************************************************************************/ 
  struct i_VIPKL_get_cq_props_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN CQM_cq_hndl_t cq;
  };
  struct o_VIPKL_get_cq_props_ops_t {
    OUT VIP_ret_t ret;
    OUT HH_cq_hndl_t	cq_id;
    OUT VAPI_cqe_num_t	num_o_entries;
  };

/*************************************************************************
 * Function: VIPKL_resize_cq <==> CQM_resize_cq
 *************************************************************************/ 
  struct i_VIPKL_resize_cq_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN CQM_cq_hndl_t cq;
    IN MT_size_t cq_ul_resources_sz;
    IN OUT MT_ulong_ptr_t cq_ul_resources[1];
  };
  struct o_VIPKL_resize_cq_ops_t {
    OUT VIP_ret_t ret;
    IN OUT MT_ulong_ptr_t cq_ul_resources[1];
  };

 
 /* TK - temporary till Elazar will complete the resize CQ */ 
 /*************************************************************************
 * Function: VIPKL_modify_cq_props <==> CQM_modify_cq_props
 *************************************************************************/ 
  struct i_VIPKL_modify_cq_props_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN CQM_cq_hndl_t cq;
    IN MT_size_t req_num_o_entries;
  };
  struct o_VIPKL_modify_cq_props_ops_t {
    OUT VIP_ret_t ret;
    OUT MT_size_t num_o_entries;
  };




/*************************************************************************
                        
                        MMU functions
 
 *************************************************************************/ 

/*******************************************************************
 * FUNCTION: VIPKL_create_mr <==> MM_create_mr
 *******************************************************************/ 
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

/*******************************************************************
 * FUNCTION: VIPKL_create_smr <==> MM_create_smr
 *******************************************************************/ 
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
  
  /*******************************************************************
 * FUNCTION:  VIPKL_reregister_mr <==>  MM_reregister_mr
 *******************************************************************/ 
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

  /*******************************************************************
 * FUNCTION:  VIPKL_create_mw <==>  MM_create_mw
 *******************************************************************/ 
  struct i_VIPKL_create_mw_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN PDM_pd_hndl_t pd_hndl;
  };
  struct o_VIPKL_create_mw_ops_t {
    OUT VIP_ret_t ret;
    OUT MM_key_t		r_key;
  };

/*******************************************************************
 * FUNCTION:  VIPKL_alloc_fmr <==>  MM_alloc_fmr
 *******************************************************************/ 
struct i_VIPKL_alloc_fmr_ops_t {
    IN  VIP_hca_hndl_t      hca_hndl;
    IN  EVAPI_fmr_t         fmr_props;
    IN  PDM_pd_hndl_t       pd_hndl;
};
struct o_VIPKL_alloc_fmr_ops_t {
    OUT VIP_ret_t ret;
    OUT MM_mrw_hndl_t      fmr_hndl;
};
/*******************************************************************
 * FUNCTION:  VIPKL_map_fmr <==>  MM_map_fmr
 *******************************************************************/ 
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
/*******************************************************************
 * FUNCTION:  VIPKL_unmap_fmr <==>  MM_unmap_fmr
 *******************************************************************/ 
struct i_VIPKL_unmap_fmr_ops_t {
    IN   VAPI_hca_hndl_t      hca_hndl;
    IN   MT_size_t            num_of_fmr_to_unmap;
    IN   EVAPI_fmr_hndl_t       fmr_hndl_array[1];
};
struct o_VIPKL_unmap_fmr_ops_t {
    OUT VIP_ret_t ret;
};
/*******************************************************************
 * FUNCTION:  VIPKL_free_fmr <==>  MM_free_fmr
 *******************************************************************/ 
struct i_VIPKL_free_fmr_ops_t {
    IN   VAPI_hca_hndl_t      hca_hndl;
    IN   EVAPI_fmr_hndl_t    fmr_hndl;
};
struct o_VIPKL_free_fmr_ops_t {
    OUT VIP_ret_t ret;
};

  /*******************************************************************
 * FUNCTION:  VIPKL_destroy_mr <==>  MM_destroy_mr
 *******************************************************************/ 
  struct i_VIPKL_destroy_mr_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN MM_mrw_hndl_t mrw_hndl;
  };
struct o_VIPKL_destroy_mr_ops_t {
    OUT VIP_ret_t ret;
};

/*******************************************************************
 * FUNCTION:  VIPKL_destroy_mw <==>        MM_destroy_mw
 *******************************************************************/ 
  struct i_VIPKL_destroy_mw_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN MM_mrw_hndl_t mrw_hndl;
  };
struct o_VIPKL_destroy_mw_ops_t {
    OUT VIP_ret_t ret;
};

/*******************************************************************
 * FUNCTION:  VIPKL_query_mr <==>        MM_query_mr
 *******************************************************************/ 
  struct i_VIPKL_query_mr_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN MM_mrw_hndl_t mrw_hndl;
  };
  struct o_VIPKL_query_mr_ops_t {
    OUT VIP_ret_t ret;
    OUT MM_VAPI_mro_t mr_prop;
  };
/*******************************************************************
 * FUNCTION:  VIPKL_query_mw 
 *******************************************************************/ 
 struct i_VIPKL_query_mw_ops_t {
    IN VIP_hca_hndl_t hca_hndl;
    IN IB_rkey_t initial_key;
  };
  struct o_VIPKL_query_mw_ops_t {
    OUT VIP_ret_t ret;
    OUT VAPI_rkey_t current_key;
  };



/*************************************************************************
                        
        EM functions (all functions using the k2u_cbk infrastructure)
 
 *************************************************************************/ 

/*******************************************************************
 * FUNCTION:   VIPKL_bind_async_error_handler <==> EM_bind_async_error_handler
 *******************************************************************/ 
#if 0
/* Obsolete for user-space (wrapper). Use VIPKL_EQ_set_async_event_handler() instead */

typedef struct {
  VAPI_async_event_handler_t handler;
  void* private_data;
} async_handler_data_t;

struct async_handler_params_t {
  /*IN*/VAPI_hca_hndl_t hca_hndl;
  /*IN*/VAPI_event_record_t event_record;
  /*IN*/void* private_data;
};

struct i_VIPKL_bind_async_error_handler_ops_t {
  IN VIP_hca_hndl_t 		hca_hndl;
  IN k2u_cbk_hndl_t 		proc_cbk_hndl;
  IN k2u_cbk_id_t   		async_handler_id;
  IN async_handler_data_t * 	cbk_context_p;
};
struct o_VIPKL_bind_async_error_handler_ops_t {
  OUT VIP_ret_t ret;
};

#endif

/*************************************************************************
 * FUNCTION: VIPKL_set_async_event_handler <==> EM_set_async_event_handler
 *************************************************************************/ 
#if 0
/* Obsolete for user-space (wrapper). Use VIPKL_EQ_set_async_event_handler() instead */

struct i_VIPKL_set_async_event_handler_ops_t{
  /*IN*/  VIP_hca_hndl_t                 hca_hndl;
  /*IN*/  VAPI_async_event_handler_t      handler;
  /*IN*/  void*                           private_data;
  /*OUT*/ EVAPI_async_handler_hndl_t     *async_handler_hndl_p;
};
                                        
struct o_VIPKL_set_async_event_handler_ops_t {
  OUT VIP_ret_t ret;
  OUT EVAPI_async_handler_hndl_t     *async_handler_hndl; /* TK ?? */

};
#endif

/*************************************************************************
 * FUNCTION: VIPKL_clear_async_event_handler <==> EM_clear_async_event_handler
 *************************************************************************/ 
#if 0
/* Obsolete for user-space (wrapper). Use VIPKL_EQ_clear_async_event_handler() instead */
struct i_VIPKL_clear_async_event_handler_ops_t{
  /*IN*/ VIP_hca_hndl_t                  hca_hndl;
  /*IN*/ EVAPI_async_handler_hndl_t      async_handler_hndl;
};

struct o_VIPKL_clear_async_event_handler_ops_t {
  OUT VIP_ret_t ret;
};
#endif

/*************************************************************************
 * FUNCTION: VIPKL_bind_completion_event_handler <==> EM_bind_completion_event_handler
 *************************************************************************/ 
#if 0
/* Obsolete for user-space (wrapper). Use VIPKL_EQ_evapi_set_comp_eventh() instead */

typedef struct {
  VAPI_completion_event_handler_t handler;
  void* private_data;
} comp_handler_data_t;

struct completion_handler_params_t {
  /*IN*/VAPI_hca_hndl_t hca_hndl;
  /*IN*/VAPI_cq_hndl_t  cq_hndl;
  /*IN*/void* private_data;
};

struct i_VIPKL_bind_completion_event_handler_ops_t {
  IN VIP_hca_hndl_t 		hca_hndl;
  IN k2u_cbk_hndl_t 		proc_cbk_hndl;
  IN k2u_cbk_id_t   		comp_handler_id;
  IN comp_handler_data_t  *	cbk_context_p;
};
struct o_VIPKL_bind_completion_event_handler_ops_t {
  OUT VIP_ret_t ret;
};

#endif

/*************************************************************************
 * VIPKL_EQ functions 
 *************************************************************************/ 
struct i_VIPKL_EQ_new_ops_t {
  IN VAPI_hca_hndl_t hca_hndl;
  IN VIPKL_EQ_cbk_type_t cbk_type;
  IN EM_async_ctx_hndl_t async_ctx;
};
struct o_VIPKL_EQ_new_ops_t {
  OUT VIP_ret_t ret;
  OUT VIPKL_EQ_hndl_t vipkl_eq_h;
};

struct i_VIPKL_EQ_del_ops_t {
  IN VAPI_hca_hndl_t hca_hndl;
  IN VIPKL_EQ_hndl_t vipkl_eq;
};
struct o_VIPKL_EQ_del_ops_t {
  OUT VIP_ret_t ret;
};

struct i_VIPKL_EQ_evapi_set_comp_eventh_ops_t {
  IN  VAPI_hca_hndl_t hca_hndl;
  IN  VIPKL_EQ_hndl_t                  vipkl_eq;
  IN  CQM_cq_hndl_t                    vipkl_cq;
  IN  VAPI_completion_event_handler_t  completion_handler;
  IN  void *                           private_data;
};
struct o_VIPKL_EQ_evapi_set_comp_eventh_ops_t {
  OUT VIP_ret_t ret;
};

struct i_VIPKL_EQ_evapi_clear_comp_eventh_ops_t {
  IN  VAPI_hca_hndl_t                  hca_hndl;
  IN  VIPKL_EQ_hndl_t                  vipkl_eq;
  IN  CQM_cq_hndl_t                    vipkl_cq;
};
struct o_VIPKL_EQ_evapi_clear_comp_eventh_ops_t {
  OUT VIP_ret_t ret;
};

struct i_VIPKL_EQ_set_async_event_handler_ops_t {
  IN VIP_RSCT_t usr_ctx;
  IN VAPI_hca_hndl_t hca_hndl;
  IN VIPKL_EQ_hndl_t                  vipkl_eq;
  IN VAPI_async_event_handler_t       async_eventh;
  IN void *                           private_data;
};
struct o_VIPKL_EQ_set_async_event_handler_ops_t {
  OUT VIP_ret_t ret;
  OUT EVAPI_async_handler_hndl_t      async_handler_hndl;
};

struct i_VIPKL_EQ_clear_async_event_handler_ops_t {
  IN VIP_RSCT_t usr_ctx;
  IN VAPI_hca_hndl_t hca_hndl;
  IN VIPKL_EQ_hndl_t                  vipkl_eq;
  IN EVAPI_async_handler_hndl_t       async_handler_hndl;
};
struct o_VIPKL_EQ_clear_async_event_handler_ops_t {
  OUT VIP_ret_t ret;
};

struct i_VIPKL_EQ_poll_ops_t {
     VAPI_hca_hndl_t hca_hndl;
  IN  VIPKL_EQ_hndl_t vipkl_eq;
};
struct o_VIPKL_EQ_poll_ops_t {
  OUT VIP_ret_t ret;
  OUT VIPKL_EQ_event_t eqe;
};

/*************************************************************************
 * VIPKL_cqblk functions 
 *************************************************************************/ 
struct i_VIPKL_cqblk_alloc_ctx_ops_t {
  IN	VIP_hca_hndl_t   hca_hndl;
  IN	CQM_cq_hndl_t    vipkl_cq;
};
struct o_VIPKL_cqblk_alloc_ctx_ops_t {
  OUT VIP_ret_t ret;
  OUT VIPKL_cqblk_hndl_t cqblk_hndl;
};

struct i_VIPKL_cqblk_free_ctx_ops_t {
    	VIP_hca_hndl_t   hca_hndl;
  IN	VIPKL_cqblk_hndl_t cqblk_hndl;
};
struct o_VIPKL_cqblk_free_ctx_ops_t {
  OUT VIP_ret_t ret;
};

struct i_VIPKL_cqblk_wait_ops_t {
  	VIP_hca_hndl_t   hca_hndl;
  IN	VIPKL_cqblk_hndl_t cqblk_hndl;
  IN	MT_size_t timeout_usec;
};
struct o_VIPKL_cqblk_wait_ops_t {
  OUT VIP_ret_t ret;
};

struct i_VIPKL_cqblk_signal_ops_t {
    	VIP_hca_hndl_t   hca_hndl;
  IN	VIPKL_cqblk_hndl_t cqblk_hndl;
};
struct o_VIPKL_cqblk_signal_ops_t {
  OUT VIP_ret_t ret;
};

/*************************************************************************
 * Function: VIPKL_process_local_mad  <==> HOBKL_process_local_mad
 *
 *************************************************************************/ 
struct i_VIPKL_process_local_mad_ops_t {
  IN    VIP_hca_hndl_t hca_hndl;
  IN    IB_port_t      port_num;
  IN    IB_lid_t       slid;
  IN	  EVAPI_proc_mad_opt_t	 proc_mad_opts;
  IN    u_int8_t       mad_in[IB_MAD_LEN];
};
struct o_VIPKL_process_local_mad_ops_t {
  OUT VIP_ret_t ret;
  OUT   u_int8_t       mad_out[IB_MAD_LEN];
};

/*************************************************************************
 * Function: VIPKL_alloc_map_devmem  <==> DEVMM_alloc_map_devmem
 *
 *************************************************************************/ 
struct i_VIPKL_alloc_map_devmem_ops_t {
  IN   VIP_hca_hndl_t 	hca_hndl;
  IN   EVAPI_devmem_type_t mem_type;
  IN   VAPI_size_t           bsize;
  IN   u_int8_t            align_shift;
  IN   MT_bool             map; 
};
struct o_VIPKL_alloc_map_devmem_ops_t {
  OUT VIP_ret_t ret;
  OUT VAPI_phy_addr_t     buf;
  OUT void*               virt_addr;
  OUT DEVMM_dm_hndl_t     dm_hndl;
};

/*************************************************************************
 * Function: VIPKL_query_devmem  <==> DEVMM_alloc_devmem
 *
 *************************************************************************/ 
struct i_VIPKL_query_devmem_ops_t {
  IN   VIP_hca_hndl_t 	hca_hndl;
  IN   EVAPI_devmem_type_t  mem_type; 	   
  IN   u_int8_t             align_shift;
};
struct o_VIPKL_query_devmem_ops_t {
  OUT VIP_ret_t ret;
  OUT  EVAPI_devmem_info_t  devmem_info;
};

/*************************************************************************
 * Function: VIPKL_free_devmem  <==> DEVMM_free_devmem
 *
 *************************************************************************/ 
struct i_VIPKL_free_unmap_devmem_ops_t {
  IN   VIP_hca_hndl_t 	hca_hndl;
  IN   DEVMM_dm_hndl_t  dm_hndl;
 };
struct o_VIPKL_free_unmap_devmem_ops_t {
  OUT VIP_ret_t ret;
 };

#if defined(MT_SUSPEND_QP)
/******************************************************************************
 *  Function: VIPKL_suspend_qp <==> QPM_suspend_qp
 *****************************************************************************/
  struct i_VIPKL_suspend_qp_ops_t {
    IN VIP_hca_hndl_t 		hca_hndl;
    IN QPM_qp_hndl_t 		qp_hndl;
    IN MT_bool              suspend_flag;
  };
  struct o_VIPKL_suspend_qp_ops_t {
	OUT VIP_ret_t ret;
  };

/******************************************************************************
 *  Function: VIPKL_suspend_cq <==> CQM_suspend_cq
 *****************************************************************************/
  struct i_VIPKL_suspend_cq_ops_t {
    IN VIP_hca_hndl_t 		hca_hndl;
    IN CQM_cq_hndl_t 		cq_hndl;
    IN MT_bool              do_suspend;
  };
  struct o_VIPKL_suspend_cq_ops_t {
	OUT VIP_ret_t ret;
  };

#endif

#endif
