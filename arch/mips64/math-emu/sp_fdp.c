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

ieee754sp ieee754sp_fdp(ieee754dp x)
{
	COMPXDP;

	CLEARCX;

	EXPLODEXDP;

	switch (xc) {
	case IEEE754_CLASS_QNAN:
	case IEEE754_CLASS_SNAN:
		return ieee754sp_nanxcpt(buildsp(xs,
						 SP_EMAX + 1 + SP_EBIAS,
						 (unsigned long)
						 (xm >>
						  (DP_MBITS - SP_MBITS))),
					 "fdp", x);
	case IEEE754_CLASS_INF:
		return ieee754sp_inf(xs);
	case IEEE754_CLASS_ZERO:
		return ieee754sp_zero(xs);
	case IEEE754_CLASS_DNORM:
		/* cant possibly be sp representable */
		SETCX(IEEE754_UNDERFLOW);
		return ieee754sp_xcpt(ieee754sp_zero(xs), "fdp", x);
	case IEEE754_CLASS_NORM:
		break;
	}

	{
		unsigned long rm;

		/* convert from DP_MBITS to SP_MBITS+3 with sticky right shift 
		 */
		rm = (xm >> (DP_MBITS - (SP_MBITS + 3))) |
		    ((xm << (64 - (DP_MBITS - (SP_MBITS + 3)))) != 0);

		SPNORMRET1(xs, xe, rm, "fdp", x);
	}
}
