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

  $Id: ts_ib_header.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_HEADER_H
#define _TS_IB_HEADER_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../header,header_export.ver)
#endif

#include "ts_ib_header_types.h"

void ib_header_pack(const struct ib_header_field *desc,
		    int                           desc_len,
		    void                         *header,
		    void                         *buf);

void ib_header_unpack(const struct ib_header_field *desc,
                      int                           desc_len,
                      void                         *buf,
                      void                         *header);

void ib_ud_header_init(int     		   payload_bytes,
		       int    		   grh_present,
		       struct ib_ud_header *header);

int ib_ud_header_pack(struct ib_ud_header *header,
		      void                *buf);

int ib_ud_header_unpack(void                *buf,
			struct ib_ud_header *header);

/* Defines to support legacy code -- don't use the tsIb names in new code. */
#define tsIbUdHeaderInit    ib_ud_header_init
#define tsIbUdHeaderPack    ib_ud_header_pack
#define tsIbUdHeaderUnpack  ib_ud_header_unpack

#endif /* _TS_IB_HEADER_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
