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
#include <vapi_common.h>

#include "vip_array.h"

#define ARRAY_INIT_NUM_ENTRIES (1<<16)   /*16K initial size */
#define ARRAY_INCR_NUM_ENTRIES (1<<16)   /*16K increment size */
#define ARRAY_MIN_SIZE         2 
#define ARRAY_MAX_SIZE         0xFFFFFFFE
#define ARRAY_RESIZE_ERR       0xFFFFFFFF
#define ARRAY_DEFAULT_MAXSIZE  (1 << 24)    /* default max size is 16M entries */
#define ARRAY_2ND_LVL_BLOCK_SIZE   (2*MOSAL_SYS_PAGE_SIZE)
#define ARRAY_2ND_LVL_ENTRIES_PER_BLOCK (ARRAY_2ND_LVL_BLOCK_SIZE / sizeof(VIP_array_internal_obj_t))
#define ARRAY_2ND_LVL_ENTRY_SIZE   (sizeof(VIP_array_internal_obj_t))

#define CALCULATE_NEW_SIZE(curr_size, max_size, array)   ((max_size - curr_size < ARRAY_INCR_NUM_ENTRIES) ? max_size : \
                                         curr_size + ARRAY_INCR_NUM_ENTRIES)
#define AT_MAX_SIZE(curr_size, max_size)   ((max_size == curr_size ) ? TRUE : FALSE)
                                         
#define CALC_MAX_2ND_LVL_BLOCKS(array)  ((array->max_size + (array->sec_lvl_entries_per_blk_m_1)) / (array->sec_lvl_entries_per_blk))
#define CALC_NUM_2ND_LVL_BLOCKS(size, array)  ((size + (array->sec_lvl_entries_per_blk_m_1)) / (array->sec_lvl_entries_per_blk))
#define KMALLOC_1ST_LVL_MAX  (1<<15)  /* max 32K for first level kmalloc */

typedef MT_size_t VIP_array_ref_cnt_t;

typedef struct VIP_array_internal_obj_t {
    MT_ulong_ptr_t         array_obj;
    VIP_array_ref_cnt_t   ref_count;  /* Handle reference count. (-1) if invalid. */
} VIP_array_internal_obj_t;

typedef VIP_array_internal_obj_t * VIP_array_1st_lvl_t;

typedef struct VIP_array_t {
  VIP_array_1st_lvl_t      *begin;
  VIP_array_ref_cnt_t      first_invalid;
  u_int32_t        size; 
  u_int32_t        watermark;
  u_int32_t        size_allocated;
  u_int32_t        max_size;
  u_int32_t        sec_lvl_entries_per_blk_m_1;     /* number of second level entries - 1 per block*/
  u_int32_t        sec_lvl_entries_per_blk;         /* number of second level entries per block*/
  u_int32_t        size_2nd_lvl_block;              /* number of bytes per second level block */
  u_int8_t         log2_2nd_lvl_entries_per_blk;    /* log2 of number of second level entries per blk*/
  MT_bool          first_level_uses_vmalloc;

  MOSAL_spinlock_t  array_lock;
  MOSAL_mutex_t   resize_lock;  //held while resize is in progress
} VIP_array_t;

#define INVALID_REF_VAL (-1UL)  /* Value to put in "ref_cnt" to mark invalid entry */
#define PREP_ERASE_VAL  (-2UL)  /* Value to put in "ref_cnt" to mark status: prepare to erase */

#define GET_OBJ_BY_HNDL(array, handle)  ((VIP_array_internal_obj_t *) &((*(array->begin+((handle) >> array->log2_2nd_lvl_entries_per_blk)))[(handle) & (array->sec_lvl_entries_per_blk_m_1)]))
#define GET_OBJ_ARR_OBJ_BY_HNDL(array, handle)  (GET_OBJ_BY_HNDL(array,handle))->array_obj
#define GET_OBJ_REF_CNT_BY_HNDL(array, handle)  (GET_OBJ_BY_HNDL(array,handle))->ref_count
#define IS_INVALID_ASSIGN(vip_array_p,obj,i)    (((obj = GET_OBJ_BY_HNDL(vip_array_p,i))==NULL) || (obj->ref_count == INVALID_REF_VAL)||(obj->ref_count == PREP_ERASE_VAL))
#define IS_INVALID_OBJ(obj)        ((obj->ref_count == INVALID_REF_VAL)||(obj->ref_count == PREP_ERASE_VAL))
#define SET_INVALID_OBJ(obj)       (obj->ref_count = INVALID_REF_VAL)
#define SET_VALID_OBJ(obj)         (obj->ref_count = 0)
#define RESIZE_REQUIRED(array_obj) (array_obj->watermark >= array_obj->size_allocated))
#define IS_NOT_BUSY_OBJ(obj,fl)   (IS_INVALID_OBJ(obj) || ((fl == TRUE) && (obj->ref_count ==0)))

