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

#ifndef H_MTL_SYS_TYPES_H
#define H_MTL_SYS_TYPES_H

#define MT_API	

#ifndef FALSE
#define FALSE 0
#undef TRUE
#define TRUE  (!FALSE)
#endif

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <sys/types.h>
#endif


#undef MT_64BIT
#ifdef __i386__
#   include "mtl_x86_types.h"
#elif defined(powerpc) || defined(__powerpc__)
#   include "mtl_powerpc_types.h"
#elif defined __ia64__
#   include "mtl_ia64_types.h"
#   define MT_64BIT
#elif defined __x86_64__
#   define MT_64BIT
#   include "mtl_x86_64_types.h"
#else
#   error Platform is not supported yet
#endif


#define MT_BYTE_ALIGN(n)  __attribute__ ((aligned(n)))




#endif /* H_MTL_SYS_TYPES_H */
