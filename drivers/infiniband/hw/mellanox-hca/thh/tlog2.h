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

#if !defined(_TLOG2_H)
#define _TLOG2_H

#include <mtl_common.h>

#ifdef  __cplusplus
 extern "C" {
#endif


/************************************************************************
 * Function: floor_log2(n)
 *    floor_log2(0) = 0                 if n=0
 *    floor_log2(n) = floor(log_2(n))   if n>=1
 *
 * Better than formal description:
 *
 *  floor_log2(0) = 0
 *  floor_log2(1) = 0
 *  floor_log2(2) = 1
 *  floor_log2(3) = 1
 *  floor_log2(4) = 2
 *  floor_log2(5) = 2
 *  ...
 *  floor_log2(15) = 3
 *  floor_log2(16) = 4
 *  floor_log2(17) = 4
 *  ...
 */
extern unsigned int  floor_log2(u_int64_t x);
/* extern unsigned int  tlog2(u_int64_t x); / * obsolete, use floor_log2 */

/************************************************************************
 * Function: ceil_log2(n) 
 *
 * Minimal p>=0, such that  x <= 2^p.
 *    ceil_log2(0) = 0                if n=0
 *    ceil_log2(n) = ceil(log_2(n))   if n>=1
 *
 * Better than formal description:
 *
 *  ceil_log2(0) = 0
 *  ceil_log2(1) = 0
 *  ceil_log2(2) = 1
 *  ceil_log2(3) = 2
 *  ceil_log2(4) = 2
 *  ceil_log2(5) = 3
 *  ...
 *  ceil_log2(15) = 4
 *  ceil_log2(16) = 4
 *  ceil_log2(17) = 5
 *  ...
 */
extern unsigned int  ceil_log2(u_int64_t x);

/************************************************************************
 * Function: lowest_bit(x) 
 * 
 * If x=0 return 64. Otherwise return minimal b such that  ((1<<b) & x) != 0.
 *
 * Intuitively, a mirror of floor_log2, which picks the highest bit.
 * Better than formal description:
 *   
 *   lowest_bit(0x0)  = 64
 *   lowest_bit(0x1)  = 0
 *   lowest_bit(0x2)  = 1
 *   lowest_bit(0x3)  = 0
 *   lowest_bit(0x4)  = 2
 *   lowest_bit(0x5)  = 0
 *   lowest_bit(0x6)  = 1
 *   lowest_bit(0x7)  = 0
 *   lowest_bit(0x8)  = 3
 *   lowest_bit(0x9)  = 0
 *   ...
 *   lowest_bit(0xf)  = 0
 *   lowest_bit(0x10) = 4
 *   lowest_bit(0x11) = 0
 *   ...
 *   lowest_bit(0x1f) = 0
 *   lowest_bit(0x20) = 5
 *
 */
extern unsigned int  lowest_bit(u_int64_t x);

#ifdef  __cplusplus
 }
#endif

#endif /* _TLOG2_H */
