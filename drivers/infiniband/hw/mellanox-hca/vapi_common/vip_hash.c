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

#include <mtl_types.h>

static const unsigned long prime_list[] =
{
 /*approx 100% table size growth per resize (except for 1st entry)*/
 7ul,          53ul,         97ul,         193ul,        389ul,
 769ul,        1543ul,       3079ul,       6151ul,

 /*approx 20% table size growth per resize */
 12289ul,      14747ul,      17707ul,      21269ul,      25523ul,
 30631ul,      36761ul,      44119ul,      52951ul,      63559ul,
 76283ul,      91541ul,      109859ul,     131837ul,     158209ul,
 189851ul,     227827ul,     273433ul,     328121ul,     393749ul,
 472523ul,     567031ul,     680441ul,     816539ul,     979873ul,
 1175849ul,    1411021ul,    1693249ul,    2031907ul,    2438309ul,
 2925973ul,    3511171ul,    4194301ul,    5056133ul,    6067361ul,
 7280863ul,    8737039ul,    10484471ul,   12581407ul,   15097711ul, 16777213ul,
 18117271ul,   21740729ul,   26088911ul,   31306697ul,   37568051ul,
 45081683ul,   54098059ul,   64917691ul,   77901247ul,   93481541ul,
 112177873ul,  134613491ul,  161536217ul,  193843493ul,  232612217ul,
 279134677ul,  334961647ul,  401953999ul,  482344801ul,  578813771ul,
 694576537ul,  833491849ul,  1000190263ul, 1200228319ul, 1440273997ul,
 1728328807ul, 2073994579ul, 2488793497ul, 2986552201ul, 3583862647ul,
 4294967291ul
};
static const int n_primes = sizeof(prime_list)/sizeof(prime_list[0]);

static unsigned long mtl_find_prime(unsigned long size) {
  int  i;
  for (i = 0 ; (i != n_primes-1) && (size >= prime_list[i]) ;++i);
  return prime_list[i];
}


/************************************************************************/
static inline  u_int32_t  hash_u64tou32(u_int64_t u64)
{
   u_int32_t  high = (u_int32_t) (u64 >> 32);
   u_int32_t  low  = (u_int32_t) (u64 & ~(u_int32_t)0);
   u_int32_t  h = high ^ low;
   return h;
} /* hash_u64tou32 */


/************************************************************************/
static inline  u_int32_t  hash_uv4tou32(u_int32_t *uv4)
{
   u_int32_t  h = uv4[0] ^ uv4[1] ^ uv4[2] ^ uv4[3];
   return h;
} /* hash_u64tou32 */


#include "vip_hash.h"
#include "vip_hash.ic"  /* 1st time, now  __VIP_HASH_VARIANT == 0 */

#include "vip_hashp.h"
#include "vip_hash.ic"  /* 2nd time, now  __VIP_HASH_VARIANT == 1 */

#include "vip_hashp2p.h"
#include "vip_hash.ic"  /* 3rd time, now  __VIP_HASH_VARIANT == 2 */

#include "vip_hash64p.h"
#include "vip_hash.ic"  /* 4th time, now  __VIP_HASH_VARIANT == 3 */

#include "vip_hashv4p.h"
#include "vip_hash.ic"  /* 5th time, now  __VIP_HASH_VARIANT == 4 */

