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

  $Id: ts_kernel_version.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_KERNEL_VERSION_H
#define _TS_KERNEL_VERSION_H

#if defined(IN_TREE_BUILD)
#define TS_VER_FILE(base, name) "linux/modversions.h"
#else
#define TS_VER_FILE(base, name) _TS_VER_FILE(base,TS_OBJ_DIR,name)
#define _TS_VER_FILE(base, obj, name) __TS_VER_FILE(base/obj/name)
#define __TS_VER_FILE(path) # path
#endif

#endif /* _TS_KERNEL_VERSION_H */