static unsigned int  floor_log2(u_int64_t x)
{
   enum { nLowBits = 8 };
   static const u_int64_t 
      highMask = ~(((u_int64_t)1 << nLowBits) - (u_int64_t)1);
   static const unsigned char  lowlog2[1<<nLowBits] =
   {
      0, /* special case (not -infinity) */
      0,
      1, 1,
      2, 2, 2, 2,
      3, 3, 3, 3, 3, 3, 3, 3,
      4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
      4, 4, 4, 4, 4, 4,
      5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
      5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
      5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
      5, 5,
      6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 6, 6,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
      7, 7, 7, 7, 7, 7, 7, 7
   };
   
   unsigned  step = 8*sizeof(u_int64_t);
   unsigned  p = 0;
   while (x & highMask)
   {
      u_int64_t   high;
      step >>= 1; /* /= 2 */
      high = (x >> step);
      if (high)
      {
         p |= step;
         x = high;
      }
      else
      {
         u_int64_t  mask = ((u_int64_t)1 << step) - 1;
         x &= mask;
      }
   }
      
   p |= lowlog2[x];
   return p;
} /* floor_log2 */


/************************************************************************/
static unsigned int  ceil_log2(u_int64_t x)
{
   unsigned int  p = floor_log2(x);
   if (((u_int64_t)1 << p) < x)
   {
      p += 1;
   }
   return p;
} /* ceil_log2 */

/********************************************************************************/
/* Resize array to given size_to_alloc entries                                */
static VIP_common_ret_t resize_array(VIP_array_p_t a,u_int32_t  size_to_alloc)
{
  /* We can sample the value above with no lock, since there is only one thread
   * which can perform resize at one time 
   */
  u_int32_t  blocks_needed = 0, max_num_blocks = 0, curr_blocks = 0, block_size_to_allocate = 0;
  int i,j;

  MTL_DEBUG4(MT_FLFMT("realloc: watermark=%d, size_to_alloc=%d, size allocated = 0x%x, max_size=0x%x"), 
             a->watermark, size_to_alloc, a->size_allocated, a->max_size);
  /* now, insert second level blocks until initial size is reached.  If this is also the max size,
   *  make sure that the last block allocation is only until the max number of entries needed. 
   *  Note that do not need special protection when adding new blocks to the array, since we have
   *  not yet changed the allocated size for the array -- so the new blocks are not yet visible.
   */

  MOSAL_spinlock_irq_lock(&(a->array_lock));
  if (size_to_alloc > a->max_size) {
      MOSAL_spinlock_unlock(&(a->array_lock));
      MTL_ERROR1(MT_FLFMT("resize_array: requested new size (0x%x)greater than max (0x%x)"), 
                 size_to_alloc, a->max_size);
      return VIP_EINVAL_PARAM;
  }
  max_num_blocks = CALC_MAX_2ND_LVL_BLOCKS(a);
  blocks_needed = CALC_NUM_2ND_LVL_BLOCKS(size_to_alloc,a);
  curr_blocks = CALC_NUM_2ND_LVL_BLOCKS(a->size_allocated,a);
  block_size_to_allocate = a->size_2nd_lvl_block;
  MOSAL_spinlock_unlock(&(a->array_lock));
  
  for (i = (int)curr_blocks; i < (int)blocks_needed; i++) {
      if (i == (int)max_num_blocks - 1) {
          block_size_to_allocate =  (a->max_size - ((max_num_blocks-1)*(a->sec_lvl_entries_per_blk)))
                                                  * ARRAY_2ND_LVL_ENTRY_SIZE;
      }
      a->begin[i] = (VIP_array_internal_obj_t*)MALLOC(block_size_to_allocate);
      if (a->begin[i] == NULL) {
          MTL_ERROR1(MT_FLFMT("VIP_array_create_maxsize: malloc failure at 2nd level block %d"), i);
          for (j = curr_blocks; j < i; j++) {
              FREE(a->begin[j]);
              a->begin[j] = NULL;
          }
          return VIP_EAGAIN;
      } else {
          memset(a->begin[i], 0xFF, block_size_to_allocate);
      }
  }

  /* adjust vip array object parameters */
  MOSAL_spinlock_irq_lock(&(a->array_lock));
  a->size_allocated = size_to_alloc;
  MOSAL_spinlock_unlock(&(a->array_lock));

  return VIP_OK;
} /* resize_array */
  

/* This function either initiates a resize or wait for another thread to complete it */
/* The function must be called with array's lock held
 */
static VIP_common_ret_t resize_or_waitfor(VIP_array_p_t VIP_array, u_int32_t new_sz)
{
  call_result_t mt_rc;
  VIP_common_ret_t rc;

  MTL_DEBUG4(MT_FLFMT("%s: Entering.  new size = %d"),__func__, new_sz);
  MOSAL_spinlock_unlock(&(VIP_array->array_lock));


  mt_rc = MOSAL_mutex_acq(&(VIP_array->resize_lock),TRUE);
  if (mt_rc != MT_OK)
  {
    //assume MT_EINTR
    rc= VIP_EINTR;
    goto mutex_acq_lbl;
  }

  if (new_sz <= VIP_array->size_allocated)
  {
    rc = VIP_OK;
    goto size_check_lbl;
  }

  rc= resize_array(VIP_array,new_sz);
  if (rc) goto resize_lbl;
  
resize_lbl:

size_check_lbl:
  MOSAL_mutex_rel(&(VIP_array->resize_lock));

mutex_acq_lbl:
  MOSAL_spinlock_irq_lock(&(VIP_array->array_lock));
  return rc;
}

