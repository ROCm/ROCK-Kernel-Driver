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

#define PAGESHIFT 12
#define PAGESIZE  (1UL << PAGESHIFT)


#define __ARCH_MIN_PAGE_SIZE PAGESIZE 

/*
 * Mosal memory managment types
 */
/*  TDB: All were "%u" */
#define SIZE_T_XFMT    "0x%X"
#define SIZE_T_DFMT    "%u"
#define SIZE_T_FMT    SIZE_T_XFMT 
#define OFF_T_FMT     "0x%X"
#define VIRT_ADDR_FMT "0x%X"
#define PHYS_ADDR_FMT "0x%X"
#define U64_FMT       "0x%llX"
#define MT_ULONG_PTR_FMT "0x%X"
#define MT_PID_FMT "%d"
typedef u_int32_t MT_phys_addr_t;
typedef u_int32_t MT_virt_addr_t;
typedef u_int32_t MT_offset_t;
typedef u_int32_t MT_size_t;
typedef int16_t MT_half_ptr_t;
typedef int32_t MT_long_ptr_t;
typedef u_int16_t MT_uhalf_ptr_t;
typedef u_int32_t MT_ulong_ptr_t;

#endif /* H_MTL_ARCH_TYPES_H */
