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



#ifndef H_MT_BIT_OPS_H
#define H_MT_BIT_OPS_H

/*****************************************************************************************
 * Bit manipulation macros
 *****************************************************************************************/

/* MASK generate a bit mask S bits width */
#define MASK32(S)         ( ((u_int32_t) ~0L) >> (32-(S)) )

/*
 * BITS generate a bit mask with bits O+S..O set (assumes 32 bit integer).
 *      numbering bits as following:    31........................76543210
 */
#define BITS32(O,S)       ( MASK32(S) << (O) )

/* 
 * MT_EXTRACT32 macro extracts S bits from (u_int32_t)W with offset O 
 *  and shifts them O places to the right (right justifies the field extracted).
 */
#define MT_EXTRACT32(W,O,S)  ( ((W)>>(O)) & MASK32(S) )

/*
 * MT_INSERT32 macro inserts S bits with offset O from field F into word W (u_int32_t)
 */
#define MT_INSERT32(W,F,O,S) ((W)= ( ( (W) & (~BITS32(O,S)) ) | (((F) & MASK32(S))<<(O)) ))


/*
 * MT_EXTRACT_ARRAY32 macro is similar to EXTRACT but works on an array of (u_int32_t),
 * thus offset may be larger than 32 (but not size).
 */
#define MT_EXTRACT_ARRAY32(A,O,S) MT_EXTRACT32(((u_int32_t*)A)[O >> 5],(O & MASK32(5)),S)

/*
 * MT_INSERT_ARRAY32 macro is similar to INSERT but works on an array of (u_int32_t),
 * thus offset may be larger than 32 (but not size).
 */
#define MT_INSERT_ARRAY32(A,F,O,S) MT_INSERT32(((u_int32_t*)A)[O >> 5],F,(O & MASK32(5)),S)


/* swap 32 bit number */
#define mswab32(x) ((((x) >> 24)&0xff) | (((x) >> 8)&0xff00) | (((x) << 8)&0xff0000) | (((x) << 24)&0xff000000))


#endif  /* H_MTL_COMMON_H */
