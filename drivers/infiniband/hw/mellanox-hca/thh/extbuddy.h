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

#if !defined(_ExtBuddy_H)
#define _ExtBuddy_H

#include <mtl_common.h>

typedef struct Extbuddy_st*  Extbuddy_hndl;
#define EXTBUDDY_NULL ((const u_int32_t) ~0ul)

#ifdef  __cplusplus
 extern "C" {
#endif
extern Extbuddy_hndl  extbuddy_create(u_int32_t size,  u_int8_t log2_min_chunk);
extern void           extbuddy_destroy(Extbuddy_hndl handle);
extern u_int32_t      extbuddy_alloc(Extbuddy_hndl handle, u_int8_t log2_sz);
extern u_int32_t      extbuddy_alloc_bound(
                        Extbuddy_hndl handle, 
                        u_int8_t      log2_sz,
                        u_int32_t     area_start,
                        u_int32_t     area_size
                        );
extern MT_bool        extbuddy_free(
                        Extbuddy_hndl handle,
                        u_int32_t     p, 
                        u_int8_t      log2_sz);
extern unsigned int   extbuddy_chunks_available(
                        Extbuddy_hndl handle, 
                        u_int8_t log2_sz);
extern unsigned int   extbuddy_total_available(Extbuddy_hndl handle);
extern int            extbuddy_log2_max_available(Extbuddy_hndl handle);
extern void           extbuddy_query_chunks(
                        Extbuddy_hndl handle,
                        u_int8_t      log2_sz,
                        unsigned int  ptrs_buff_sz,
                        u_int32_t*    ptrs_buff);

/* reserve interface */
extern MT_bool        extbuddy_reserve(  /* Only before any allocation */
                        Extbuddy_hndl handle,
                        u_int32_t     p,
                        u_int32_t     size /* here, not a log2 */); 

#ifdef  __cplusplus
 }
#endif


#endif /* _ExtBuddy_H */
