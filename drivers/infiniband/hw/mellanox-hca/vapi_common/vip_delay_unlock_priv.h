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
#ifndef VIP_COMMON_VIP_DELAY_UNLOCK_PRIV_H
#define VIP_COMMON_VIP_DELAY_UNLOCK_PRIV_H

#include <vapi_types.h>
#include <mosal.h>
#include <vip_delay_unlock.h>
 
typedef struct VIP_delay_unlock_elem_st {
     MOSAL_iobuf_t iobuf;
     struct VIP_delay_unlock_elem_st * next;
} VIP_delay_unlock_elem_t;

 
struct VIP_delay_unlock_st {
     struct VIP_delay_unlock_elem_st *  list_start;
     MT_bool  is_valid;
     MOSAL_spinlock_t    spl;
};
 
#endif



