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

#ifndef H_VIP_COMMON_H
#define H_VIP_COMMON_H

#include <mtl_common.h>
#include <vapi_types.h>
#include <vapi_common.h>

enum VIP_common_ret {
  /* Remap VAPI return codes */

  /* Non error return values */
  VIP_OK                    = VAPI_OK,

  /* General errors */
  VIP_EGEN                  = VAPI_EGEN,    /* general error for entire stack */
  VIP_EAGAIN                = VAPI_EAGAIN,  /* Not enough resources (try again later...) */
  VIP_EBUSY                 = VAPI_EBUSY,  /* Resource is in use */
  VIP_ETIMEDOUT             = VAPI_ETIMEOUT, /* Operation timedout */
  VIP_EINTR                 = VAPI_EINTR,   /* Interrupted blocking function (operation not completed) */
  VIP_EFATAL                = VAPI_EFATAL, /* catastrophic error */
  VIP_ENOMEM                = VAPI_ENOMEM,  /* Invalid address of exhausted physical memory quota */
  VIP_EPERM                 = VAPI_EPERM,  /* Not enough permissions */
  VIP_ENOSYS                = VAPI_ENOSYS,/* Operation/option not supported */
  VIP_ESYSCALL              = VAPI_ESYSCALL, /* Error in underlying O/S call */
  VIP_EINVAL_PARAM          = VAPI_EINVAL_PARAM, /* invalid parameter*/
  
  /*******************************************************/
  /* VIP specific errors */

  VIP_COMMON_ERROR_MIN      = VAPI_ERROR_MAX,  /* Dummy error code: put this VIP error code first */

  /* General errors */
  VIP_EINVAL_HNDL,          /* Invalid (no such) handle */

  VIP_COMMON_ERROR_MAX             /* Dummy max error code : put all error codes before this */
};

typedef int32_t VIP_common_ret_t;

static inline const char* VIP_common_strerror_sym( VIP_common_ret_t errnum)
{
    if (errnum <= VAPI_ERROR_MAX) {
        return VAPI_strerror_sym(errnum);
    } else if (errnum == VIP_EINVAL_HNDL) {
        return "VIP_EINVAL_HNDL";
    } else {
        return "VIP_COMMON_UNKNOWN_ERROR";
    }
}

/* Memory mgmt functions */

/********************************************************************************
 * Function type: VIP_allocator_malloc_t (not used for now)
 *
 * Arguments:
 *  size (IN) - allocate this number of bytes on the heap
 *
 * Returns:
 *  pointer to allocated memory.
 *  0 if resources were unavailable
 *
 * Description:
 *   allocate given amount of bytes memory
 *
 ********************************************************************************/
typedef void* (VIP_allocator_malloc_t)(size_t size);
/********************************************************************************
 * Function type: VIP_allocator_free_t
 *
 * Arguments:
 *  void* (IN) - deallocate memory at this location
 *
 * Returns:
 *  void
 *
 * Description:
 *   deallocate memory at a given location
 *
 ********************************************************************************/
typedef void (*VIP_allocator_free_t)(void *);


#endif
