/* IEEE754 floating point arithmetic
 * single precision
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


#include "ieee754sp.h"

long long ieee754sp_tlong(ieee754sp x)
{
	COMPXDP;		/* <-- need 64-bit mantissa tmp */

	CLEARCX;

	EXPLODEXSP;

	switch (xc) {
	case IEEE754_CLASS_SNAN:
	case IEEE754_CLASS_QNAN:
		SETCX(IEEE754_INVALID_OPERATION);
		return ieee754di_xcpt(ieee754di_indef(), "sp_tlong", x);
	case IEEE754_CLASS_INF:
		SETCX(IEEE754_OVERFLOW);
		return ieee754di_xcpt(ieee754di_indef(), "sp_tlong", x);
	case IEEE754_CLASS_ZERO:
		return 0;
	case IEEE754_CLASS_DNORM:	/* much to small */
		SETCX(IEEE754_UNDERFLOW);
		return ieee754di_xcpt(0, "sp_tlong", x);
	case IEEE754_CLASS_NORM:
		break;
	}
	if (xe >= 63) {
		SETCX(IEEE754_OVERFLOW);
		return ieee754di_xcpt(ieee754di_indef(), "sp_tlong", x);
	}
	if (xe < 0) {
		SETCX(IEEE754_UNDERFLOW);
		return ieee754di_xcpt(0, "sp_tlong", x);
	}
	/* oh gawd */
	if (xe > SP_MBITS) {
		xm <<= xe - SP_MBITS;
	} else if (xe < SP_MBITS) {
		/* XXX no rounding 
		 */
		xm >>= SP_MBITS - xe;
	}
	if (xs)
		return -xm;
	else
		return xm;
}


unsigned long long ieee754sp_tulong(ieee754sp x)
{
	ieee754sp hb = ieee754sp_1e63();

	/* what if x < 0 ?? */
	if (ieee754sp_lt(x, hb))
		return (unsigned long long) ieee754sp_tlong(x);

	return (unsigned long long) ieee754sp_tlong(ieee754sp_sub(x, hb)) |
	    (1ULL << 63);
}
