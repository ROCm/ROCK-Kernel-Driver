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

  $Id: header_main.c 50 2004-04-13 20:53:33Z roland $
*/

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#ifndef W2K_OS
#  include <linux/config.h>
#endif
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  ifndef W2K_OS
#    include <linux/modversions.h>
#  endif
#endif

#include "ts_ib_header.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/init.h>
#include <linux/errno.h>

static uint64_t ib_value_read(int   offset,
                              int   size,
                              void *header)
{
	switch (size * 8) {
	case 8:
		return *(uint8_t *) (header + offset);

	case 16:
		return *(uint16_t *) (header + offset);

	case 32:
		return *(uint32_t *) (header + offset);

	case 64:
		return *(uint64_t *) (header + offset);

	default:
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Field size %d bits not handled", size * 8);
		return 0;
	}
}

void ib_header_pack(const struct ib_header_field *desc,
		    int                           desc_len,
		    void                         *header,
		    void                         *buf)
{
	int i;

	for (i = 0; i < desc_len; ++i) {
		if (desc[i].header_size_bits <= 32) {
			int shift;
			uint32_t  val;
			uint32_t  mask;
			uint32_t *addr;

			shift = 32 - desc[i].header_offset_bits - desc[i].header_size_bits;
			if (desc[i].struct_size_bytes) {
				val = ib_value_read(desc[i].struct_offset_bytes,
						    desc[i].struct_size_bytes,
						    header) << shift;
			} else {
				val = 0;
			}
			mask = ((1ull << desc[i].header_size_bits) - 1) << shift;
			addr = (uint32_t *) buf + desc[i].header_offset_words;
			*addr = cpu_to_be32((be32_to_cpup(addr) & ~mask) | (val & mask));
		} else if (desc[i].header_size_bits == 64) {
			uint64_t *addr;

			if (desc[i].header_offset_bits % 8) {
				TS_REPORT_WARN(MOD_KERNEL_IB,
					       "64-bit header field %s is not word-aligned",
					       desc[i].field_name);
			}
			addr = (uint64_t *) ((uint32_t *) buf + desc[i].header_offset_words);
			*addr = cpu_to_be64(ib_value_read(desc[i].struct_offset_bytes,
							  desc[i].struct_size_bytes,
							  header));
		} else {
			if (desc[i].header_offset_bits % 8 ||
			    desc[i].header_size_bits   % 8) {
				TS_REPORT_WARN(MOD_KERNEL_IB,
					       "Header field %s of size %d bits is not byte-aligned",
					       desc[i].field_name, desc[i].header_size_bits);
			}

			memcpy(buf + desc[i].header_offset_words * 4 + 
			       desc[i].header_offset_bits / 8,
			       header + desc[i].struct_offset_bytes,
			       desc[i].header_size_bits / 8);
		}
	}
}

static void ib_value_write(int      offset,
                           int      size,
                           uint64_t val,
                           void    *header)
{
	switch (size * 8) {
	case 8:
		*(uint8_t *) (header + offset) = val;
		break;

	case 16:
		*(uint16_t *) (header + offset) = val;
		break;

	case 32:
		*(uint32_t *) (header + offset) = val;
		break;

	case 64:
		*(uint64_t *) (header + offset) = val;
		break;

	default:
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Field size %d bits not handled", size * 8);
	}
}

void ib_header_unpack(const struct ib_header_field *desc,
		      int                           desc_len,
		      void                         *buf,
		      void                         *header)
{
	int i;

	for (i = 0; i < desc_len; ++i) {
		if (!desc[i].struct_size_bytes) {
			continue;
		}
		if (desc[i].header_size_bits <= 32) {
			int shift;
			uint32_t  val;
			uint32_t  mask;
			uint32_t *addr;

			shift = 32 - desc[i].header_offset_bits - desc[i].header_size_bits;
			mask = ((1ull << desc[i].header_size_bits) - 1) << shift;
			addr = (uint32_t *) buf + desc[i].header_offset_words;
			val = (be32_to_cpup(addr) & mask) >> shift;
			ib_value_write(desc[i].struct_offset_bytes,
				       desc[i].struct_size_bytes,
				       val,
				       header);
		} else if (desc[i].header_size_bits == 64) {
			uint64_t *addr;

			if (desc[i].header_offset_bits % 8) {
				TS_REPORT_WARN(MOD_KERNEL_IB,
					       "64-bit header field %s is not word-aligned",
					       desc[i].field_name);
			}
			addr = (uint64_t *) ((uint32_t *) buf + desc[i].header_offset_words);
			ib_value_write(desc[i].struct_offset_bytes,
				       desc[i].struct_size_bytes,
				       be64_to_cpup(addr),
				       header);
		} else {
			if (desc[i].header_offset_bits % 8 ||
			    desc[i].header_size_bits   % 8) {
				TS_REPORT_WARN(MOD_KERNEL_IB,
					       "Header field %s of size %d bits is not byte-aligned",
					       desc[i].field_name, desc[i].header_size_bits);
			}

			memcpy(header + desc[i].struct_offset_bytes,
			       buf + desc[i].header_offset_words * 4 +
			       desc[i].header_offset_bits / 8,
			       desc[i].header_size_bits / 8);
		}
	}
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
