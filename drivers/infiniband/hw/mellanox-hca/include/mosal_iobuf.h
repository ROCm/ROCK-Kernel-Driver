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

#ifndef __MOSAL_IOBUF_H_
#define __MOSAL_IOBUF_H_

typedef enum {
  MOSAL_PERM_READ = 1,
  MOSAL_PERM_WRITE = 1<<1
}
MOSAL_mem_perm_enum_t;

typedef u_int32_t MOSAL_mem_perm_t;

typedef struct {
  MT_virt_addr_t va;  /* virtual address of the buffer */
  MT_size_t size;          /* size in bytes of the buffer */
  MOSAL_prot_ctx_t prot_ctx;
  u_int32_t nr_pages;
  MT_u_int_t page_shift;
}
MOSAL_iobuf_props_t;


typedef struct mosal_iobuf_iter_st MOSAL_iobuf_iter_t;
typedef struct mosal_iobuf_st *MOSAL_iobuf_t;

/**** Note ****
   Other platform must define specific flags in the same manner as bellow
   and consume free numbers. Wheather flags may or may not be or'ed must 
   be specified specifically */
/*=== Linux specific flags ===*/
/* The following flags MAY NOT be or'ed */
#define MOSAL_IOBUF_LNX_FLG_MARK_ALL_DONTCOPY        ((u_int32_t)1<<0)
#define MOSAL_IOBUF_LNX_FLG_MARK_FULL_PAGES_DONTCOPY ((u_int32_t)1<<1) /* mark only regions that occupy integral number of pages */


/* The following are os specific flags contained in MOSAL_iobuf_props_t
 * The same rules for allocating numbers are as above */
#define MOSAL_IOBUF_LNX_FLG_PROP_MARKED_DONT_COPY ((u_int32_t)1<<0)

/******************************************************************************
 *  Function (Kernel space only): MOSAL_iobuf_register
 *
 *  Description: register a virtual buf and assure it is locked
 *
 *  Parameters:
 *       va(in) virtual address to register
 *       size(in) size in bytes of area to be registered
 *       prot_ctx(in) context of the calling thread
 *       iobuf_p(out) pointer to return iobuf object
 *       flags(in) flags affecting function's behavior. Flags may have meaning
 *                 in a subset of OS specific implementations of this function.
 *                 Others can ignore them. Document all flags bellow.
 *
 *  Returns:
 *    call_result_t
 *        MT_OK       success
 *        MT_EINVAL   invalid argument
 *        MT_EAGAIN   not enough resources
 *
 *  Notes: upon successful return the buffer pointed by va is guaranteed to be
 *         locked
 *  Flags: MOSAL_IOBUF_LNX_FLG_MARK_ALL_DONTCOPY
 *         MOSAL_IOBUF_LNX_FLG_MARK_FULL_PAGES_DONTCOPY
 *  
 ******************************************************************************/
call_result_t 
#ifndef MTL_TRACK_ALLOC
MOSAL_iobuf_register
#else
MOSAL_iobuf_register_memtrack                                  
#endif
                                  (MT_virt_addr_t va,
                                   MT_size_t size,
                                   MOSAL_prot_ctx_t prot_ctx,
                                   MOSAL_mem_perm_t req_perm,
                                   MOSAL_iobuf_t *iobuf_p);

#ifdef MTL_TRACK_ALLOC
#define MOSAL_iobuf_register(va, size, prot_ctx, req_perm, iobuf_p)                                                    \
                             ({                                                                                        \
                                call_result_t rc;                                                                      \
                                rc = MOSAL_iobuf_register_memtrack(va, size, prot_ctx, req_perm, iobuf_p);             \
                                if ( rc == MT_OK ) {                                                                   \
                                  memtrack_alloc(MEMTRACK_IOBUF, (unsigned long)(*iobuf_p), size, __FILE__, __LINE__); \
                                }                                                                                      \
                                rc;                                                                                    \
                             })
#endif



/******************************************************************************
 *  Function (Kernel space only): MOSAL_iobuf_deregister
 *
 *  Description: deregister the memory context and free resources
 *
 *  Parameters:
 *       iobuf(in) iobuf object to be released
 *
 *  Returns:
 *    call_result_t
 *        MT_OK       success
 *  Notes: when the function returns the the memory area is no longer guaranteed
 *         to be locked and iobuf is no longer valid
 ******************************************************************************/
call_result_t 
#ifndef MTL_TRACK_ALLOC
MOSAL_iobuf_deregister
#else
MOSAL_iobuf_deregister_memtrack                                  
#endif
                      (MOSAL_iobuf_t iobuf);