/********************************************************************************
 * Function: VIP_array_create_maxsize
 *
 * Arguments:
 *  VIP_array_p (OUT) - Return new VIP_array object here
 *  maxsize - max number of entries in the array.
 *
 * Returns:
 *  VIP_OK, 
 *  VIP_EAGAIN: Not enough resources
 *  VIP_EINVAL_PARAM: requested an array larger than max permitted size
 *
 * Description:
 *   Create a new VIP_array table
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_create_maxsize(u_int32_t size, u_int32_t maxsize, VIP_array_p_t* VIP_array_p)
{
  VIP_common_ret_t  rc = VIP_EAGAIN;
  VIP_array_p_t     array;
  u_int32_t         max_num_blocks = 0, size_1st_lvl = 0;

  MTL_DEBUG4(MT_FLFMT("VIP_array_create_maxsize: size=0x%x, maxsize=0x%x"), size, maxsize);
  
  if ( size > maxsize) {
      MTL_ERROR1(MT_FLFMT("VIP_array_create_maxsize: requested size (0x%x) greater than supplied max size (0x%x"),
                  size, maxsize);
      return VIP_EINVAL_PARAM;
  }
  if (size > ARRAY_MAX_SIZE) {
      MTL_ERROR1(MT_FLFMT("VIP_array_create: requested size (0x%x) greater than max permitted"), size);
      return VIP_EINVAL_PARAM;
  }
  array = TMALLOC(VIP_array_t);
  if (array == NULL) {
      MTL_ERROR1(MT_FLFMT("VIP_array_create: malloc failure"));
      return VIP_EAGAIN;
  }

  if (maxsize < ARRAY_MIN_SIZE) {maxsize =  ARRAY_MIN_SIZE;}

  memset(array, 0, sizeof(VIP_array_t));
  array->max_size = maxsize;
  array->sec_lvl_entries_per_blk_m_1  = ARRAY_2ND_LVL_ENTRIES_PER_BLOCK-1;
  array->sec_lvl_entries_per_blk      = ARRAY_2ND_LVL_ENTRIES_PER_BLOCK;
  array->size_2nd_lvl_block           = ARRAY_2ND_LVL_BLOCK_SIZE;
  array->log2_2nd_lvl_entries_per_blk = ceil_log2(ARRAY_2ND_LVL_ENTRIES_PER_BLOCK);
  
  /* make sure that the first block set is always allocated in total -- unless max array size is less than entries
     per second level block
   */

  if (maxsize <= ARRAY_INIT_NUM_ENTRIES) { 
      if (size < maxsize) {size = maxsize;}
  } else {
      if (size < ARRAY_INIT_NUM_ENTRIES) {
          size = ARRAY_INIT_NUM_ENTRIES;
      }
  }

  /* compute size of and allocate first-level allocation */
  max_num_blocks = CALC_MAX_2ND_LVL_BLOCKS(array);
  size_1st_lvl   = sizeof(VIP_array_1st_lvl_t) * max_num_blocks;

  /* use KMALLOC if first level size is smaller than 32K */
  array->begin = (size_1st_lvl <= KMALLOC_1ST_LVL_MAX) ? TNMALLOC(VIP_array_1st_lvl_t, max_num_blocks) : NULL;
  if (array->begin == NULL) {
      array->first_level_uses_vmalloc = TRUE;
      array->begin = TNVMALLOC(VIP_array_1st_lvl_t, max_num_blocks);
      if (array->begin == NULL) {
          MTL_ERROR1(MT_FLFMT("VIP_array_create: malloc failure for size 0x%x"), (u_int32_t)(size_1st_lvl));
          rc = VIP_EAGAIN;
          goto first_lvl_fail;
      }
  }

  memset(array->begin, 0, size_1st_lvl);
  array->first_invalid       = INVALID_REF_VAL;
  array->size                = 0;
  array->watermark           = 0;
  array->size_allocated      = 0;
  MOSAL_spinlock_init(&(array->array_lock));

  rc = resize_array(array,size);
  if (rc != VIP_OK) {
      MTL_ERROR1(MT_FLFMT("VIP_array_create_maxsize: 2nd level alloc failure for size 0x%x"),
                (u_int32_t)(sizeof(VIP_array_1st_lvl_t)* maxsize));
      goto second_lvl_fail;
  }
  MOSAL_mutex_init(&(array->resize_lock));
  *VIP_array_p = array;
  rc = VIP_OK;
  
  MTL_DEBUG4(MT_FLFMT("VIP_array_create: rc=%d"), rc);
  return rc;

second_lvl_fail:
  if (array->first_level_uses_vmalloc) {
      VFREE(array->begin);
  } else {
      FREE(array->begin);
  }
first_lvl_fail:
  FREE(array);
  return rc;
} /* VIP_array_create_maxsize */

VIP_common_ret_t VIP_array_create(u_int32_t size, VIP_array_p_t* VIP_array_p)
{
    return VIP_array_create_maxsize(size,ARRAY_DEFAULT_MAXSIZE,VIP_array_p);
}


