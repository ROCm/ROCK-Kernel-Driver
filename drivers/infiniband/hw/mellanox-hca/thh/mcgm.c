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
#include <mcgm.h>
#include <cmdif.h>
#include <epool.h>
#include <thh_hob.h>
#include <vip_hashp.h>
#include <vapi_common.h>

/*================ macro definitions ===============================================*/

//#define EPOOL_ENTRY_SIZE 8 /* the minimum needed for the Epool */

/*================ type definitions ================================================*/
typedef VIP_hashp_p_t mcg_status_hash_t;
typedef enum
{
  MGHT =0, AMGM=1
} mcg_tbl_kind_t;
typedef enum
{
  INSERT =0, REMOVE=1
} mcg_op_t;



/* The main MCG-manager structure */
typedef struct THH_mcgm_st
{
  THH_hob_t      hob; 
  THH_ver_info_t version;
  VAPI_size_t      mght_total_entries;
  VAPI_size_t      mght_hash_bins; /* actually, it requires 16 bit!! */
  u_int16_t      max_qp_per_mcg; 
  MOSAL_mutex_t  mtx;     /* used internally */
  THH_cmd_t      cmd_if_h;
  EPool_t        free_list;   /* the free list of the non-hash part of the MCG table */
  mcg_status_hash_t my_hash;
  //u_int32_t      amgm_idx;
  //u_int32_t      mght_idx;
}THH_mcgm_props_t;

/* multicast groups status hash tabel*/
struct mcg_status_entry
{
  IB_gid_t  mgid;       /* Group's GID */
  u_int16_t num_valid_qps;
  u_int32_t idx;    //absolute idx !!!  (AMGM + MGHT)
  u_int32_t prev_idx; //absolute idx !!! (AMGM + MGHT)
};

/*================ global variables definitions ====================================*/
static const EPool_meta_t  free_list_meta =
{
  2*sizeof(unsigned long),  
  0, /* unsigned long  'prev' index   */
  sizeof(unsigned long)  
};


/*================ static functions prototypes =====================================*/

static HH_ret_t THMCG_get_status_entry(IB_gid_t gid,
                                       mcg_status_hash_t* hash_p,
                                       mcg_status_entry_t* mcg_p);
static HH_ret_t  THMCG_update_status_entry(mcg_status_hash_t* hash_p,mcg_status_entry_t* entry_p,MT_bool new_e);
static HH_ret_t  THMCG_remove_status_entry(mcg_status_hash_t* hash_p,mcg_status_entry_t* entry_p);

static inline HH_ret_t read_alloc_mgm(THH_mcgm_t mcgm, THH_mcg_entry_t* entry,u_int32_t idx);

static HH_ret_t THMCG_reduce_MGHT(THH_mcgm_t mcgm, u_int32_t idx,THH_mcg_entry_t* fw_tmp_entry);

static HH_ret_t THMCG_find_last_index(THH_mcgm_t mcgm, THH_mcg_hash_t fw_hash_val,
                                      u_int32_t* last,  THH_mcg_entry_t* last_entry);
static inline void print_fw_entry(THH_mcg_entry_t* entry);
static inline void print_status_entry(mcg_status_entry_t* my_entry);

