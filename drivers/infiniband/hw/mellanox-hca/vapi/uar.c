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

#define C_UAR_C
#include "uar.h"
#include <MT23108.h>

struct THH_uar_st {
  THH_ver_info_t  ver_info;
  THH_uar_index_t   uar_index;
  volatile u_int32_t *uar_base;
  MOSAL_spinlock_t  uar_lock;
} /* *THH_uar_t */;

/* Doorbells dword offsets */
#define UAR_RD_SEND_DBELL_OFFSET (MT_BYTE_OFFSET(tavorprm_uar_st,rd_send_doorbell)>>2)
#define UAR_SEND_DBELL_OFFSET (MT_BYTE_OFFSET(tavorprm_uar_st,send_doorbell)>>2)
#define UAR_RECV_DBELL_OFFSET (MT_BYTE_OFFSET(tavorprm_uar_st,receive_doorbell)>>2)
#define UAR_CQ_DBELL_OFFSET (MT_BYTE_OFFSET(tavorprm_uar_st,cq_command_doorbell)>>2)
#define UAR_EQ_DBELL_OFFSET (MT_BYTE_OFFSET(tavorprm_uar_st,eq_command_doorbell)>>2)
/* Doorbells dword offsets */
#define UAR_RD_SEND_DBELL_SZ (MT_BYTE_SIZE(tavorprm_uar_st,rd_send_doorbell)>>2)
#define UAR_SEND_DBELL_SZ (MT_BYTE_SIZE(tavorprm_uar_st,send_doorbell)>>2)
#define UAR_RECV_DBELL_SZ (MT_BYTE_SIZE(tavorprm_uar_st,receive_doorbell)>>2)
#define UAR_CQ_DBELL_SZ (MT_BYTE_SIZE(tavorprm_uar_st,cq_command_doorbell)>>2)
#define UAR_EQ_DBELL_SZ (MT_BYTE_SIZE(tavorprm_uar_st,eq_command_doorbell)>>2)

/* doorbell ringing for 2 dwords */
#ifdef __MOSAL_MMAP_IO_WRITE_QWORD_ATOMIC__
/* If Qword write is assured to be atomic (MMX or a 64-bit arch) - no need for the spinlock */
#define RING_DBELL_2DW(uar,dword_offset,dbell_draft)                                       \
  dbell_draft[0]= MOSAL_cpu_to_be32(dbell_draft[0]);                                       \
  dbell_draft[1]= MOSAL_cpu_to_be32(dbell_draft[1]);                                       \
  MOSAL_MMAP_IO_WRITE_QWORD(uar->uar_base+dword_offset,*(volatile u_int64_t*)dbell_draft); 
#else
#define RING_DBELL_2DW(uar,dword_offset,dbell_draft)                                       \
  MOSAL_spinlock_irq_lock(&(uar->uar_lock));                                               \
  MOSAL_MMAP_IO_WRITE_DWORD(uar->uar_base+dword_offset,MOSAL_cpu_to_be32(dbell_draft[0])); \
  MOSAL_MMAP_IO_WRITE_DWORD(uar->uar_base+dword_offset+1,MOSAL_cpu_to_be32(dbell_draft[1])); \
  MOSAL_spinlock_unlock(&(uar->uar_lock));
#endif /* Atomic Qword write */

#define RING_DBELL_4DW(uar,dword_offset,dbell_draft)                                  \
  dbell_draft[0]= MOSAL_cpu_to_be32(dbell_draft[0]);                                  \
  dbell_draft[1]= MOSAL_cpu_to_be32(dbell_draft[1]);                                  \
  dbell_draft[2]= MOSAL_cpu_to_be32(dbell_draft[2]);                                  \
  dbell_draft[3]= MOSAL_cpu_to_be32(dbell_draft[3]);                                  \
  MOSAL_spinlock_irq_lock(&(uar->uar_lock));                                          \
  MOSAL_MMAP_IO_WRITE_QWORD(uar->uar_base+dword_offset,((u_int64_t*)dbell_draft)[0]); \
  MOSAL_MMAP_IO_WRITE_QWORD(uar->uar_base+dword_offset+2,((u_int64_t*)dbell_draft)[1]); \
  MOSAL_spinlock_unlock(&(uar->uar_lock));



/************************************************************************/
/*                    Public functions                                  */ 
/************************************************************************/

HH_ret_t  THH_uar_create(
  /*IN*/  THH_ver_info_t  *version_p, 
  /*IN*/  THH_uar_index_t uar_index, 
  /*IN*/  void            *uar_base, 
  /*OUT*/ THH_uar_t       *uar_p
)
{
  THH_uar_t new_uar;

  new_uar= (THH_uar_t)MALLOC(sizeof(struct THH_uar_st));
  if (new_uar == NULL) {
    MTL_ERROR1("THH_uar_create: Failed allocating object memory.\n");
    return HH_EAGAIN;
  }
  new_uar->uar_index= uar_index;
  new_uar->uar_base= (volatile u_int32_t*)uar_base;
  memcpy(&(new_uar->ver_info),version_p,sizeof(THH_ver_info_t));
  MOSAL_spinlock_init(&(new_uar->uar_lock));
  
  *uar_p= new_uar;
  return HH_OK;
} /* THH_uar_create */