/********************************************************************************
 * Function: VIP_array_destroy
 *
 * Arguments:
 *  VIP_array (IN) - Object to destroy
 *  force (IN) - If false do not destroy VIP_array that is not empty
 *  FREE_objects (IN) - If true destroy objects pointed from the array
 *
 * Returns:
 *  VIP_OK
 *
 * Description:
 *   cleanup resources for a VIP_array table
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_destroy(VIP_array_p_t VIP_array,
    VIP_allocator_free_t free_objects_fun)
{
  VIP_common_ret_t ret=VIP_OK;
  VIP_array_handle_t hdl;
  VIP_array_obj_t    obj;
  u_int32_t  max_num_blocks;
  int i;
  call_result_t mt_rc = MT_OK;
 
  if (VIP_array == NULL)  return VIP_EINVAL_HNDL;
  MTL_DEBUG4("Inside " "%s:Array size %d\n", __func__, VIP_array->size);
 


  if (VIP_array->size != 0 && free_objects_fun) {
    ret = VIP_array_get_first_handle(VIP_array, &hdl, &obj);
    while (ret == VIP_OK) {
      free_objects_fun(obj);
      ret= VIP_array_get_next_handle(VIP_array,&hdl, &obj);
    }
  }
  /*free second level allocations */

  max_num_blocks = CALC_MAX_2ND_LVL_BLOCKS(VIP_array);
  for (i = 0; i < (int)max_num_blocks; i++) {
      if (VIP_array->begin[i] == NULL) {break;}
      FREE(VIP_array->begin[i]);
  }

  /* now, free first level allocation */
  if (VIP_array->first_level_uses_vmalloc) {
      VFREE(VIP_array->begin);
  } else {
      FREE(VIP_array->begin);
  }
  mt_rc = MOSAL_mutex_free(&(VIP_array->resize_lock));
  if (mt_rc != MT_OK) {
    MTL_ERROR2(MT_FLFMT("Failed MOSAL_syncobj_free (%s)"),mtl_strerror_sym(mt_rc));
  }
  FREE(VIP_array);
  return VIP_OK;
}

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
 *   Associate this object with this handle.
 *   Return the object associated with this handle.
 *   Note: No check is done that the object is not already
 *   in the array.
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_insert(VIP_array_p_t VIP_array, VIP_array_obj_t obj,
  VIP_array_handle_t* handle_p ) 
{
  VIP_common_ret_t  rc;
  register VIP_array_internal_obj_t *itl_obj_p = NULL;
  
  MTL_DEBUG4(MT_FLFMT("VIP_array_insert: %p"), obj);
  if (VIP_array == NULL)  return VIP_EINVAL_HNDL;
  /* First, try to use the list of invalid */
  MOSAL_spinlock_irq_lock(&(VIP_array->array_lock));

  while (1) { 
    /* Try allocating free entry. In case of "resize", a retry is required
     * since during spinlock release a lot could happen.
     */
    if (VIP_array->first_invalid != INVALID_REF_VAL) { /* Check on free list */
      *handle_p = (VIP_array_handle_t)VIP_array->first_invalid;
      /* Move the list head to point to the next invalid handle */
      itl_obj_p = GET_OBJ_BY_HNDL(VIP_array,(*handle_p));
      VIP_array->first_invalid = itl_obj_p->array_obj;
      break;
    } else {  /* If free list is empty, take next free pointed by "watermark" */
      /* check for allocation: "watermark" is valid only if it does not exceeds array size */
      if (VIP_array->watermark >= VIP_array->size_allocated) {/* resize required ? */ 
        if (AT_MAX_SIZE(VIP_array->size_allocated, VIP_array->max_size)) {
            MOSAL_spinlock_unlock(&(VIP_array->array_lock));
            MTL_ERROR1(MT_FLFMT("%s: Array size already at maximum (0x%x)"),__func__,VIP_array->max_size);
            return VIP_EAGAIN;
        }
        rc= resize_or_waitfor(VIP_array, CALCULATE_NEW_SIZE(VIP_array->size_allocated, VIP_array->max_size, VIP_array));
        if (rc != VIP_OK)  {  /* Fatal error */
          MOSAL_spinlock_unlock(&(VIP_array->array_lock));
          MTL_ERROR1(MT_FLFMT("%s: Failed resizing array (%s   %d)"),__func__,VAPI_strerror_sym((VAPI_ret_t)rc),rc);
          return rc;
        }
        continue; 
        /* Retry all process - maybe some "frees" were done while spinlock was unlocked */
      } else {  /* There are free entries at "watermark" */
        *handle_p = VIP_array->watermark++;
        itl_obj_p = GET_OBJ_BY_HNDL(VIP_array,*handle_p);
        break;
      } /* if "watermark" */
    } /* if "free list" */
  } /* while */

  /* Reaching this point means "*handle_p" and "itl_obj_p" are valid */
  itl_obj_p->array_obj = (MT_ulong_ptr_t) obj; /* Put object in entry */
  SET_VALID_OBJ(itl_obj_p); /* implies ref_cnt=0 */
  ++VIP_array->size;

  MOSAL_spinlock_unlock(&(VIP_array->array_lock));

  return VIP_OK;  /* If reached here, allocation was successful */
} /* VIP_array_insert */



