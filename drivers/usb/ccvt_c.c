/*
   Colour conversion routines (RGB <-> YUV) in plain C
   (C) 2000-2001 Nemosoft Unv.    nemosoft@smcc.demon.nl
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include "ccvt.h"
#include "vcvt.h"

/* We use the viewport routines, with a viewport width that is exactly
   the same as the image width. The overhead for computing the view/image
   offsets is very small anyway.
   
   The assembly routines are still different, since they are quite optimized.
 */
void ccvt_420i_rgb24(int width, int height, void *src, void *dst)
{
	vcvt_420i_rgb24(width, height, width, src, dst);
}

void ccvt_420i_bgr24(int width, int height, void *src, void *dst)
{
	vcvt_420i_bgr24(width, height, width, src, dst);
}

void ccvt_420i_rgb32(int width, int height, void *src, void *dst)
{
	vcvt_420i_rgb32(width, height, width, src, dst);
}

void ccvt_420i_bgr32(int width, int height, void *src, void *dst)
{
	vcvt_420i_bgr32(width, height, width, src, dst);
}


void ccvt_420i_420p(int width, int height, void *src, void *dsty, void *dstu, void *dstv)
{
	vcvt_420i_420p(width, height, width, src, dsty, dstu, dstv);
}

void ccvt_420i_yuyv(int width, int height, void *src, void *dst)
{
	vcvt_420i_yuyv(width, height, width, src, dst);
}
