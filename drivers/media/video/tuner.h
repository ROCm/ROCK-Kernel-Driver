/* 
    tuner.h - definition for different tuners

    Copyright (C) 1997 Markus Schroeder (schroedm@uni-duesseldorf.de)
    minor modifications by Ralph Metzler (rjkm@thp.uni-koeln.de)

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

#ifndef _TUNER_H
#define _TUNER_H

#define TUNER_TEMIC_PAL     0  /*  Miro Gpio Coding -1 */
#define TUNER_PHILIPS_PAL_I 1
#define TUNER_PHILIPS_NTSC  2
#define TUNER_PHILIPS_SECAM 3
#define TUNER_ABSENT        4
#define TUNER_PHILIPS_PAL   5
#define TUNER_TEMIC_NTSC    6
#define TUNER_TEMIC_PAL_I   7
#define TUNER_TEMIC_4036FY5_NTSC	8
#define TUNER_ALPS_TSBH1_NTSC 	9
#define TUNER_ALPS_TSBE1_PAL 	10
#define TUNER_ALPS_TSBB5_PAL_I 	11
#define TUNER_ALPS_TSBE5_PAL 	12
#define TUNER_ALPS_TSBC5_PAL 	13
#define TUNER_TEMIC_4006FH5_PAL	14
#define TUNER_ALPS_TSHC6_NTSC 	15

#define NOTUNER 0
#define PAL     1
#define PAL_I   2
#define NTSC    3
#define SECAM   4

#define NoTuner 0
#define Philips 1
#define TEMIC   2
#define Sony    3
#define Alps    4

#define TUNER_SET_TYPE               _IOW('t',1,int)    /* set tuner type */
#define TUNER_SET_TVFREQ             _IOW('t',2,int)    /* set tv freq */
#define TUNER_SET_RADIOFREQ          _IOW('t',3,int)    /* set radio freq */
#define TUNER_SET_MODE               _IOW('t',4,int)    /* set tuner mode */

#endif