VIP_common_ret_t VIP_array_insert2hndl(VIP_array_p_t VIP_array, VIP_array_obj_t obj,
  VIP_array_handle_t hndl )
{
  VIP_common_ret_t  rc;
  MT_ulong_ptr_t prev_hndl,cur_hndl;
  u_int32_t required_size= 0;
  register VIP_array_internal_obj_t *itl_obj_p = NULL;

  
  MTL_DEBUG4(MT_FLFMT("VIP_array_insert: %p"), obj);
  if (VIP_array == NULL)  return VIP_EINVAL_HNDL;
  if (VIP_array->max_size < hndl) {
      MTL_ERROR1(MT_FLFMT("%s: requested handle (0x%x) greater than array maximum"),__func__,hndl);
      return VIP_EINVAL_PARAM;
  }
  required_size = VIP_array->size_allocated;
  while (required_size <= VIP_array->max_size) {
      if (required_size >= hndl+1) {break;}
      if (required_size == VIP_array->max_size) {
          MTL_ERROR1(MT_FLFMT("%s: requested handle (0x%x) greater than array maximum"),__func__,hndl);
          return VIP_EINVAL_PARAM;
      }
      //MTL_DEBUG3(MT_FLFMT("%s: loop: new required size = %d"),__func__, required_size );
      required_size = CALCULATE_NEW_SIZE(required_size, VIP_array->max_size, VIP_array);
  }
  
  MTL_DEBUG3(MT_FLFMT("%s: new array size = %d"),__func__, required_size );

  /* First, try to use the list of invalid */
  MOSAL_spinlock_irq_lock(&(VIP_array->array_lock));
  /* retry resize until requested size is reached */ 
  while (required_size > (VIP_array->size_allocated)) {
    rc= resize_or_waitfor(VIP_array,required_size);
    if (rc != VIP_OK)  {  /* Fatal error */
      MOSAL_spinlock_unlock(&(VIP_array->array_lock));
      MTL_ERROR1(MT_FLFMT("%s: Failed resizing array (%s  %d)"),__func__,VAPI_strerror_sym((VAPI_ret_t)rc),rc);
      return rc;
    }
  } /* while resize_or_waitfor */

  itl_obj_p = GET_OBJ_BY_HNDL(VIP_array, hndl);
  if (!IS_INVALID_OBJ(itl_obj_p))  {
    MOSAL_spinlock_unlock(&(VIP_array->array_lock));
    MTL_ERROR1(MT_FLFMT("%s: Handle %d is already in use"),__func__,hndl);
    return VIP_EBUSY;
  }
  
  if (VIP_array->watermark <= hndl) { /* taking a hndl above (or at) watermark */
    /* Insert handles up to requested to free list */
    while (VIP_array->watermark < hndl) {
      VIP_array_internal_obj_t *loop_obj_p = GET_OBJ_BY_HNDL(VIP_array,VIP_array->watermark);
      /* Attach to "free list" */
      loop_obj_p->array_obj = (MT_ulong_ptr_t) VIP_array->first_invalid;
      loop_obj_p->ref_count = INVALID_REF_VAL;
      VIP_array->first_invalid = VIP_array->watermark;
      VIP_array->watermark++;
    }
    VIP_array->watermark++; /* Allocate entry at "hndl" */
  } else { /* Requested handle is in the free list */
    /* Look for the entry and remove from free list */
    for (prev_hndl= INVALID_REF_VAL, cur_hndl= VIP_array->first_invalid;
         cur_hndl != INVALID_REF_VAL; 
         prev_hndl= cur_hndl, cur_hndl= GET_OBJ_ARR_OBJ_BY_HNDL(VIP_array, cur_hndl)) {
      if (cur_hndl == (MT_ulong_ptr_t)hndl)  break; /* handle found */
    }
    if (cur_hndl == INVALID_REF_VAL) {
      MOSAL_spinlock_unlock(&(VIP_array->array_lock));
      MTL_ERROR3(MT_FLFMT("%s: Unexpected error - could not find handle %d in free list"),__func__,
                 hndl);
      return VIP_EFATAL;
    }
    /* Requested handle's entry found - removing from free list */
    if (prev_hndl != INVALID_REF_VAL)  {
        GET_OBJ_ARR_OBJ_BY_HNDL(VIP_array,prev_hndl)= GET_OBJ_ARR_OBJ_BY_HNDL(VIP_array,cur_hndl); /* connect next to prev. */
    } else {
        VIP_array->first_invalid= GET_OBJ_ARR_OBJ_BY_HNDL(VIP_array,cur_hndl); /* next to first */
    }
  }
  
  /* Reaching this point means given hndl was found */
  itl_obj_p->array_obj = (MT_ulong_ptr_t) obj; /* Put object in entry */
  SET_VALID_OBJ(itl_obj_p); /* implies ref_cnt=0 */
  ++VIP_array->size;

  MOSAL_spinlock_unlock(&(VIP_array->array_lock));

  return VIP_OK;  /* If reached here, allocation was successful */
}


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
 *   to the object can be set later
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_insert_ptr(
  VIP_array_p_t a,
  VIP_array_handle_t* handle_p,
  VIP_array_obj_t** obj
)
{
  VIP_common_ret_t    rc = VIP_EAGAIN;

  MTL_DEBUG4(MT_FLFMT("VIP_array_insert_ptr"));
  rc= VIP_array_insert(a,0,handle_p);
  if ((rc == VIP_OK) && (obj)) *obj = (VIP_array_obj_t *)&(GET_OBJ_ARR_OBJ_BY_HNDL(a,(MT_ulong_ptr_t)*handle_p));
  return rc;
} /* VIP_array_insert_ptr */


