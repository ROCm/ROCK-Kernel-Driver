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

  $Id: trace_codes.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _ALL_COMMON_TRACE_CODES_H
#define _ALL_COMMON_TRACE_CODES_H

/*
  This include file is included in both common library code and the
  kernel trace module.  Don't add any more declarations or definitions
  to this file; create a new file instead!
*/


/*
  any changes made here should also be made in the following places:
  - mgmt/rapidlogic/util_for_rl.c, mgmt_rl_getTraceLevelId()
  - RapidControl, custom type "tTraceLevel"
*/
typedef enum {
    T_NO_DISPLAY     	= 0x0,
    T_VERY_TERSE     	= 0x1,
    T_TERSE          	= 0x2,
    T_VERBOSE        	= 0x3,
    T_VERY_VERBOSE   	= 0x4,
    T_SCREAM         	= 0x5,
    T_MAX
} tTS_TRACE_LEVEL;


/* system default values */
#define TS_TRACE_LEVEL_USER_DEFVAL      T_NO_DISPLAY
#define TS_TRACE_LEVEL_KERNEL_DEFVAL    T_TERSE

#endif /* _ALL_COMMON_TRACE_CODES_H */
