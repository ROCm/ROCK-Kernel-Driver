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

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: ts_kernel_uintptr.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_KERNEL_UINTPTR_H
#define _TS_KERNEL_UINTPTR_H

/*
  We define a uintptr_t type for use in kernel space.  To ease
  portability to Windows (in particular Win64), uintptr_t should be
  used in preference to "unsigned long" when a pointer is cast to an
  integral type.  This is because unsigned long is always 32 bits
  under Windows, so under Win64, casting a pointer to unsigned long
  will truncate the pointer.
*/

typedef unsigned long uintptr_t;

#endif /* _TS_KERNEL_UINTPTR_H */