/* selector for erase_handle() operation */
typedef enum {
  VIP_ARRAY_ERASE,
  VIP_ARRAY_REL_ERASE,
  VIP_ARRAY_ERASE_PREP,
  VIP_ARRAY_REL_ERASE_PREP
} VIP_array_erase_type_t;

/* erase VIP_array handle on if only_this_obj is the object at that handle. */
/* If only_this_obj==NULL, no check is done */
static VIP_common_ret_t erase_handle(VIP_array_erase_type_t etype,
                                     VIP_array_p_t a, VIP_array_handle_t handle, 
                                     VIP_array_obj_t* obj_p)
{
  register VIP_array_internal_obj_t * itl_obj_p = NULL;
  if (a == NULL)  return VIP_EINVAL_HNDL;

  MTL_DEBUG4(MT_FLFMT("VIP_array_erase: handle=%d, wmark=%d"), 
                      handle, a->watermark);
  
  MOSAL_spinlock_irq_lock(&(a->array_lock));
  
  if (handle >= a->watermark || IS_INVALID_ASSIGN(a,itl_obj_p,handle) ) {  /* Invalid handle */
    if (obj_p != NULL) *obj_p = NULL; /* Just "cosmetics" */
    MOSAL_spinlock_unlock(&(a->array_lock));
    return VIP_EINVAL_HNDL;
  }

  if ((etype == VIP_ARRAY_REL_ERASE) || (etype == VIP_ARRAY_REL_ERASE_PREP)) {
    itl_obj_p->ref_count--;  /* Handle is release anyway */
  }
  
  if (itl_obj_p->ref_count > 0) {
    MTL_DEBUG1(MT_FLFMT("%s: handle=%d ref_cnt="SIZE_T_FMT), __func__, 
               handle, itl_obj_p->ref_count);
    MOSAL_spinlock_unlock(&(a->array_lock));
    return VIP_EBUSY;
  }


  if (obj_p != NULL) {*obj_p = (VIP_array_obj_t)(itl_obj_p->array_obj);}
  switch (etype) {
    case VIP_ARRAY_ERASE:
    case VIP_ARRAY_REL_ERASE:
      SET_INVALID_OBJ(itl_obj_p);
      /* Attach to "free list" */
      itl_obj_p->array_obj = a->first_invalid;
      a->first_invalid = (MT_ulong_ptr_t) handle;
      --a->size;
      break;
    case VIP_ARRAY_ERASE_PREP:
    case VIP_ARRAY_REL_ERASE_PREP:
      itl_obj_p->ref_count = PREP_ERASE_VAL;
      break;
    default:
      MOSAL_spinlock_unlock(&(a->array_lock));
      MTL_ERROR1(MT_FLFMT("%s: function used with invalid erase type (%d)"),__func__,etype);
      return VIP_EINVAL_PARAM;
  }
  
  MOSAL_spinlock_unlock(&(a->array_lock));
  return VIP_OK;
}

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
  VIP_array_obj_t* obj_p )
{
  return erase_handle(VIP_ARRAY_ERASE,a,handle,obj_p);
}

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
  VIP_array_obj_t* obj_p )
{
  return erase_handle(VIP_ARRAY_REL_ERASE,a,handle,obj_p);
}


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
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_erase_prepare(VIP_array_p_t a, VIP_array_handle_t handle, 
  VIP_array_obj_t* obj_p)
{
  return erase_handle(VIP_ARRAY_ERASE_PREP,a,handle,obj_p);
}

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
  VIP_array_obj_t* obj_p )
{
  return erase_handle(VIP_ARRAY_REL_ERASE_PREP,a,handle,obj_p);
}

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
VIP_common_ret_t VIP_array_erase_undo(VIP_array_p_t a, VIP_array_handle_t handle)
{
    register VIP_array_internal_obj_t *itl_obj_p = NULL;
    if (a == NULL)  return VIP_EINVAL_HNDL;
    MTL_DEBUG4(MT_FLFMT("VIP_array_erase_roll: handle=%d, wmark=%d"), 
                        handle, a->watermark);

    MOSAL_spinlock_irq_lock(&(a->array_lock));

    if ((handle >= a->watermark) || ((itl_obj_p = GET_OBJ_BY_HNDL(a,handle)) == NULL) || (itl_obj_p->ref_count!= PREP_ERASE_VAL)) {  /* Invalid handle */
      MOSAL_spinlock_unlock(&(a->array_lock));
      return VIP_EINVAL_HNDL;
    }
    SET_VALID_OBJ(itl_obj_p);

    MOSAL_spinlock_unlock(&(a->array_lock));
    return VIP_OK;
}


