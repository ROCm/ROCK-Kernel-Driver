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
#ifndef H_MOSAL_MLOCK_H
#define H_MOSAL_MLOCK_H

#include <mosal_iobuf.h>

typedef struct MOSAL_mlock_ctx_st *MOSAL_mlock_ctx_t;

#if !defined(__DARWIN__)
/******************************************************************************
 *
 * Function: MOSAL_mlock
 *
 * Arguments:
 *          addr (IN) - Base of the region
 *          size (IN) - Size of the region
 *
 * Returns:
 *  MT_OK, 
 *  appropriate error code otherwise
 *
 * Description:
 *   Locks memory region.
 *
 ********************************************************************************/
call_result_t MOSAL_mlock(MT_virt_addr_t addr, MT_size_t size);

/********************************************************************************
 * Function: MOSAL_munlock
 *
 * Arguments:
 *          addr (IN) - Base of the region
 *          size (IN) - Size of the region
 *
 * Returns:
 *  MT_OK, 
 *  appropriate error code otherwise
 *
 * Description:
 *   Unlocks memory region.
 *
 ********************************************************************************/
call_result_t MOSAL_munlock(MT_virt_addr_t addr, MT_size_t size);


/********************************************************************************
 * Function: (kernel-mode only): MOSAL_mlock_ctx_init
 *
 * Arguments:
 *   mlock_ctx_p (OUT): pointer to return mlock context in.
 *
 * Returns:
 *  MT_OK, 
 *  appropriate error code otherwise
 *
 * Description:
 *  Initiailize MOSAL_mlock context for this process
 *  This function should only be called by MOSAL wrapper "open" entry point.
 *  Returned context should be saved in file descriptor (private)
 ********************************************************************************/
call_result_t MOSAL_mlock_ctx_init(MOSAL_mlock_ctx_t *mlock_ctx_p);

/********************************************************************************
 * Function: (kernel-mode only): MOSAL_mlock_ctx_cleanup
 *
 * Arguments:
 *   mlock_ctx (IN): mlock context allocated with MOSAL_init_mlock_ctx
 * Returns:
 *
 * Description:
 *  Free MOSAL_mlock context for this process
 *  This function should be invoked by the "close" entry point of MOSAL wrapper 
 ********************************************************************************/
call_result_t MOSAL_mlock_ctx_cleanup(MOSAL_mlock_ctx_t mlock_ctx);

/********************************************************************************
 * Function: (kernel-mode only): MOSAL_mlock_iobuf
 *
 * Arguments:
 *   va(IN): start address of the region to be locked
 *   size(IN): size of the area to be locked
 *   iobuf(IN/OUT): iobuf associated with this area
 *   page_shift(IN): page shift of the va
 * Returns:
 *
 ********************************************************************************/
call_result_t MOSAL_mlock_iobuf(MT_virt_addr_t addr, MT_size_t size, MOSAL_iobuf_t mosal_iobuf,
                                unsigned int page_shift);

/********************************************************************************
 * Function: (kernel-mode only): MOSAL_munlock_iobuf
 *
 * Arguments:
 *   va(IN): start address of the region to be unlocked
 *   size(IN): size of the area to be unlocked
 *   iobuf(IN/OUT): iobuf associated with this area
 *   page_shift(IN): page shift of the va
 * Returns:
 *
 ********************************************************************************/
call_result_t MOSAL_munlock_iobuf(MT_virt_addr_t addr, MT_size_t size, MOSAL_iobuf_t mosal_iobuf,
                                  unsigned int page_shift);

#endif /* !defined(__DARWIN__) */

#endif /*__MOSAL_MLOCK_H__*/
