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


#include <mtl_common.h>
#include <vapi_types.h>
#include <hh_common.h>

#if defined(__DARWIN__) && defined (MT_KERNEL)
#ifndef abs
#define abs(x) ((x)>0?(x):-(x))
#endif
#endif

/*  We cannot use simple and efficient switch/cases,
 *  since HH_ERROR_LIST has some different symbols with equal values!
 */

enum { E_SYMBOL, E_MESSAGE };


#undef HH_ERROR_INFO
#define HH_ERROR_INFO(A, B, C) {#A, C},
#define ERR_TABLE_SIZE (-VAPI_EGEN + 1)
static const char *tab[ERR_TABLE_SIZE][2] = {
  HH_ERROR_LIST
};
#undef HH_ERROR_INFO

static const int32_t err_nums[] = {
#define HH_ERROR_INFO(A, B, C) A,
  HH_ERROR_LIST
#undef HH_ERROR_INFO
  HH_ERROR_DUMMY_CODE
};
#define ERR_NUMS_SIZE	(sizeof(err_nums)/sizeof(int32_t))


/************************************************************************/
static const char* HH_strerror_t(HH_ret_t errnum, unsigned int te)
{
  const char*         s = "HH Unknown Error";
  int                 ie = ERR_TABLE_SIZE;
  int i;
  for (i=0; (err_nums[i] != HH_ERROR_DUMMY_CODE) && (i < ERR_NUMS_SIZE); i++)
  { 
    if (err_nums[i] == errnum)
    {
      ie = i;
      break;
    }
  }
  if (ie < ERR_TABLE_SIZE)
  {
    const char*  ts = tab[ie][te];
    if (ts) { s = ts; }
  }
  return s;
} /* HH_strerror_t */


/************************************************************************/
const char* HH_strerror(HH_ret_t errnum)
{
  return HH_strerror_t(errnum, E_MESSAGE);
} /* HH_strerror */


/************************************************************************/
const char* HH_strerror_sym(HH_ret_t errnum)
{
  return HH_strerror_t(errnum, E_SYMBOL);
} /* HH_strerror_sym */


#if defined(HH_COMMON_TEST)
/* Compile by
  gcc -g -Wall -DHH_COMMON_TEST -I. -I$MTHOME/include -o /tmp/hhc hh_common.c
 */
#include <stdio.h>
int main(int argc, char** argv)
{
  if (argc < 2)        
  {
    fprintf(stderr, "Usage: %s <error-code list>\n", argv[0]);
  }
  else
  {
     int  ai;
     for (ai = 1;  ai != argc;  ++ai)
     {
        int  errrc = atol(argv[ai]);
        printf("[%d] errrc=%d, sym=%s, doc=%s\n", ai, errrc,
               HH_strerror_sym(errrc), HH_strerror(errrc));
     }
  }
  return 0;
} /* main */

#endif