/********************************************************************************
 * Function: VIP_array_erase_done
 *
 * Arguments:
 *  VIP_array (IN) - Insert in this table
 *  handle (IN) - remove object by this handle
 *  obj (OUT) - if non zero, returns the object by this handle here
 *  
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
VIP_common_ret_t VIP_array_erase_done(VIP_array_p_t a, VIP_array_handle_t handle, VIP_array_obj_t *obj)
{
  register VIP_array_internal_obj_t *itl_obj_p = NULL;
  if (a == NULL)  return VIP_EINVAL_HNDL;

  MTL_DEBUG4(MT_FLFMT("VIP_array_erase: handle=%d, wmark=%d"), 
                      handle, a->watermark);
  
  MOSAL_spinlock_irq_lock(&(a->array_lock));

  if ((handle >= a->watermark) || ((itl_obj_p = GET_OBJ_BY_HNDL(a,handle)) == NULL) || (itl_obj_p->ref_count!= PREP_ERASE_VAL)) {  /* Invalid handle */
      if (obj != NULL) *obj = NULL; /* Just "cosmetics" */
      MOSAL_spinlock_unlock(&(a->array_lock));
      return VIP_EINVAL_HNDL;
  }

  if (obj != NULL) {*obj = (VIP_array_obj_t)(itl_obj_p->array_obj);}
  SET_INVALID_OBJ(itl_obj_p);
  /* Attach to "free list" */
  itl_obj_p->array_obj = a->first_invalid;
  a->first_invalid = (MT_ulong_ptr_t) handle;
  --a->size;
  
  MOSAL_spinlock_unlock(&(a->array_lock));
  return VIP_OK;
}


VIP_common_ret_t VIP_array_find(VIP_array_p_t a, VIP_array_handle_t handle,
  VIP_array_obj_t* obj )
{
  register VIP_array_internal_obj_t *itl_obj_p = NULL;
  if (a == NULL)  return VIP_EINVAL_HNDL;
  MOSAL_spinlock_irq_lock(&(a->array_lock));
  
  if (handle >= a->watermark || IS_INVALID_ASSIGN(a,itl_obj_p,handle) ) {  /* Invalid handle */
    MOSAL_spinlock_unlock(&(a->array_lock));
    if (obj != NULL) *obj= NULL;
    return VIP_EINVAL_HNDL;
  }
  if (obj != NULL) *obj=(VIP_array_obj_t)(itl_obj_p->array_obj);
  
  MOSAL_spinlock_unlock(&(a->array_lock));
  return VIP_OK;
}

VIP_common_ret_t VIP_array_find_hold(VIP_array_p_t a, VIP_array_handle_t handle,
  VIP_array_obj_t* obj )
{
  VIP_common_ret_t rc= VIP_OK;
  register VIP_array_internal_obj_t *itl_obj_p = NULL;

  if (a == NULL)  return VIP_EINVAL_HNDL;
  MOSAL_spinlock_irq_lock(&(a->array_lock));
  if (handle >= a->watermark || IS_INVALID_ASSIGN(a,itl_obj_p,handle)) {  /* Invalid handle */
    if (obj != NULL) *obj= NULL;
    rc= VIP_EINVAL_HNDL;
  } else if ( itl_obj_p->ref_count == INVALID_REF_VAL-1) { /* protect from overflow */
    rc= VIP_EAGAIN;  /* Try again later - when ref. cnt. will be smaller */
  } else {
    (itl_obj_p->ref_count)++;
    if (obj != NULL) *obj=(VIP_array_obj_t)(itl_obj_p->array_obj);
  }

  MOSAL_spinlock_unlock(&(a->array_lock));
  return rc;
}

VIP_common_ret_t VIP_array_find_release(VIP_array_p_t a, VIP_array_handle_t handle)
{
  VIP_common_ret_t rc= VIP_OK;
  register VIP_array_internal_obj_t *itl_obj_p = NULL;

  if (a == NULL)  return VIP_EINVAL_HNDL;
  MOSAL_spinlock_irq_lock(&(a->array_lock));
  
  if (handle >= a->watermark || IS_INVALID_ASSIGN(a,itl_obj_p,handle)) {  /* Invalid handle */
    rc= VIP_EINVAL_HNDL;
  } else if (itl_obj_p->ref_count == 0) {  /* Caller did not invoke VIP_array_find_hold for this handle */
    rc= VIP_EINVAL_HNDL;
  } else {
    (itl_obj_p->ref_count)--;
    MTL_DEBUG6(MT_FLFMT("%s: handle=0x%X ref_count="SIZE_T_DFMT"->"SIZE_T_DFMT), __func__, handle,
               itl_obj_p->ref_count+1 , itl_obj_p->ref_count);
  }
  
  MOSAL_spinlock_unlock(&(a->array_lock));
  return rc;
}

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
u_int32_t VIP_array_get_allocated_size(VIP_array_p_t VIP_array)
{
  if (VIP_array == NULL)  return VIP_EINVAL_HNDL;
  return VIP_array->size_allocated;
}

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
u_int32_t VIP_array_get_num_of_objects(VIP_array_p_t VIP_array)
{
  if (VIP_array == NULL)  return VIP_EINVAL_HNDL;
  return VIP_array->size;
}

/********************************************************************************
 * Functions: VIP_array_get_first/next_handle
 *
 * Arguments:
 *  VIP_array (IN) - Go over this table 
 *  hdl (OUT) - if non zero, returns the next valid handle here
 *
 * Returns:
 *  VIP_OK - this code was returned for all objects
 *  VIP_EINVAL_HNDL: no more valid handles in this array
 *
 * Description:
 *   These can be used to iterate over the array, and get all valid
 *   handles. Initialise handle with first_handle, then call next.
 *   VIP_EINVAL_HNDL is returned when there are no more handles.
 *
 ********************************************************************************/
