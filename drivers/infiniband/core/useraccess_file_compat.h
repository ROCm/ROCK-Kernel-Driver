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

  $Id: useraccess_file_compat.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _USERACCESS_FILE_COMPAT_H
#define _USERACCESS_FILE_COMPAT_H

#if defined(__linux__)

#define TS_FILE_OP_PARAMS(inode, filp) \
    struct inode *inode, \
    struct file  *filp

#define TS_READ_PARAMS(filp, buf, count, pos) \
    struct file *filp, \
    char *buf,         \
    size_t count,      \
    loff_t *pos

#define TS_WRITE_PARAMS(filp, buf, count, pos) \
    struct file *filp, \
    const char *buf,   \
    size_t count,      \
    loff_t *pos

#define TS_IB_USER_PRIV_FROM_FILE(filep) ((tTS_IB_USERACCESS_PRIVATE) (filep)->private_data)

#else
#error __linux__ not defined, no support for this OS
#endif

#endif /* _USERACCESS_FILE_COMPAT_H */