#ifdef MTL_TRACK_ALLOC
#define MOSAL_iobuf_deregister(iobuf)                                                     \
                             ({                                                           \
                                call_result_t rc;                                         \
                                memtrack_free(MEMTRACK_IOBUF, (unsigned long)(iobuf), __FILE__, __LINE__);   \
                                rc = MOSAL_iobuf_deregister_memtrack(iobuf);              \
                                rc;                                                       \
                             })
#endif



/******************************************************************************
 *  Function (Kernel space only): MOSAL_iobuf_get_props
 *
 *  Description: get the properties of the iobuf
 *
 *  Parameters:
 *       iobuf(in) iobuf object
 *       props_p(out) pointer by which to return props
 *       
 *
 *  Returns:
 *    call_result_t
 *        MT_OK       success
 *
 ******************************************************************************/
call_result_t MOSAL_iobuf_get_props(MOSAL_iobuf_t iobuf,
                                    MOSAL_iobuf_props_t *props_p);



/******************************************************************************
 *  Function (Kernel space only): MOSAL_iobuf_get_tpt
 *
 *  Description: get the tpt of the iobuf
 *
 *  Parameters:
 *       iobuf(in) iobuf object
 *       npages(in) number of entries in pa_arr
 *       pa_arr(out) pointer to hold physical addresses
 *       page_size_p(out) where to copy the page size
 *                       (may be null if output is not desired)
 *       act_table_sz_p(out) where to copy actual number of entries in the table
 *                           (may be null if output is not desired)
 *
 *  Returns:
 *    call_result_t
 *        MT_OK       success
 *  Notes: the physical addresses of the pages are returned as from the start of the buffer
 *         to up to npages translation or the max translations on the buffer - the minimum
 *         between the two
 ******************************************************************************/
call_result_t MOSAL_iobuf_get_tpt(MOSAL_iobuf_t iobuf,
                                  u_int32_t npages,
                                  MT_phys_addr_t *pa_arr,
                                  u_int32_t *page_size_p,
                                  u_int32_t *act_table_sz_p);


/******************************************************************************
 *  Function (Kernel space only): MOSAL_iobuf_cmp_tpt
 *
 *  Description: compare the tpt of two iobufs
 *
 *  Parameters:
 *       iobuf_1(in) first iobuf object
 *       iobuf_2(in) second iobuf object
 *
 *  Returns:
 *    0    tpt's are equal
 *    != 0 tpt's differ
 *  Notes: The function compares iobufs belonging to the same protection context.
 *         If the protection context differs it returns -1; If the protection context
 *         is kernel than only the va and size of both iobus are compared. It is assumed
 *         that there can be no two vitual addresses that map to the same physical address.
 *         In user space it could be that the sizes of the buffers are not equal bu the
 *         number of pages is equal. In that case if the physical addresses of the two
 *         buffers are equal then the iobufs are considered equal
 *        
 ******************************************************************************/
int MOSAL_iobuf_cmp_tpt(MOSAL_iobuf_t iobuf_1, MOSAL_iobuf_t iobuf_2);


/******************************************************************************
 *  Function (Kernel space only): MOSAL_iobuf_get_tpt_seg
 *
 *  Description: get a number of entries from the tpt as specified by the iterator
 *               and the n_pages_in param
 *
 *  Parameters:
 *         iobuf(in) iobuf object
 *         iterator_p(in/out) iterator for accessing translation tables
 *         n_pages_in(in) number of translations required
 *         n_pages_out_p(out) number of translations provided
 *         page_tbl_p(out) pointer to array where to return translations
 *
 *  Returns:
 *         MT_OK
 ******************************************************************************/
call_result_t MOSAL_iobuf_get_tpt_seg(MOSAL_iobuf_t iobuf, MOSAL_iobuf_iter_t *iterator_p,
                                      MT_size_t n_pages_in, MT_size_t *n_pages_out_p,
                                      MT_phys_addr_t *page_tbl_p);


/******************************************************************************
 *  Function (Kernel space only): MOSAL_iobuf_iter_init
 *
 *  Description: initialize the iterator to the begginig of the translation
 *               table
 *
 *  Parameters:
 *         iobuf(in) iobuf object
 *         iterator_p(out) iterator to initialize
 *
 *  Returns:
 *         MT_OK
 ******************************************************************************/
call_result_t MOSAL_iobuf_iter_init(MOSAL_iobuf_t iobuf, MOSAL_iobuf_iter_t *iterator_p);


#endif /* __MOSAL_IOBUF_H_ */
