/* IEEE754 floating point arithmetic
 * double precision: common utilities
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.  All rights reserved.
 * http://www.algor.co.uk
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 */


#include "ieee754dp.h"

long long ieee754dp_tlong(ieee754dp x)
{
	COMPXDP;

	CLEARCX;

	EXPLODEXDP;

	switch (xc) {
	case IEEE754_CLASS_SNAN:
	case IEEE754_CLASS_QNAN:
		SETCX(IEEE754_INVALID_OPERATION);
		return ieee754di_xcpt(ieee754di_indef(), "dp_tlong", x);
	case IEEE754_CLASS_INF:
		SETCX(IEEE754_OVERFLOW);
		return ieee754di_xcpt(ieee754di_indef(), "dp_tlong", x);
	case IEEE754_CLASS_ZERO:
		return 0;
	case IEEE754_CLASS_DNORM:	/* much too small */
		SETCX(IEEE754_UNDERFLOW);
		return ieee754di_xcpt(0, "dp_tlong", x);
	case IEEE754_CLASS_NORM:
		break;
	}
	if (xe >= 63) {
		SETCX(IEEE754_OVERFLOW);
		return ieee754di_xcpt(ieee754di_indef(), "dp_tlong", x);
	}
	if (xe < 0) {
		if (ieee754_csr.rm == IEEE754_RU) {
			if (xs) {	/* Negative  */
				return 0x0000000000000000LL;
			} else {	/* Positive */
				return 0x0000000000000001LL;
			}
		} else if (ieee754_csr.rm == IEEE754_RD) {
			if (xs) {	/* Negative , return -1 */
				return 0xffffffffffffffffLL;
			} else {	/* Positive */
				return 0x0000000000000000LL;
			}
		} else {
			SETCX(IEEE754_UNDERFLOW);
			return ieee754di_xcpt(0, "dp_tlong", x);
		}
	}
	/* oh gawd */
	if (xe > DP_MBITS) {
		xm <<= xe - DP_MBITS;
	} else if (xe < DP_MBITS) {
		unsigned long long residue;
		unsigned long long mask = 0;
		int i;
		int round;
		int sticky;
		int odd;

		/* compute mask */
		for (i = 0; i < DP_MBITS - xe; i++) {
			mask = mask << 1;
			mask = mask | 0x1;
		}
		residue = (xm & mask) << (64 - (DP_MBITS - xe));
		round =
		    ((0x8000000000000000LL & residue) !=
		     0x0000000000000000LL);
		sticky =
		    ((0x7fffffffffffffffLL & residue) !=
		     0x0000000000000000LL);

		xm >>= DP_MBITS - xe;

		odd = ((xm & 0x1) != 0x0000000000000000LL);

		/* Do the rounding */
		if (!round && sticky) {
			if ((ieee754_csr.rm == IEEE754_RU && !xs)
			    || (ieee754_csr.rm == IEEE754_RD && xs)) {
				xm++;
			}
		} else if (round && !sticky) {
			if ((ieee754_csr.rm == IEEE754_RU && !xs)
			    || (ieee754_csr.rm == IEEE754_RD && xs)
			    || (ieee754_csr.rm == IEEE754_RN && odd)) {
				xm++;
			}
		} else if (round && sticky) {
			if ((ieee754_csr.rm == IEEE754_RU && !xs)
			    || (ieee754_csr.rm == IEEE754_RD && xs)
			    || (ieee754_csr.rm == IEEE754_RN)) {
				xm++;
			}
		}
	}
	if (xs)
		return -xm;
	else
		return xm;
}


unsigned long long ieee754dp_tulong(ieee754dp x)
{
	ieee754dp hb = ieee754dp_1e63();

	/* what if x < 0 ?? */
	if (ieee754dp_lt(x, hb))
		return (unsigned long long) ieee754dp_tlong(x);

	return (unsigned long long) ieee754dp_tlong(ieee754dp_sub(x, hb)) |
	    (1ULL << 63);
}
