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


#include <linux/kernel.h>
#include "ieee754dp.h"

int ieee754dp_tint(ieee754dp x)
{
	COMPXDP;

	CLEARCX;

	EXPLODEXDP;

	switch (xc) {
	case IEEE754_CLASS_SNAN:
	case IEEE754_CLASS_QNAN:
		SETCX(IEEE754_INVALID_OPERATION);
		return ieee754si_xcpt(ieee754si_indef(), "fixdp", x);
	case IEEE754_CLASS_INF:
		SETCX(IEEE754_OVERFLOW);
		return ieee754si_xcpt(ieee754si_indef(), "fixdp", x);
	case IEEE754_CLASS_ZERO:
		return 0;
	case IEEE754_CLASS_DNORM:	/* much to small */
		SETCX(IEEE754_UNDERFLOW);
		return ieee754si_xcpt(0, "fixdp", x);
	case IEEE754_CLASS_NORM:
		break;
	}
	if (xe >= 31) {
		SETCX(IEEE754_OVERFLOW);
		return ieee754si_xcpt(ieee754si_indef(), "fix", x);
	}
	if (xe < 0) {
		SETCX(IEEE754_UNDERFLOW);
		return ieee754si_xcpt(0, "fix", x);
	}
	/* oh gawd */
	if (xe > DP_MBITS) {
		xm <<= xe - DP_MBITS;
	} else if (xe < DP_MBITS) {
		/* XXX no rounding 
		 */
		xm >>= DP_MBITS - xe;
	}
	if (xs)
		return -xm;
	else
		return xm;
}


unsigned int ieee754dp_tuns(ieee754dp x)
{
	ieee754dp hb = ieee754dp_1e31();

	/* what if x < 0 ?? */
	if (ieee754dp_lt(x, hb))
		return (unsigned) ieee754dp_tint(x);

	return (unsigned) ieee754dp_tint(ieee754dp_sub(x, hb)) |
	    ((unsigned) 1 << 31);
}