HH_ret_t THH_uar_destroy(THH_uar_t uar /* IN */)
{
  FREE(uar);
  return HH_OK;
} /* THH_uar_destroy */


HH_ret_t THH_uar_get_index(
  /*IN*/ THH_uar_t uar, 
  /*OUT*/ THH_uar_index_t *uar_index_p
)
{
  if (uar == NULL)  return HH_EINVAL;
  *uar_index_p= uar->uar_index;
  return HH_OK;
}


HH_ret_t  THH_uar_sendq_dbell(
  THH_uar_t              uar,          /* IN */
  THH_uar_sendq_dbell_t* sendq_dbell_p /* IN */ )
{
  volatile u_int32_t dbell_draft[UAR_SEND_DBELL_SZ]= {0};

//  MTL_DEBUG4(MT_FLFMT("SendQ Dbell: qpn=0x%X nda=0x%X nds=0x%X nopcode=0x%X"),
//    sendq_dbell_p->qpn, sendq_dbell_p->next_addr_32lsb,
//    sendq_dbell_p->next_size, sendq_dbell_p->nopcode);

  dbell_draft[MT_BYTE_OFFSET(tavorprm_send_doorbell_st,nda)>>2]=
    sendq_dbell_p->next_addr_32lsb; /* 6 ls-bits will be masked anyway by f and nopcode */
  MT_INSERT_ARRAY32(dbell_draft, sendq_dbell_p->fence ? 1 : 0,
    MT_BIT_OFFSET(tavorprm_send_doorbell_st,f),
    MT_BIT_SIZE(tavorprm_send_doorbell_st,f));
  MT_INSERT_ARRAY32(dbell_draft, sendq_dbell_p->nopcode,
    MT_BIT_OFFSET(tavorprm_send_doorbell_st,nopcode),
    MT_BIT_SIZE(tavorprm_send_doorbell_st,nopcode));
  MT_INSERT_ARRAY32(dbell_draft, sendq_dbell_p->qpn,
    MT_BIT_OFFSET(tavorprm_send_doorbell_st,qpn),
    MT_BIT_SIZE(tavorprm_send_doorbell_st,qpn));
  MT_INSERT_ARRAY32(dbell_draft, sendq_dbell_p->next_size,
    MT_BIT_OFFSET(tavorprm_send_doorbell_st,nds),
    MT_BIT_SIZE(tavorprm_send_doorbell_st,nds));

  RING_DBELL_2DW(uar,UAR_SEND_DBELL_OFFSET,dbell_draft);

  return HH_OK;
} /* THH_uar_sendq_dbell */


HH_ret_t  THH_uar_sendq_rd_dbell(
  THH_uar_t               uar,            /* IN */
  THH_uar_sendq_dbell_t*  sendq_dbell_p,  /* IN */
  IB_eecn_t               een             /* IN */)
{
  volatile u_int32_t dbell_draft[UAR_RD_SEND_DBELL_SZ]= {0};
  
  MT_INSERT_ARRAY32(dbell_draft, een,
    MT_BIT_OFFSET(tavorprm_rd_send_doorbell_st,een),
    MT_BIT_SIZE(tavorprm_rd_send_doorbell_st,qpn));
  MT_INSERT_ARRAY32(dbell_draft, sendq_dbell_p->qpn,
    MT_BIT_OFFSET(tavorprm_rd_send_doorbell_st,qpn),
    MT_BIT_SIZE(tavorprm_rd_send_doorbell_st,qpn));
  dbell_draft[MT_BYTE_OFFSET(tavorprm_rd_send_doorbell_st,snd_params.nda)>>2]=
    sendq_dbell_p->next_addr_32lsb; /* 6 ls-bits will be masked anyway by f and nopcode */
  MT_INSERT_ARRAY32(dbell_draft, sendq_dbell_p->fence ? 1 : 0,
    MT_BIT_OFFSET(tavorprm_rd_send_doorbell_st,snd_params.f),
    MT_BIT_SIZE(tavorprm_rd_send_doorbell_st,snd_params.f));
  MT_INSERT_ARRAY32(dbell_draft, sendq_dbell_p->nopcode,
    MT_BIT_OFFSET(tavorprm_rd_send_doorbell_st,snd_params.nopcode),
    MT_BIT_SIZE(tavorprm_rd_send_doorbell_st,snd_params.nopcode));
  MT_INSERT_ARRAY32(dbell_draft, sendq_dbell_p->qpn,
    MT_BIT_OFFSET(tavorprm_rd_send_doorbell_st,snd_params.qpn),
    MT_BIT_SIZE(tavorprm_rd_send_doorbell_st,snd_params.qpn));
  MT_INSERT_ARRAY32(dbell_draft, sendq_dbell_p->next_size,
    MT_BIT_OFFSET(tavorprm_rd_send_doorbell_st,snd_params.nds),
    MT_BIT_SIZE(tavorprm_rd_send_doorbell_st,snd_params.nds));

  RING_DBELL_4DW(uar,UAR_RD_SEND_DBELL_OFFSET,dbell_draft);

  return HH_OK;
} /* THH_uar_sendq_rd_dbell */