/*================ global functions definitions ====================================*/

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
   
   implementation saving the values:
   1.Total number of entries in the MGHT. 
   2.Size of the hash table part (i.e.number of bins). 
   3.THH_cmd object to use (taken from the THH_hob using THH_hob_get_cmd_if()). 
   4.Version information (taken using THH_hob_get_ver_info(). 
   5.Free MGHT entries pool management data structure.


 ************************************************************************/

extern HH_ret_t THH_mcgm_create(/*IN */ THH_hob_t hob, 
                                /*IN */ VAPI_size_t mght_total_entries, 
                                /*IN */ VAPI_size_t mght_hash_bins, 
                                /*IN */ u_int16_t max_qp_per_mcg, 
                                /*OUT*/ THH_mcgm_t *mcgm_p )
{
  THH_mcgm_t new_mcgm_p = NULL;
  unsigned long amgm_table_size;
  VIP_common_ret_t ret=VIP_OK;
  HH_ret_t hh_ret = HH_OK;

  FUNC_IN;


  MTL_DEBUG1("%s: starting...\n", __func__);
  /* allocation of object structure */
  new_mcgm_p = (THH_mcgm_t)MALLOC(sizeof(THH_mcgm_props_t));
  if (!new_mcgm_p)
  {
    MTL_ERROR4("%s: Cannot allocate MGM object.\n", __func__);
    MT_RETURN(HH_EAGAIN);
  }
  MTL_DEBUG1("after allocating new_mcgm_p\n"); 
  memset(new_mcgm_p,0,sizeof(THH_mcgm_props_t));

  //check arguments
  MTL_DEBUG1("hash bins:"U64_FMT", total entries:"U64_FMT"\n",mght_hash_bins,mght_total_entries);
  if ((mght_hash_bins > mght_total_entries) || (mght_total_entries <= 0) || (mght_hash_bins <= 0))
  {

    MTL_ERROR4("%s: bad initial values!\n", __func__);
    FREE(new_mcgm_p);
    MT_RETURN(HH_EINVAL);
  }


  MOSAL_mutex_init(&new_mcgm_p->mtx);
  
  amgm_table_size = (unsigned long)(mght_total_entries - mght_hash_bins);
  MTL_DEBUG1("total: "U64_FMT", hashbins: "U64_FMT"  \n",
             mght_total_entries,mght_hash_bins);

  new_mcgm_p->free_list.entries = (void*)(MT_ulong_ptr_t)VMALLOC(sizeof(unsigned long)* 2 *amgm_table_size); 
  if (!new_mcgm_p->free_list.entries)
  {
    MTL_ERROR4("%s: Cannot allocate AMGM table.\n", __func__);
    FREE(new_mcgm_p);
    MT_RETURN(HH_EAGAIN);
   }
  MTL_DEBUG1("after allocating free_list.entries\n" ); 

  memset(new_mcgm_p->free_list.entries,0,sizeof(unsigned long)* 2 *amgm_table_size);
  /* init the epool */
  new_mcgm_p->free_list.size    = amgm_table_size;
  new_mcgm_p->free_list.meta    = &free_list_meta;
  epool_init(&(new_mcgm_p->free_list));



  /*filling mcgm structure */
  new_mcgm_p->hob = hob;
  new_mcgm_p->mght_total_entries = mght_total_entries;
  new_mcgm_p->mght_hash_bins = mght_hash_bins;
  new_mcgm_p->max_qp_per_mcg = max_qp_per_mcg;
  MTL_DEBUG1("max qp per mgm entry: %d\n",new_mcgm_p->max_qp_per_mcg);

  hh_ret = THH_hob_get_cmd_if (hob, &new_mcgm_p->cmd_if_h);
  if (hh_ret != HH_OK) {
    MTL_ERROR4("%s: Cannot get cmd_if object handle.\n", __func__);
    goto clean;
  }
  hh_ret = THH_hob_get_ver_info(hob, &new_mcgm_p->version);
  if (hh_ret != HH_OK) {
    MTL_ERROR4("%s: Cannot get version.\n", __func__);
    goto clean;
  }
    /*init local hash table */
  ret = VIP_hashp_create_maxsize((u_int32_t)mght_total_entries,(u_int32_t)mght_total_entries,&(new_mcgm_p->my_hash));
  if (ret != VIP_OK)
  {
    MTL_ERROR4("%s: Cannot allocate multicast status hash.\n", __func__);
    hh_ret = HH_EAGAIN;
    goto clean;
  }
  MTL_DEBUG1("after creating hash table\n"); 
  VIP_hashp_may_grow(new_mcgm_p->my_hash,FALSE);  /* fix hash table size */
  //new_mcgm_p->amgm_idx = 0;
  //new_mcgm_p->mght_idx = 0;

  /* succeeded to create object - return params: */
  *mcgm_p = new_mcgm_p;
  
  MT_RETURN(HH_OK);

  clean:
      VFREE(new_mcgm_p->free_list.entries);
      FREE(new_mcgm_p);
      MT_RETURN(hh_ret);
}

/************************************************************************
 *  Function: THMCG_remove_status_entry
 *  
    Arguments:
    hash_p - pointer to status hash 
    entry_p - pointer to entry that should be removed 
    
    Returns:
    HH_OK 
    HH_ERR - an error has occured
    
    Description: 
    removes enty from status hash 
    
 ************************************************************************/

static void  THMCG_destroy_remove_status_entry( VIP_hash_key_t key,VIP_hashp_value_t hash_val, void * priv_data)
{


  FUNC_IN;

  if (hash_val != 0) {
      FREE(hash_val);
  }
  return;
}
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

extern HH_ret_t THH_mcgm_destroy( /*IN */ THH_mcgm_t mcgm )
{
  VIP_common_ret_t ret = VIP_OK;

  FUNC_IN;

  THMCG_CHECK_NULL(mcgm,done);
  
  MOSAL_mutex_acq_ui(&mcgm->mtx);
  epool_cleanup(&(mcgm->free_list));

  /* first free the epool  */
  VFREE(mcgm->free_list.entries);

  /*destroy my_hash. Need to de-allocate 'malloced' entries */
  ret = VIP_hashp_destroy(mcgm->my_hash,&THMCG_destroy_remove_status_entry,0);
  if (ret != VIP_OK)
  {
    MTL_ERROR4("%s: Cannot destroy multicast status hash.\n", __func__);
  }

done:  
  MOSAL_mutex_rel(&mcgm->mtx);
  MOSAL_mutex_free(&mcgm->mtx);
  FREE(mcgm);
  MT_RETURN(ret);

}

/************************************************************************
   Function: THH_mcgm_attach_qp
   
    Arguments:
    mcgm 
    qpn -QP number of QP to attach 
    mgid -GID of a multicast group to attach to
    
    Returns:
    HH_OK 
    HH_EINVAL 
    HH_EAGAIN - No more MGHT entries. 
    HH_2BIG_MCG_SIZE - Number of QPs attached to multicast groups exceeded") \
    HH_ERR - an error has ocuured
    
    Description: 
    Attach given QP to multicast with given DGID. Add new group if this is the first QP in MCG.
  ************************************************************************/
extern HH_ret_t THH_mcgm_attach_qp(/*IN */ THH_mcgm_t mcgm, 
                                   /*IN */ IB_wqpn_t qpn, 
                                   /*IN */ IB_gid_t mgid )
{
  THH_mcg_entry_t fw_tmp_entry;
  THH_cmd_status_t c_status;
  mcg_status_entry_t my_entry;
  HH_ret_t ret,s_ret = HH_OK;
  THH_mcg_hash_t fw_hash_val;
  u_int32_t new_idx,i;
  MT_bool new_e = TRUE;
  MT_bool is_empty=TRUE;
 
  FUNC_IN;

  ret=HH_EINVAL;
  THMCG_CHECK_NULL(mcgm,fin);
  ret=HH_OK;

  MOSAL_mutex_acq_ui(&mcgm->mtx);

  MTL_DEBUG1("got gid:%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.\n",mgid[0],mgid[1],
             mgid[2],mgid[3],mgid[4],mgid[5],mgid[6],mgid[7],
             mgid[8],mgid[9],mgid[10],mgid[11],mgid[12],mgid[13],
             mgid[14],mgid[15]); 

  memset(&my_entry,0,sizeof(mcg_status_entry_t));
  s_ret = THMCG_get_status_entry(mgid,&(mcgm->my_hash),&my_entry);
  switch (s_ret) {
  
  case HH_NO_MCG:
        MTL_DEBUG1(MT_FLFMT("gid doesn't have a multicast group yet  \n"));
                
        //get hash idx from fw
        c_status = THH_cmd_MGID_HASH(mcgm->cmd_if_h,mgid,&fw_hash_val);
        ret= HH_ERR;
        THMCG_CHECK_CMDIF_ERR(c_status,fin," THH_cmd_HASH  failed\n");
        ret= HH_OK;
    
        MTL_DEBUG1("returned hash val:0x%x \n",fw_hash_val);
        if (fw_hash_val >= mcgm->mght_hash_bins)
        {
            MTL_ERROR1("ERROR:got invalid hash idx for new gid\n");
            ret=HH_ERR;
            goto fin;
        }
    
        //read the entry where the new gid supposed to be- is it empty?
        ret= read_alloc_mgm(mcgm,&fw_tmp_entry,fw_hash_val);
        if (ret!= HH_OK) {
            goto fin;
        }
        
        for (i=0; i< 16; i++) {
            if (fw_tmp_entry.mgid[i] != 0) {
                is_empty= FALSE;
                break;
            }
        }
        if (!is_empty) /* if the hash isn't empty*/
        {                        
            /* put the gid in amgm */
          u_int32_t last=0;
          //mcgm->amgm_idx++;      
          MTL_DEBUG1("original hash idx is taken. before find last gid\n");
          if (fw_tmp_entry.next_gid_index > 0) {
              ret=THMCG_find_last_index(mcgm,fw_tmp_entry.next_gid_index,&last,&fw_tmp_entry);
              THMCG_CHECK_HH_ERR(ret,clean,"THMCG_find_last_index failed");
          }
                 
          //get free idx
          new_idx = (u_int32_t)epool_alloc(&mcgm->free_list);
          if (new_idx == EPOOL_NULL)
          {
            ret = HH_EAGAIN;
            THMCG_CHECK_HH_ERR(ret,clean,"THH_mcgm_attach_qp: $$ No free entries in MCGM table.\n");
          }
    
          MTL_DEBUG1("after allocating new idx: 0x%x in AMGM\n",new_idx);
          new_idx+=(u_int32_t)mcgm->mght_hash_bins; //abs idx in MGHT+AMGM
          
          //update the last entry
          fw_tmp_entry.next_gid_index = new_idx;
          c_status = THH_cmd_WRITE_MGM(mcgm->cmd_if_h,last,mcgm->max_qp_per_mcg,&fw_tmp_entry);
          ret= HH_ERR;
          THMCG_CHECK_CMDIF_ERR(c_status,clean," THH_cmd_WRITE_MGM  failed\n");
          ret= HH_OK;
          my_entry.prev_idx = last;
        
        }else{
            //mcgm->mght_idx++; 
            new_idx = (u_int32_t)fw_hash_val;
            my_entry.prev_idx = 0xffffffff;
            MTL_DEBUG1("using fw hash idx \n");
        }

        //either in MGHT or AMGM  - insert new GID entry
        my_entry.idx = new_idx;
        my_entry.num_valid_qps = 1;
        memcpy(my_entry.mgid,mgid,sizeof(IB_gid_t)); 
          
        fw_tmp_entry.next_gid_index=0;
        fw_tmp_entry.valid_qps=0;
        memset(fw_tmp_entry.qps, 0, sizeof(IB_wqpn_t) * mcgm->max_qp_per_mcg);   
        memcpy(fw_tmp_entry.mgid,mgid,sizeof(IB_gid_t)); 
      
        break;
  
  case HH_OK:
        MTL_DEBUG1("$$$ gid already exists in hash table\n");
        
           //read the entry
          ret=read_alloc_mgm(mcgm,&fw_tmp_entry,my_entry.idx);
          if (ret!= HH_OK) {
              goto fin;
          }
          if (fw_tmp_entry.valid_qps != my_entry.num_valid_qps) {
              MTL_ERROR1(MT_FLFMT("mismatch hw (%d)/sw (%d) MCG entry \n"),fw_tmp_entry.valid_qps,my_entry.num_valid_qps);
              ret= HH_ERR;
              goto clean;
          }
          
          //check if the qp already exists in the gid
          for (i=0; i<fw_tmp_entry.valid_qps; i++)
          {
            if (fw_tmp_entry.qps[i] == qpn)
            {
              MTL_DEBUG1("qpn already exists in the requested gid\n");
              ret=HH_OK; 
              goto clean;
            }
          }
        
          if (my_entry.num_valid_qps == mcgm->max_qp_per_mcg){
            ret=HH_2BIG_MCG_SIZE;
            MTL_ERROR1("exceeded mcgroup's max qps amount\n");
            goto clean;
          }
          
          my_entry.num_valid_qps ++;
          new_e = FALSE;
          break;
          
  default: MTL_ERROR1(MT_FLFMT("MCG_get_status_entry failed"));        
           goto fin;    
  }
  fw_tmp_entry.qps[fw_tmp_entry.valid_qps] = qpn;
  fw_tmp_entry.valid_qps ++;
  
  MTL_DEBUG1("writing new mcg entry to idx: 0x%x\n",my_entry.idx);
  c_status = THH_cmd_WRITE_MGM(mcgm->cmd_if_h,my_entry.idx,mcgm->max_qp_per_mcg,&fw_tmp_entry);
  ret=HH_ERR;
  THMCG_CHECK_CMDIF_ERR(c_status,clean," THH_cmd_WRITE_MGM  failed\n");
  ret=HH_OK;

  THMCG_update_status_entry(&mcgm->my_hash,&my_entry,new_e);

  //MTL_ERROR1("so far:     amgm:%d     mght:%d  \n",mcgm->amgm_idx,mcgm->mght_idx);
  
clean: 
    if (fw_tmp_entry.qps) {
        FREE(fw_tmp_entry.qps);
    }
fin:  
  MOSAL_mutex_rel(&mcgm->mtx);
  MT_RETURN(ret);
}



/************************************************************************
 *  Function: THH_mcgm_detach_qp
 *  
    Arguments:
    mcgm 
    qpn - QP number of QP to attach 
    mgid - GID of a multicast group to attach to 
    
    Returns:
    HH_OK 
    HH_EINVAL - No such multicast group or given QP is not in given group 
    HH_ERR - an error has occured
    
    Description: 
    Detach given QP from multicast group with given GID. 
    
 ************************************************************************/

extern HH_ret_t THH_mcgm_detach_qp(/*IN */ THH_mcgm_t mcgm, 
                                   /*IN */ IB_wqpn_t qpn, 
                                   /*IN */ IB_gid_t mgid)
{
  THH_mcg_entry_t fw_tmp_entry;
  THH_cmd_status_t c_status;
  mcg_status_entry_t my_entry;
  HH_ret_t ret = HH_OK;
  MT_bool qp_is_in = FALSE;
  u_int32_t i;

  FUNC_IN;

  ret=HH_ERR;
  THMCG_CHECK_NULL(mcgm,fin);
  ret=HH_OK;

  MOSAL_mutex_acq_ui(&mcgm->mtx);

  MTL_DEBUG1("got gid:%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.\n",mgid[0],mgid[1],
             mgid[2],mgid[3],mgid[4],mgid[5],mgid[6],mgid[7],
             mgid[8],mgid[9],mgid[10],mgid[11],mgid[12],mgid[13],
             mgid[14],mgid[15]); 
  MTL_DEBUG1("got qpn: 0x%x \n",qpn);

  ret = THMCG_get_status_entry(mgid,&(mcgm->my_hash),&my_entry);
  if (ret != HH_OK) {
    if (ret == HH_NO_MCG) {
        ret= HH_EINVAL_MCG_GID;
        MTL_ERROR1("this gid doesn't have a multicast group\n");
    }else {
    	MTL_ERROR1("MCG_get_status_entry failed");
    	}
    goto fin;
  }
  print_status_entry(&my_entry);

  //read the entry
  MTL_DEBUG1("reading from idx: 0x%x \n",my_entry.idx);
  
  ret=read_alloc_mgm(mcgm,&fw_tmp_entry,my_entry.idx);
  if (ret!= HH_OK) {
        goto fin;
  }
    
  for (i=0; i< fw_tmp_entry.valid_qps; i++)
  {
    if (fw_tmp_entry.qps[i] == qpn)
    {
      qp_is_in = TRUE;
      break;
    }
  }
  //qp is not in group
  if (qp_is_in == FALSE)
  {
    MTL_ERROR1("qp doesn't belong to gid's multicast group\n");
    ret=HH_EINVAL_QP_NUM;
    goto clean;
  }

  MTL_DEBUG1("MCGM detach qp: qp & gid are valid \n");
  
  if (fw_tmp_entry.valid_qps > 1)
  {
    MTL_DEBUG1("no need to remove gid's mcg \n");
    for (i=0; i< fw_tmp_entry.valid_qps; i++)
    {
        //assuming qp exists max once in mcg group
        if (fw_tmp_entry.qps[i] == qpn)
            fw_tmp_entry.qps[i]=fw_tmp_entry.qps[fw_tmp_entry.valid_qps-1];
    }
    fw_tmp_entry.valid_qps--;
        
    c_status = THH_cmd_WRITE_MGM(mcgm->cmd_if_h,my_entry.idx,mcgm->max_qp_per_mcg,&fw_tmp_entry);
    ret=HH_ERR;
    THMCG_CHECK_CMDIF_ERR(c_status,clean," THH_cmd_WRITE_MGM  failed\n");
    ret=HH_OK;

    //update my hash
    my_entry.num_valid_qps--;
    ret=THMCG_update_status_entry(&mcgm->my_hash,&my_entry,FALSE);
    THMCG_CHECK_HH_ERR(ret,clean," THMCG_update_status_entry  failed\n");
  }else {
    MTL_DEBUG1("no more qp's - removing MGM entry\n");
        
    //MGHT
    if (my_entry.idx < mcgm->mght_hash_bins)
    {
      MTL_DEBUG1("reduced entry is in MGHT \n");
      
      if (fw_tmp_entry.next_gid_index > 0) {
        ret=THMCG_reduce_MGHT(mcgm,my_entry.idx/*the entry to remove*/,&fw_tmp_entry);
        THMCG_CHECK_HH_ERR(ret,clean," THMCG_reduce_MGHT  failed\n");
        //mcgm->amgm_idx--;      
      }else{
         /* write nullified entry */
         memset(fw_tmp_entry.mgid,0,sizeof(IB_gid_t));   
         c_status = THH_cmd_WRITE_MGM(mcgm->cmd_if_h,my_entry.idx,mcgm->max_qp_per_mcg,&fw_tmp_entry);
         ret=HH_ERR;
         THMCG_CHECK_CMDIF_ERR(c_status,clean," THH_cmd_WRITE_MGM  failed\n");
         ret=HH_OK;
         //mcgm->mght_idx--;      
      }
    }
    //AMGM
    else
    {
        THH_mcg_entry_t fw_prev_entry;
        u_int32_t next_idx = fw_tmp_entry.next_gid_index;

        MTL_DEBUG1("reduced entry is in AMGM \n");
        //mcgm->amgm_idx--;      
        /*1.HW: update the prev  */
        ret=read_alloc_mgm(mcgm,&fw_prev_entry,my_entry.prev_idx);
        if (ret!= HH_OK) {
                goto clean;
        }
        
        fw_prev_entry.next_gid_index = fw_tmp_entry.next_gid_index;

        MTL_DEBUG1("writing updated prev to AMGM \n");
        c_status = THH_cmd_WRITE_MGM(mcgm->cmd_if_h,my_entry.prev_idx,mcgm->max_qp_per_mcg,&fw_prev_entry);
        FREE(fw_prev_entry.qps);
        
        ret=HH_ERR;
        THMCG_CHECK_CMDIF_ERR(c_status,clean," THH_cmd_WRITE_MGM  failed\n");
        ret=HH_OK;
        

      /* 2. read the gid of the next entry (if there's any)*/ 
        if (next_idx> 0) {
            mcg_status_entry_t next_entry;

            c_status = THH_cmd_READ_MGM(mcgm->cmd_if_h,next_idx,mcgm->max_qp_per_mcg,&fw_tmp_entry);
            ret=HH_ERR;
            THMCG_CHECK_CMDIF_ERR(c_status,clean," THH_cmd_WRITE_MCG  failed\n");
            ret=HH_OK;
              
        
              /* 2.1 update the prev field of the next in my DB*/
              ret=THMCG_get_status_entry(fw_tmp_entry.mgid,&mcgm->my_hash,&next_entry);
              THMCG_CHECK_HH_ERR(ret,clean," THMCG_get_status_entry  failed\n");  
              
              next_entry.prev_idx = my_entry.prev_idx;
              
              ret=THMCG_update_status_entry(&mcgm->my_hash,&next_entry,FALSE);
              THMCG_CHECK_HH_ERR(ret,clean," THMCG_get_status_entry  failed\n");  
        }
      
        epool_free(&mcgm->free_list,(unsigned long)((VAPI_size_t)my_entry.idx - mcgm->mght_hash_bins));
    
    }/* else - AMGM */
    
    /* remove the entry from my table */
    ret=THMCG_remove_status_entry(&mcgm->my_hash,&my_entry);
    THMCG_CHECK_HH_ERR(ret,clean," THMCG_remove_status_entry  failed\n");
    
  }
  
  clean:
      if (fw_tmp_entry.qps) {
        FREE(fw_tmp_entry.qps);
      }
  fin:
  MOSAL_mutex_rel(&mcgm->mtx);
  MT_RETURN(ret);
}



/***** static definitions **********************************/

/************************************************************************
 *  Function: THMCG_find_last_index
 *  
    Arguments:
    mcgm
    fw_hash_val - the MGHT idx to start with
    last- pointer to last index in chain
    last_entry - pointer to last fw entry that will be filled 
    Returns:
    HH_OK 
    HH_ERR - an error has occured
    
    Description: 
   finds the last fw entry in a chain of the sam hash key
 ************************************************************************/
static HH_ret_t THMCG_find_last_index(THH_mcgm_t mcgm, THH_mcg_hash_t start,
                                      u_int32_t* last,  THH_mcg_entry_t* last_entry)
{

  THH_cmd_status_t c_status;
  HH_ret_t ret = HH_EAGAIN;
  u_int32_t cur_idx=(u_int32_t)start;

  FUNC_IN;

    
  while (1)
  {
    MTL_DEBUG1("MCGM find last idx: reading from idx: 0x%x \n",cur_idx);
    memset(last_entry->qps, 0, sizeof(IB_wqpn_t) * mcgm->max_qp_per_mcg);   
    c_status = THH_cmd_READ_MGM(mcgm->cmd_if_h,cur_idx,mcgm->max_qp_per_mcg,last_entry);
    ret=HH_ERR;
    THMCG_CHECK_CMDIF_ERR(c_status,fin," THH_cmd_READ_MGM  failed\n");
    ret=HH_OK;
    print_fw_entry(last_entry);
    if (last_entry->next_gid_index==0) {
        *last=cur_idx;
        break;
    }
    cur_idx = last_entry->next_gid_index;
  }
    
  fin:
      MT_RETURN(ret);   
}

/************************************************************************
 *  Function: THMCG_update_status_entry
 *  
    Arguments:
    hash_p - status hash table
    mcg_p - the status entry to be updated
    new_e - is it a new entry (add) or not (update)
    Returns:
    HH_OK 
    HH_ERR - an error has occured
    
    Description: 
    updates or addes status entry
 ************************************************************************/

static HH_ret_t  THMCG_update_status_entry(mcg_status_hash_t* hash_p,mcg_status_entry_t* entry_p,MT_bool new_e)
{

  u_int32_t hash_key;
  VIP_common_ret_t hash_ret;
  VIP_hashp_value_t hash_val;
  mcg_status_entry_t* mgm_p;
    
  FUNC_IN;

  hash_key = GID_2_HASH_KEY(entry_p->mgid);

  if (!new_e)
  {
    hash_ret = VIP_hashp_erase(*hash_p, hash_key, &hash_val);
    if (hash_ret != VIP_OK)
    {
      MTL_ERROR1("failed VIP_hashp_erase: got %s \n",VAPI_strerror_sym(hash_ret));
      return HH_EINVAL;
    }
    mgm_p = (mcg_status_entry_t*)hash_val;
  }else {
      mgm_p = (mcg_status_entry_t*)MALLOC(sizeof(mcg_status_entry_t));
      if (mgm_p == NULL)
      {
          MT_RETURN(HH_EAGAIN);
      }
  }
  print_status_entry(entry_p);

  mgm_p->idx = entry_p->idx;
  mgm_p->prev_idx = entry_p->prev_idx;
  mgm_p->num_valid_qps = entry_p->num_valid_qps;
  memcpy(mgm_p->mgid,entry_p->mgid,sizeof(IB_gid_t)); 

  hash_ret = VIP_hashp_insert(*hash_p, hash_key, (VIP_hashp_value_t)mgm_p);
  if (hash_ret != VIP_OK)
  {
    MTL_ERROR1(MT_FLFMT("failed VIP_hashp_insert\n"));
    if (new_e) FREE(mgm_p);
    return HH_ERR;
  }

////////////////
#if 0
  /* try to find mcg of this mcg */
  hash_ret = VIP_hashp_find(*hash_p, hash_key, &hash_val);
  if (hash_ret != VIP_OK)
  {
    MTL_ERROR1("failed VIP_hashp_insert\n");
    return HH_ERR;
  }
  print_status_entry((mcg_status_entry_t*)hash_val);
#endif

////////////////


  MT_RETURN( HH_OK);
}

/************************************************************************
 *  Function: THMCG_get_status_entry
 *  
    Arguments:
    gid - the status entry gid
    hash_p - status hash table
    mcg_p - the status entry that will be filled    
    Returns:
    HH_OK 
    HH_ERR - an error has occured
    
    Description: 
    returns the entry which matches the gid    
 ************************************************************************/

static HH_ret_t THMCG_get_status_entry(IB_gid_t gid,
                                       mcg_status_hash_t* hash_p,
                                       mcg_status_entry_t* mcg_p)
{
  VIP_common_ret_t v_ret = VIP_OK;
  HH_ret_t ret = HH_OK;
  u_int32_t hash_key;
  VIP_hashp_value_t hash_val;

  FUNC_IN;

  hash_key = GID_2_HASH_KEY(gid);

  MTL_DEBUG2("%s: GID key is: %u\n", __func__, hash_key);

  /* try to find mcg of this mcg */
  v_ret = VIP_hashp_find(*hash_p, hash_key, &hash_val);

  switch (v_ret)
  {
  case VIP_OK:
    MTL_DEBUG1("returning HH OK\n"); 
    *mcg_p = *((mcg_status_entry_t*)hash_val);
    break;
  
  case VIP_EINVAL_HNDL:
    MTL_DEBUG1("returning HH NO MCG\n"); 
    ret= HH_NO_MCG;
    break;
  
  default:
    ret=HH_EAGAIN;
  }
   
  return ret;
}

/************************************************************************
 *  Function: THMCG_reduce_MGHT
 *  
    Arguments:
    mcgm
    cur_idx - entry idx in FW table 
    fw_entry_p - pointer to fw_hash_entry that reduced from MGHT 
    
    Returns:
    HH_OK 
    HH_ERR - an error has occured
    
    Description: 
    removes entry from MGHT, moves its succ to the entry's place in MGHT and updates status hash 
    
 ************************************************************************/

static HH_ret_t THMCG_reduce_MGHT(THH_mcgm_t mcgm, u_int32_t cur_idx,  THH_mcg_entry_t* fw_entry_p)
{
  THH_mcg_entry_t fw_next_entry;
  THH_cmd_status_t c_status;
  mcg_status_entry_t my_entry;
  HH_ret_t ret = HH_OK;
    
  FUNC_IN;

  //read the next entry & write it instead the removed one
  ret=read_alloc_mgm(mcgm,&fw_next_entry,fw_entry_p->next_gid_index);
  if (ret!= HH_OK) {
       goto fin;
  }

  c_status = THH_cmd_WRITE_MGM(mcgm->cmd_if_h,cur_idx,mcgm->max_qp_per_mcg,&fw_next_entry);
  FREE(fw_next_entry.qps);
  
  ret=HH_ERR;
  THMCG_CHECK_CMDIF_ERR(c_status,fin," THH_cmd_WRITE_MGM  failed\n");
  ret=HH_OK;

    //free the next idx in epool
    epool_free(&mcgm->free_list,(unsigned long)((VAPI_size_t)fw_entry_p->next_gid_index - mcgm->mght_hash_bins));

    //update the next's entry
    memcpy(my_entry.mgid,fw_next_entry.mgid,sizeof(IB_gid_t)); 
    my_entry.idx = cur_idx;
    my_entry.num_valid_qps = fw_next_entry.valid_qps;
    my_entry.prev_idx = 0xffffffff;
    ret=THMCG_update_status_entry(&mcgm->my_hash,&my_entry,FALSE);
    THMCG_CHECK_HH_ERR(ret,fin," THMCG_update_status_entry  failed\n");
    print_status_entry(&my_entry);
  

  fin:
  MT_RETURN(ret);
}

/************************************************************************
 *  Function: THMCG_remove_status_entry
 *  
    Arguments:
    hash_p - pointer to status hash 
    entry_p - pointer to entry that should be removed 
    
    Returns:
    HH_OK 
    HH_ERR - an error has occured
    
    Description: 
    removes enty from status hash 
    
 ************************************************************************/

static HH_ret_t  THMCG_remove_status_entry(mcg_status_hash_t* hash_p,mcg_status_entry_t* entry_p)
{

  u_int32_t hash_key;
  VIP_common_ret_t hash_ret;
  VIP_hashp_value_t hash_val;
  HH_ret_t ret = HH_OK;

  FUNC_IN;

  hash_key = GID_2_HASH_KEY(entry_p->mgid);

  hash_ret = VIP_hashp_erase(*hash_p, hash_key, &hash_val);
  if (hash_ret != VIP_OK)
  {
    MTL_ERROR1("failed VIP_hashp_erase\n");
    ret= HH_ERR;
  }
  FREE(hash_val);
  MT_RETURN(ret);
}

static inline void print_fw_entry(THH_mcg_entry_t* entry)
{
  u_int32_t i;
  MTL_DEBUG1("--- FW ENTRY ----\n");

  MTL_DEBUG1("gid:%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.\n",entry->mgid[0],entry->mgid[1],
             entry->mgid[2],entry->mgid[3],entry->mgid[4],entry->mgid[5],entry->mgid[6],entry->mgid[7],
             entry->mgid[8],entry->mgid[9],entry->mgid[10],entry->mgid[11],entry->mgid[12],entry->mgid[13],
             entry->mgid[14],entry->mgid[15]); 

  MTL_DEBUG1("num of qps:0x%x \n",entry->valid_qps);
  if (entry->qps) {
      for (i=0; i< entry->valid_qps ; i++)
      {MTL_DEBUG1("qpn:0x%x ",entry->qps[i]);} 
  }else {MTL_DEBUG1("-... no qps ...-\n");}
  MTL_DEBUG1("next gid idx:%d \n",entry->next_gid_index);
  MTL_DEBUG1("--------------\n");
}

static inline void print_status_entry(mcg_status_entry_t* my_entry)
{
  MTL_DEBUG1("--- STATUS ENTRY --\n");
  MTL_DEBUG1("gid:%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.\n",my_entry->mgid[0],my_entry->mgid[1],
             my_entry->mgid[2],my_entry->mgid[3],my_entry->mgid[4],my_entry->mgid[5],my_entry->mgid[6],
             my_entry->mgid[7],my_entry->mgid[8],my_entry->mgid[9],my_entry->mgid[10],my_entry->mgid[11],
             my_entry->mgid[12],my_entry->mgid[13],my_entry->mgid[14],my_entry->mgid[15]); 

  MTL_DEBUG1("\nnum of qps:0x%x\n",my_entry->num_valid_qps);
  MTL_DEBUG1("gid_idx:0x%x  prev_idx:0x%x \n",my_entry->idx,my_entry->prev_idx);
  MTL_DEBUG1("------------------\n");
}

static inline HH_ret_t read_alloc_mgm(THH_mcgm_t mcgm, THH_mcg_entry_t* entry,u_int32_t idx)
{
    THH_cmd_status_t c_status= THH_CMD_STAT_OK;
    entry->qps = TNMALLOC(IB_wqpn_t,mcgm->max_qp_per_mcg);  
    if (entry->qps == NULL) {                  
        MTL_ERROR1(MT_FLFMT("Null pointer detected\n"));   
        MT_RETURN(HH_EAGAIN);                      
    }
    memset(entry->qps, 0, sizeof(IB_wqpn_t) * mcgm->max_qp_per_mcg);  
    c_status = THH_cmd_READ_MGM(mcgm->cmd_if_h,idx,mcgm->max_qp_per_mcg,entry); 
    if (c_status != THH_CMD_STAT_OK) {                  
        FREE(entry->qps);
        entry->qps = NULL;
        MTL_ERROR1(MT_FLFMT("failed READ_MGM\n"));   
        MT_RETURN(HH_ERR);
    }
    print_fw_entry(entry);
    MT_RETURN(HH_OK);
}

HH_ret_t THH_mcgm_get_num_mcgs(THH_mcgm_t mcgm, u_int32_t *num_mcgs_p)
{
    THMCG_CHECK_NULL(mcgm,done);
    *num_mcgs_p = VIP_hashp_get_num_of_objects(mcgm->my_hash);
    MT_RETURN(HH_OK);

done:
    MT_RETURN(HH_EINVAL);
}
