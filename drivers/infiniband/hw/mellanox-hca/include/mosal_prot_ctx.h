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

#ifndef __MOSAL_PROT_CTX_H
#define __MOSAL_PROT_CTX_H

typedef mosal_prot_ctx_t MOSAL_prot_ctx_t;
typedef mosal_pid_t MOSAL_pid_t;

typedef MOSAL_prot_ctx_t MOSAL_protection_ctx_t;  /* keep for backward compatibility */

#define MOSAL_get_current_prot_ctx() mosal_get_current_prot_ctx() 
#define MOSAL_get_kernel_prot_ctx() mosal_get_kernel_prot_ctx() 

#ifdef __KERNEL__
#define MY_PROT_CTX MOSAL_get_kernel_prot_ctx()
#else
#define MY_PROT_CTX  MOSAL_get_current_prot_ctx()
#endif

#endif /* __MOSAL_PROT_CTX_H */