HH_ret_t THH_uar_recvq_dbell(
  THH_uar_t              uar,           /* IN */
  THH_uar_recvq_dbell_t* recvq_dbell_p  /* IN */)
{
  volatile u_int32_t dbell_draft[UAR_RECV_DBELL_SZ]= {0};
  
    /* nda field in the Doorbell is actually nda[31:6] - so we must shift it right before inse*/
  dbell_draft[MT_BYTE_OFFSET(tavorprm_receive_doorbell_st,nda) >> 2]= 
    ( recvq_dbell_p->next_addr_32lsb & (~MASK32(6)) ) |
    ( recvq_dbell_p->next_size & MASK32(6) );
  dbell_draft[MT_BYTE_OFFSET(tavorprm_receive_doorbell_st,qpn) >> 2]= 
    ( recvq_dbell_p->qpn << (MT_BIT_OFFSET(tavorprm_receive_doorbell_st,qpn) & MASK32(5)) ) |
    recvq_dbell_p->credits;

  RING_DBELL_2DW(uar,UAR_RECV_DBELL_OFFSET,dbell_draft);

  return HH_OK;
} /* THH_uar_recvq_dbell */


HH_ret_t THH_uar_cq_cmd(
  THH_uar_t         uar,    /* IN */
  THH_uar_cq_cmd_t  cmd,    /* IN */
  HH_cq_hndl_t      cqn,    /* IN */
  u_int32_t         param   /* IN */)
{
  volatile u_int32_t dbell_draft[UAR_CQ_DBELL_SZ]= {0};
  
  MT_INSERT_ARRAY32(dbell_draft, cmd,
    MT_BIT_OFFSET(tavorprm_cq_cmd_doorbell_st,cq_cmd),
    MT_BIT_SIZE(tavorprm_cq_cmd_doorbell_st,cq_cmd));
  MT_INSERT_ARRAY32(dbell_draft, cqn,
    MT_BIT_OFFSET(tavorprm_cq_cmd_doorbell_st,cqn),
    MT_BIT_SIZE(tavorprm_cq_cmd_doorbell_st,cqn));
  MT_INSERT_ARRAY32(dbell_draft, param,
    MT_BIT_OFFSET(tavorprm_cq_cmd_doorbell_st,cq_param),
    MT_BIT_SIZE(tavorprm_cq_cmd_doorbell_st,cq_param));

  RING_DBELL_2DW(uar,UAR_CQ_DBELL_OFFSET,dbell_draft);

  return HH_OK;
} /* THH_uar_cq_cmd */


HH_ret_t THH_uar_eq_cmd(
  THH_uar_t         uar,  /* IN */
  THH_uar_eq_cmd_t  cmd,  /* IN */
  THH_eqn_t         eqn,  /* IN */
  u_int32_t         param /* IN */)
{
   volatile u_int32_t dbell_draft[UAR_EQ_DBELL_SZ]= {0};
  
  MT_INSERT_ARRAY32(dbell_draft, cmd,
    MT_BIT_OFFSET(tavorprm_eq_cmd_doorbell_st,eq_cmd),
    MT_BIT_SIZE(tavorprm_eq_cmd_doorbell_st,eq_cmd));
  MT_INSERT_ARRAY32(dbell_draft, eqn,
    MT_BIT_OFFSET(tavorprm_eq_cmd_doorbell_st,eqn),
    MT_BIT_SIZE(tavorprm_eq_cmd_doorbell_st,eqn));
  MT_INSERT_ARRAY32(dbell_draft, param,
    MT_BIT_OFFSET(tavorprm_eq_cmd_doorbell_st,eq_param),
    MT_BIT_SIZE(tavorprm_eq_cmd_doorbell_st,eq_param));
#if 2 <= MAX_DEBUG
  { u_int32_t i;
    for (i=0;i<UAR_EQ_DBELL_SZ;i=+2) {
      MTL_DEBUG2("%s EQ DB: %08X %08X \n", __func__, dbell_draft[i],dbell_draft[i+1] ); 
    }
  }
#endif
  RING_DBELL_2DW(uar,UAR_EQ_DBELL_OFFSET,dbell_draft);

  return HH_OK;
} /* THH_uar_eq_cmd */


HH_ret_t THH_uar_blast(  
  THH_uar_t  uar,    /* IN */
  void*      wqe_p,  /* IN */
  MT_size_t  wqe_sz, /* IN */
  THH_uar_sendq_dbell_t	*sendq_dbell_p,  /* IN */
  IB_eecn_t	een                          /* IN */)
{
  return HH_ENOSYS;  /* Not supported */
} /* THH_uar_bf */


  
