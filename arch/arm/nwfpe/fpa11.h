/*
    NetWinder Floating Point Emulator
    (c) Rebel.com, 1998-1999
    
    Direct questions, comments to Scott Bambrough <scottb@netwinder.org>

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __FPA11_H__
#define __FPA11_H__

/* includes */
#include "fpsr.h"		/* FP control and status register definitions */
#include "softfloat.h"

#define		typeNone		0x00
#define		typeSingle		0x01
#define		typeDouble		0x02
#define		typeExtended		0x03

typedef union tagFPREG {
   float32  fSingle;
   float64  fDouble;
   floatx80 fExtended;
} FPREG;

/* FPA11 device model */
typedef struct tagFPA11 {
  FPREG fpreg[8];		/* 8 floating point registers */
  FPSR fpsr;			/* floating point status register */
  FPCR fpcr;			/* floating point control register */
  unsigned char fType[8];	/* type of floating point value held in
				   floating point registers.  One of none
				   single, double or extended. */
  int initflag;			/* this is special.  The kernel guarantees
				   to set it to 0 when a thread is launched,
				   so we can use it to detect whether this
				   instance of the emulator needs to be
				   initialised. */
} FPA11;

extern void resetFPA11(void);
extern void SetRoundingMode(const unsigned int);
extern void SetRoundingPrecision(const unsigned int);

extern FPA11 *fpa11;

#endif