VIP_common_ret_t VIP_array_get_first_handle(VIP_array_p_t VIP_array, 
    VIP_array_handle_t* hdl,VIP_array_obj_t* obj)
{
  VIP_array_handle_t i;
  register VIP_array_internal_obj_t *itl_obj_p = NULL;
  
  if (VIP_array == NULL)  return VIP_EINVAL_HNDL;
  MOSAL_spinlock_irq_lock(&(VIP_array->array_lock));

  for (i=0;i<VIP_array->watermark;++i) {
    itl_obj_p = GET_OBJ_BY_HNDL(VIP_array, i);
    if (itl_obj_p == NULL || IS_INVALID_OBJ(itl_obj_p))
      continue;
    if (hdl) *hdl=i;
    if (obj) *obj=(VIP_array_obj_t)(itl_obj_p->array_obj);
    MOSAL_spinlock_unlock(&(VIP_array->array_lock));
    return VIP_OK;
  }
  MOSAL_spinlock_unlock(&(VIP_array->array_lock));
  return VIP_EINVAL_HNDL;
}

VIP_common_ret_t VIP_array_get_next_handle(VIP_array_p_t VIP_array, 
    VIP_array_handle_t* hdl, VIP_array_obj_t* obj)
{
  VIP_array_handle_t i;
  VIP_array_internal_obj_t *itl_obj_p = NULL;
  
  if (VIP_array == NULL)  return VIP_EINVAL_HNDL;
  if (!hdl) return VIP_EINVAL_HNDL;

  MOSAL_spinlock_irq_lock(&(VIP_array->array_lock));
  
  for (i=*hdl+1;i<VIP_array->watermark;++i) {
    itl_obj_p = GET_OBJ_BY_HNDL(VIP_array, i);
    if (itl_obj_p == NULL || IS_INVALID_OBJ(itl_obj_p))
      continue;
    *hdl=i;
    if (obj) *obj=(VIP_array_obj_t)(itl_obj_p->array_obj);
    MOSAL_spinlock_unlock(&(VIP_array->array_lock));
    return VIP_OK;
  }
  MOSAL_spinlock_unlock(&(VIP_array->array_lock));
  return VIP_EINVAL_HNDL;
}

VIP_common_ret_t VIP_array_get_first_handle_hold(VIP_array_p_t VIP_array, 
    VIP_array_handle_t* hdl,VIP_array_obj_t* obj, MT_bool busy_only)
{
  VIP_common_ret_t rc= VIP_OK;
  VIP_array_handle_t i;
  register VIP_array_internal_obj_t *itl_obj_p = NULL;
  
  if (VIP_array == NULL)  return VIP_EINVAL_HNDL;
  if (!hdl) return VIP_EINVAL_HNDL;
  MOSAL_spinlock_irq_lock(&(VIP_array->array_lock));

  for (i=0;i<VIP_array->watermark;++i) {
    itl_obj_p = GET_OBJ_BY_HNDL(VIP_array, i);
    if ((itl_obj_p == NULL) || IS_NOT_BUSY_OBJ(itl_obj_p, busy_only)) {continue;}
    if (itl_obj_p->ref_count == INVALID_REF_VAL-1) { /* protect from overflow */
      rc= VIP_EAGAIN;  /* ref. cnt. is at max for this item */
    } else {
      (itl_obj_p->ref_count)++;
    }
    if (hdl) *hdl=i;
    if (obj) *obj=(VIP_array_obj_t)(itl_obj_p->array_obj);
    MOSAL_spinlock_unlock(&(VIP_array->array_lock));
    return rc;
  }
  MOSAL_spinlock_unlock(&(VIP_array->array_lock));
  return VIP_EINVAL_HNDL;
}

VIP_common_ret_t VIP_array_get_next_handle_hold(VIP_array_p_t VIP_array, 
    VIP_array_handle_t* hdl, VIP_array_obj_t* obj, MT_bool busy_only)
{
  VIP_common_ret_t rc= VIP_OK;
  VIP_array_handle_t i;
  register VIP_array_internal_obj_t *itl_obj_p = NULL;
  
  if (VIP_array == NULL)  return VIP_EINVAL_HNDL;
  if (!hdl) return VIP_EINVAL_HNDL;

  MOSAL_spinlock_irq_lock(&(VIP_array->array_lock));
  for (i=*hdl+1;i<VIP_array->watermark;++i) {
      itl_obj_p = GET_OBJ_BY_HNDL(VIP_array, i);
      if ((itl_obj_p == NULL) || IS_NOT_BUSY_OBJ(itl_obj_p, busy_only)) {continue;}
      if (itl_obj_p->ref_count == INVALID_REF_VAL-1) { /* protect from overflow */
        rc= VIP_EAGAIN;  /* ref. cnt. is at max for this item */
      } else {
        (itl_obj_p->ref_count)++;
      }
    *hdl=i;
    if (obj) *obj=(VIP_array_obj_t)(itl_obj_p->array_obj);
    MOSAL_spinlock_unlock(&(VIP_array->array_lock));
    return rc;
  }
  MOSAL_spinlock_unlock(&(VIP_array->array_lock));
  return VIP_EINVAL_HNDL;
}

