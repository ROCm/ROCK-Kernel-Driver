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

#ifndef H_MTL_ARCH_TYPES_H
#define H_MTL_ARCH_TYPES_H



/*
 * Memory sizes
 *
 */

#define PAGESHIFT PAGE_SHIFT
#define PAGESIZE  PAGE_SIZE


/*
 * Mosal memory managment types
 */

#define SIZE_T_FMT    "0x%lX"
#define SIZE_T_XFMT    "0x%lX"
#define SIZE_T_DFMT    "%lu"
#define OFF_T_FMT     "0x%lX"
#define VIRT_ADDR_FMT "0x%lX"
#define PHYS_ADDR_FMT "0x%lX"
#define U64_FMT       "0x%lX"
#define MT_ULONG_PTR_FMT "0x%lX"
#define MT_PID_FMT "%lu"
typedef u_int64_t MT_phys_addr_t;
typedef u_int64_t MT_virt_addr_t;
typedef u_int64_t MT_offset_t;
typedef u_int64_t MT_size_t;
typedef int32_t MT_half_ptr_t;
typedef int64_t MT_long_ptr_t;
typedef u_int32_t MT_uhalf_ptr_t;
typedef u_int64_t MT_ulong_ptr_t;

#endif /* H_MTL_ARCH_TYPES_H */
