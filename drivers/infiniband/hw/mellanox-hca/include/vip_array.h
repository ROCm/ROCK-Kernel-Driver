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
 

#ifndef VIP_COMMON_VIP_ARRAY_H
#define VIP_COMMON_VIP_ARRAY_H

#include <mtl_common.h>
#include "vip_common.h"

typedef u_int32_t VIP_array_handle_t;
typedef void* VIP_array_obj_t;

struct VIP_array_t;
typedef struct VIP_array_t* VIP_array_p_t;

#ifdef  __cplusplus
 extern "C" {
#endif

/********************************************************************************
 * Function: VIP_array_create_maxsize
 *
 * Arguments:
 *  
 *  size (IN) - initial size. Must be multiple of 8.
 *  maxsize (IN) - max number of elements that the array will hold
 *
 *  VIP_array_p (OUT) - Return new VIP_array object here
 *                       Set to NULL in case of an error
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_AGAIN: Not enough resources
 *
 * Description:
 *   Create a new VIP_array table
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_create_maxsize(u_int32_t size, u_int32_t maxsize, VIP_array_p_t* VIP_array_p);

/********************************************************************************
 * Function: VIP_array_create
 *
 * Arguments:
 *  
 *  size (IN) - initial size. Must be multiple of 8.
 *
 *  VIP_array_p (OUT) - Return new VIP_array object here
 *                       Set to NULL in case of an error
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_AGAIN: Not enough resources
 *
 * Description:
 *   Create a new VIP_array table
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_create(u_int32_t size, VIP_array_p_t* VIP_array_p);


/********************************************************************************
 * Function: VIP_array_destroy
 *
 * Arguments:
 *  VIP_array (IN) - Object to destroy
 *  free_objects_fun (IN) - If non zero, call this function
 *                          for each object in the array (can be used
 *                          e.g. to deallocate memory).
 *                          Even if zero, the array is still deallocated.
 *
 * Returns:
 *  VIP_OK, 
 *
 * Description:
 *   cleanup resources for a VIP_array table
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_destroy(VIP_array_p_t VIP_array, 
    VIP_allocator_free_t free_objects_fun);

/********************************************************************************
 * Function: VIP_array_insert
 *
 * Arguments:
 *  VIP_array (IN) - Insert in this table
 *  obj (IN) - object to insert
 *  handle_p (OUT) - handle for this object
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EAGAIN: not enough resources
 *
 * Description:
 *   Inset given object to array
 *   Return the handle associated with this object.
 *   Note: No check is done that the object is not already
 *   in the array.
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_insert(VIP_array_p_t VIP_array, VIP_array_obj_t obj,
  VIP_array_handle_t* handle_p );

/********************************************************************************
 * Function: VIP_array_insert2hndl
 *
 * Arguments:
 *  VIP_array (IN) - Insert in this table
 *  obj (IN) - object to insert
 *  hndl (IN) - Requested handle for this object
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EBUSY: Given handle is already in use
 *  VIP_EAGAIN: not enough resources
 *
 * Description:
 *   Associate this object with given handle in the array (if handle not already used).
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_insert2hndl(VIP_array_p_t VIP_array, VIP_array_obj_t obj,
  VIP_array_handle_t hndl );


/********************************************************************************
 * Function: VIP_array_insert_ptr
 *
 * Arguments:
 *  VIP_array (IN) - Insert in this table
 *  handle_p (OUT) - handle for this object
 *  obj (OUT) - pointer to new object
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EAGAIN: not enough resources
 *
 * Description:
 *   Associate a new object with this handle.
 *   This is like VIP_array_insert, but it returns
 *   a pointer into the array through which the pointer 
 *   to the object can be set later.
 *
 *   NOTE: the pointer returned is only valid until the next
 *   call to insert/erase! After this you must use the handle
 *   to access the value.
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_insert_ptr(VIP_array_p_t VIP_array, 
  VIP_array_handle_t* handle_p , VIP_array_obj_t** obj);


/********************************************************************************
 * Function: VIP_array_erase
 *
 * Arguments:
 *  VIP_array (IN) - this table
 *  handle (IN) - remove object by this handle
 *  obj (OUT) - if non zero, returns the object by this handle here
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HNDL: handle is not in the VIP_array
 *  VIP_EBUSY
 *
 * Description:
 *   Remove the object associated with this handle
 *   Note: fails if handle is not already in the VIP_array
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_erase(VIP_array_p_t a, VIP_array_handle_t handle, 
  VIP_array_obj_t* obj_p );

/********************************************************************************
 * Function: VIP_array_find_release_erase
 *
 * Arguments:
 *  VIP_array (IN) - this table
 *  handle (IN) - remove object by this handle
 *  obj (OUT) - if non zero, returns the object by this handle here
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HNDL: handle is not in the VIP_array, or only_this_obj don't match object at handle
 *  VIP_EBUSY: Handle is still busy (ref_cnt > 0 , after dec.). ref_cnt is updated anyway.
 *
 * Description:
 *   This function is a combination of VIP_array_find_release and VIP_array_erase.
 *   The function atomically decrements the handle's reference count and check if it reached 0.
 *   Only if the ref_cnt is 0, the object is erased. Otherwise, VIP_EBUSY is returned.
 *   Note: The reference count is decrement by 1 even on VIP_EBUSY error.
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_find_release_erase(VIP_array_p_t a, VIP_array_handle_t handle, 
  VIP_array_obj_t* obj_p );


/********************************************************************************
 * Function: VIP_array_erase_prepare
 *
 * Arguments:
 *  VIP_array (IN) - Insert in this table
 *  handle (IN) - remove object by this handle
 *  obj (OUT) - if non zero, returns the object by this handle here
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HNDL: handle is not in the VIP_array
 *  VIP_EBUSY: Handle's reference count > 0
 *
 * Description:
 *    invalidate the object in the array, not yet removing the object associated with this handle
 *    see: VIP_array_erase_done , VIP_array_erase_undo
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_erase_prepare(VIP_array_p_t VIP_array, VIP_array_handle_t handle, 
  VIP_array_obj_t* obj );

/********************************************************************************
 * Function: VIP_array_find_release_erase_prepare
 *
 * Arguments:
 *  VIP_array (IN) - this table
 *  handle (IN) - remove object by this handle
 *  obj (OUT) - if non zero, returns the object by this handle here
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HNDL: handle is not in the VIP_array, or only_this_obj don't match object at handle
 *  VIP_EBUSY: Handle is still busy (ref_cnt > 0 , after dec.). ref_cnt is updated anyway.
 *
 * Description:
 *   This function is a combination of VIP_array_find_release and VIP_array_erase_prepare.
 *   The function atomically decrements the handle's reference count and check if it reached 0.
 *   Only if the ref_cnt is 0, the object is erased (prep.). Otherwise, VIP_EBUSY is returned.
 *   Note: The reference count is decrement by 1 even on VIP_EBUSY error.
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_find_release_erase_prepare(VIP_array_p_t a, VIP_array_handle_t handle, 
  VIP_array_obj_t* obj_p );

/********************************************************************************
 * Function: VIP_array_erase_undo
 *
 * Arguments:
 *  VIP_array (IN) - Insert in this table
 *  handle (IN) - object by this handle
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HNDL: handle is not in the VIP_array or not was "erase prepare".
 *
 * Description:
 *  revalidates the object of this handle, undo the erasing operation
 *  see: VIP_array_erase_prepare
 * 
 ********************************************************************************/
VIP_common_ret_t VIP_array_erase_undo(VIP_array_p_t VIP_array, VIP_array_handle_t handl);


/********************************************************************************
 * Function: VIP_array_erase_done
 *
 * Arguments:
 *  VIP_array (IN) - Insert in this table
 *  handle (IN) - remove object by this handle
 *  obj (OUT) - if non zero, returns the object by this handle here
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HNDL: handle is not in the VIP_array or not was "erase prepare".
 *
 * Description:
 *    removes the object associated with this handle
 *    see: VIP_array_erase_prepare
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_erase_done(VIP_array_p_t a, VIP_array_handle_t handle, VIP_array_obj_t *obj);



/********************************************************************************
 * Function: VIP_array_find
 *
 * Arguments:
 *  VIP_array (IN) - Insert in this table
 *  handle (IN) - get object by this handle
 *  obj (OUT) - if non zero, returns the object by this handle here
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HNDL: handle is not in the VIP_array
 *
 * Description:
 *   Find the object associated with this handle
 *   Note: fails if handle is illegal in the VIP_array
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_find(VIP_array_p_t VIP_array, VIP_array_handle_t handle,
  VIP_array_obj_t* obj );

/********************************************************************************
 * Function: VIP_array_find_hold
 *
 * Arguments:
 *  VIP_array (IN) - Insert in this table
 *  handle (IN) - get object by this handle
 *  obj (OUT) - if non zero, returns the object by this handle here
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HNDL: handle is not in the VIP_array
 *
 * Description:
 *   Same as VIP_array_find, but also updates object's reference count.
 *   VIP_array_erase will fail if reference count > 0 .
 *   Handle must be released with VIP_array_find_release
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_find_hold(VIP_array_p_t VIP_array, VIP_array_handle_t handle,
  VIP_array_obj_t* obj );

/********************************************************************************
 * Function: VIP_array_find_release
 *
 * Arguments:
 *  VIP_array (IN) - Insert in this table
 *  handle (IN) - remove object by this handle
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EINVAL_HNDL: handle is not in the VIP_array
 *
 * Description:
 *   Decrement handle's reference count.
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_find_release(VIP_array_p_t VIP_array, VIP_array_handle_t handle);

/********************************************************************************
 * Function: VIP_array_get_num_of_objects
 *
 * Arguments:
 *  VIP_array (IN) - table
 *
 * Returns:
 *  number of objects in the array
 *
 * Description:
 *   Get number of objects
 *
 ********************************************************************************/
u_int32_t VIP_array_get_num_of_objects(VIP_array_p_t VIP_array);

/********************************************************************************
 * Function: VIP_array_get_allocated_size
 *
 * Arguments:
 *  VIP_array (IN) - table
 *
 * Returns:
 *  current allocated size of the array
 *
 * Description:
 *   allocated size of the arrays
 *
 ********************************************************************************/
u_int32_t VIP_array_get_allocated_size(VIP_array_p_t VIP_array);
/********************************************************************************
 * Functions: VIP_array_get_first/next_handle
 *
 * Arguments:
 *  VIP_array (IN) - Go over this table 
 *  hdl (OUT) - if non zero, returns the next valid handle here
 *  obj (OUT) - if non zero, returns the object of this handle 
 *
 * Returns:
 *  VIP_OK - this code was returned for all objects
 *  VIP_EINVAL_HNDL: no more valid handles in this array
 *
 * Description:
 *   These can be used to iterate over the array, and get all valid
 *   handles. Initialise handle with get_first, then call get_next.
 *   VIP_EINVAL_HNDL is returned when there are no more handles.
 *   Usage example:
 *  VIP_array_handle_t hdl;
 *  for(ret=VIP_array_get_first_handle(VIP_array, &hdl, NULL);
 *      ret == VIP_OK; ret=VIP_array_get_next_handle(VIP_array,&hdl, NULL)) {
 *  }
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_get_first_handle(VIP_array_p_t VIP_array, 
    VIP_array_handle_t* hdl, VIP_array_obj_t* obj);

VIP_common_ret_t VIP_array_get_next_handle(VIP_array_p_t VIP_array, 
    VIP_array_handle_t* hdl, VIP_array_obj_t* obj );

/********************************************************************************
 * Functions: VIP_array_get_first_handle_hold/next_handle_hold
 *
 * Arguments:
 *  VIP_array (IN) - Go over this table 
 *  hdl (OUT) - returns the next valid busy handle here (MUST be non-zero)
 *  obj (OUT) - if non zero, returns the object of this handle 
 *  busy_only (IN) - if TRUE, only returns busy valid items in the scan
 *
 * Returns:
 *  VIP_OK - this code was returned for all objects
 *  VIP_EAGAIN: reference count already at max. Info returned, but user must not release
 *              when done with item
 *  VIP_EINVAL_HNDL: no more valid/busy handles in this array
 *
 * Description:
 *   These can be used to iterate over the array, and get all valid/busy
 *   handles, updating ref count for items returned. 
 *   Initialise handle with get_first_hold, then call get_next_hold.
 *   VIP_EINVAL_HNDL is returned when there are no more handles.
 *   When done with a given handle, user must call VIP_array_find_release on the returned
 *   handle to decrement its reference count.
 *
 *   Usage example (for getting and holding all valid items):
 *  VIP_array_handle_t hdl;
 *  for(ret=VIP_array_get_next_handle_hold(VIP_array, &hdl, NULL, FALSE);
 *      ret == VIP_OK; ret=VIP_array_get_next_busy_handle(VIP_array,&hdl, NULL, FALSE)) {
 *  }
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_get_first_handle_hold(VIP_array_p_t VIP_array, 
    VIP_array_handle_t* hdl, VIP_array_obj_t* obj, MT_bool busy_only);

VIP_common_ret_t VIP_array_get_next_handle_hold(VIP_array_p_t VIP_array, 
    VIP_array_handle_t* hdl, VIP_array_obj_t* obj, MT_bool busy_only );
/********************************************************************************
 * Macro: VIP_ARRAY_FOREACH
 *
 * Arguments:
 *  VIP_array_p_t VIP_array (IN) - Go over this table 
 *  VIP_common_ret_t ret (OUT) - variable (lvalue) to hold the current return code
 *  VIP_array_handle_t hdl (OUT) - variable (lvalue) to hold current object handle
 *  VIP_array_obj_t* obj_p (OUT) - if non zero, returns the object of this handle 
 *
 * Returns:
 *
 * Description:
 *   This macro can be used to iterate over the array.
 *   Only valid handles are returned.
 *   If you are not interested in objects but only in handles pass
 *   NULL instead of obj_p.
 *
 *   Usage example (erase all object from the array, and free them):
 *
 *  VIP_array_handle_t hdl;
 *  VIP_common_ret_t ret;
 *  VIP_array_obj_t obj;
 *
 *  VIP_ARRAY_FOREACH(VIP_array, ret, hdl, &obj) {
 *     VIP_array_erase(VIP_array, hdl, NULL);
 *     FREE(obj);
 *  }
 *
 ********************************************************************************/
#define VIP_ARRAY_FOREACH(VIP_array, ret, hdl, obj_p) \
  for(ret=VIP_array_get_first_handle(VIP_array, &hdl, obj_p);\
      ret == VIP_OK; ret=VIP_array_get_next_handle(VIP_array,&hdl, obj_p)) 

/********************************************************************************
 * Macro: VIP_ARRAY_FOREACH_HOLD
 *
 * Arguments:
 *  VIP_array_p_t VIP_array (IN) - Go over this table 
 *  VIP_common_ret_t ret (OUT) - variable (lvalue) to hold the current return code
 *  VIP_array_handle_t hdl (OUT) - variable (lvalue) to hold current object handle
 *  VIP_array_obj_t* obj_p (OUT) - if non zero, returns the object of this handle 
 *  busy_only -- if TRUE, return only items already busy (i.e., nonzero ref count)
 *
 * Returns:
 *
 * Description:
 *   This macro can be used to iterate over the array, holding each item returned.
 *   Only valid handles are returned. If the busy_only flag is TRUE, the item must
 *   already have a non-zero reference count to be returned.
 *   If you are not interested in objects but only in handles pass
 *   NULL instead of obj_p.
 *
 *   When you are done with a returned handle, you MUST call VIP_array_find_release() on
 *   that handle (or the item will not be deletable).
 *
 *   Usage example (erase all object from the array, and free them):
 *
 *  VIP_array_handle_t hdl;
 *  VIP_common_ret_t ret;
 *  VIP_array_obj_t obj;
 *
 *  VIP_ARRAY_FOREACH_HOLD(VIP_array, ret, hdl, &obj, TRUE) {
 *     ... do something with returned object ...
 *     if (ret == VIP_OK) VIP_array_find_release(VIP_array, hdl);
 *  }
 *
 ********************************************************************************/
#define VIP_ARRAY_FOREACH_HOLD(VIP_array, ret, hdl, obj_p, busy_only) \
  for(ret=VIP_array_get_first_handle_hold(VIP_array, &hdl, obj_p, busy_only);\
      ((ret == VIP_OK) || (ret == VIP_EAGAIN)); ret=VIP_array_get_next_handle_hold(VIP_array,&hdl, obj_p, busy_only)) 

#ifdef  __cplusplus
 }
#endif

#endif
