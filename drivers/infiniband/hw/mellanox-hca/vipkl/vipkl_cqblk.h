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

#ifndef H_VIPKL_POLL_BLK_H
#define H_VIPKL_POLL_BLK_H

#define VIPKL_CQBLK_INVAL_HNDL 0xFFFFFFFF

typedef u_int32_t VIPKL_cqblk_hndl_t;

/* Initialization for this module - return false if failed */
MT_bool VIPKL_cqblk_init(void);

void VIPKL_cqblk_cleanup(void);

VIP_ret_t VIPKL_cqblk_alloc_ctx(
    VIP_RSCT_t            usr_ctx,
    /*IN*/VIP_hca_hndl_t   vip_hca,
  /*IN*/CQM_cq_hndl_t    vipkl_cq,
  /*OUT*/VIPKL_cqblk_hndl_t *cqblk_hndl_p);

VIP_ret_t VIPKL_cqblk_free_ctx(VIP_RSCT_t  usr_ctx,VIP_hca_hndl_t vip_hca,/*IN*/VIPKL_cqblk_hndl_t cqblk_hndl);


VIP_ret_t VIPKL_cqblk_wait(
    VIP_RSCT_t  usr_ctx,VIP_hca_hndl_t vip_hca,
    /*IN*/VIPKL_cqblk_hndl_t cq_blk_hndl,
  /*IN*/MT_size_t timeout_usec);

VIP_ret_t VIPKL_cqblk_signal(VIP_RSCT_t  usr_ctx,VIP_hca_hndl_t vip_hca,/*IN*/VIPKL_cqblk_hndl_t cqblk_hndl);

#endif
