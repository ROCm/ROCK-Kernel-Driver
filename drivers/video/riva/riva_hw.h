/***************************************************************************\
|*                                                                           *|
|*       Copyright 1993-1998 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NOTICE TO USER:   The source code  is copyrighted under  U.S. and     *|
|*     international laws.  Users and possessors of this source code are     *|
|*     hereby granted a nonexclusive,  royalty-free copyright license to     *|
|*     use this code in individual and commercial software.                  *|
|*                                                                           *|
|*     Any use of this source code must include,  in the user documenta-     *|
|*     tion and  internal comments to the code,  notices to the end user     *|
|*     as follows:                                                           *|
|*                                                                           *|
|*       Copyright 1993-1998 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NVIDIA, CORPORATION MAKES NO REPRESENTATION ABOUT THE SUITABILITY     *|
|*     OF  THIS SOURCE  CODE  FOR ANY PURPOSE.  IT IS  PROVIDED  "AS IS"     *|
|*     WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.  NVIDIA, CORPOR-     *|
|*     ATION DISCLAIMS ALL WARRANTIES  WITH REGARD  TO THIS SOURCE CODE,     *|
|*     INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGE-     *|
|*     MENT,  AND FITNESS  FOR A PARTICULAR PURPOSE.   IN NO EVENT SHALL     *|
|*     NVIDIA, CORPORATION  BE LIABLE FOR ANY SPECIAL,  INDIRECT,  INCI-     *|
|*     DENTAL, OR CONSEQUENTIAL DAMAGES,  OR ANY DAMAGES  WHATSOEVER RE-     *|
|*     SULTING FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN ACTION     *|
|*     OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF     *|
|*     OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.     *|
|*                                                                           *|
|*     U.S. Government  End  Users.   This source code  is a "commercial     *|
|*     item,"  as that  term is  defined at  48 C.F.R. 2.101 (OCT 1995),     *|
|*     consisting  of "commercial  computer  software"  and  "commercial     *|
|*     computer  software  documentation,"  as such  terms  are  used in     *|
|*     48 C.F.R. 12.212 (SEPT 1995)  and is provided to the U.S. Govern-     *|
|*     ment only as  a commercial end item.   Consistent with  48 C.F.R.     *|
|*     12.212 and  48 C.F.R. 227.7202-1 through  227.7202-4 (JUNE 1995),     *|
|*     all U.S. Government End Users  acquire the source code  with only     *|
|*     those rights set forth herein.                                        *|
|*                                                                           *|
\***************************************************************************/
/* 
 * GPL licensing note -- nVidia is allowing a liberal interpretation of 
 * the documentation restriction above, to merely say that this nVidia's
 * copyright and disclaimer should be included with all code derived 
 * from this source.  -- Jeff Garzik <jgarzik@mandrakesoft.com>, 01/Nov/99
 */

/* $XFree86: xc/programs/Xserver/hw/xfree86/vga256/drivers/nv/riva_hw.h,v 1.1.2.2 1998/12/22 16:33:19 hohndel Exp $ */
#ifndef __RIVA_HW_H__
#define __RIVA_HW_H__
#define RIVA_SW_VERSION 0x00010000

/***************************************************************************\
*                                                                           *
*                             FIFO registers.                               *
*                                                                           *
\***************************************************************************/

/*
 * Raster OPeration. Windows style ROP3.
 */
typedef volatile struct
{
    unsigned reserved00[4];
    unsigned short FifoFree;
    unsigned short Nop;
    unsigned reserved01[0x0BB];
    unsigned Rop3;
} RivaRop;
/*
 * 8X8 Monochrome pattern.
 */
typedef volatile struct
{
    unsigned reserved00[4];
    unsigned short FifoFree;
    unsigned short Nop;
    unsigned reserved01[0x0BD];
    unsigned Shape;
    unsigned reserved03[0x001];
    unsigned Color0;
    unsigned Color1;
    unsigned Monochrome[2];
} RivaPattern;
/*
 * Scissor clip rectangle.
 */
typedef volatile struct
{
    unsigned reserved00[4];
    unsigned short FifoFree;
    unsigned short Nop;
    unsigned reserved01[0x0BB];
    unsigned TopLeft;
    unsigned WidthHeight;
} RivaClip;
/*
 * 2D filled rectangle.
 */
typedef volatile struct
{
    unsigned reserved00[4];
    unsigned short FifoFree;
    unsigned short Nop[1];
    unsigned reserved01[0x0BC];
    unsigned Color;
    unsigned reserved03[0x03E];
    unsigned TopLeft;
    unsigned WidthHeight;
} RivaRectangle;
/*
 * 2D screen-screen BLT.
 */
typedef volatile struct
{
    unsigned reserved00[4];
    unsigned short FifoFree;
    unsigned short Nop;
    unsigned reserved01[0x0BB];
    unsigned TopLeftSrc;
    unsigned TopLeftDst;
    unsigned WidthHeight;
} RivaScreenBlt;
/*
 * 2D pixel BLT.
 */
typedef volatile struct
{
    unsigned reserved00[4];
    unsigned short FifoFree;
    unsigned short Nop[1];
    unsigned reserved01[0x0BC];
    unsigned TopLeft;
    unsigned WidthHeight;
    unsigned WidthHeightIn;
    unsigned reserved02[0x03C];
    unsigned Pixels;
} RivaPixmap;
/*
 * Filled rectangle combined with monochrome expand.  Useful for glyphs.
 */
typedef volatile struct
{
    unsigned reserved00[4];
    unsigned short FifoFree;
    unsigned short Nop;
    unsigned reserved01[0x0BB];
    unsigned reserved03[(0x040)-1];
    unsigned Color1A;
    struct
    {
        unsigned TopLeft;
        unsigned WidthHeight;
    } UnclippedRectangle[64];
    unsigned reserved04[(0x080)-3];
    struct
    {
        unsigned TopLeft;
        unsigned BottomRight;
    } ClipB;
    unsigned Color1B;
    struct
    {
        unsigned TopLeft;
        unsigned BottomRight;
    } ClippedRectangle[64];
    unsigned reserved05[(0x080)-5];
    struct
    {
        unsigned TopLeft;
        unsigned BottomRight;
    } ClipC;
    unsigned Color1C;
    unsigned WidthHeightC;
    unsigned PointC;
    unsigned MonochromeData1C;
    unsigned reserved06[(0x080)+121];
    struct
    {
        unsigned TopLeft;
        unsigned BottomRight;
    } ClipD;
    unsigned Color1D;
    unsigned WidthHeightInD;
    unsigned WidthHeightOutD;
    unsigned PointD;
    unsigned MonochromeData1D;
    unsigned reserved07[(0x080)+120];
    struct
    {
        unsigned TopLeft;
        unsigned BottomRight;
    } ClipE;
    unsigned Color0E;
    unsigned Color1E;
    unsigned WidthHeightInE;
    unsigned WidthHeightOutE;
    unsigned PointE;
    unsigned MonochromeData01E;
} RivaBitmap;
/*
 * 3D textured, Z buffered triangle.
 */
typedef volatile struct
{
    unsigned reserved00[4];
    unsigned short FifoFree;
    unsigned short Nop;
    unsigned reserved01[0x0BC];
    unsigned TextureOffset;
    unsigned TextureFormat;
    unsigned TextureFilter;
    unsigned FogColor;
    unsigned Control;
    unsigned AlphaTest;
    unsigned reserved02[0x339];
    unsigned FogAndIndex;
    unsigned Color;
    float ScreenX;
    float ScreenY;
    float ScreenZ;
    float EyeM;
    float TextureS;
    float TextureT;
} RivaTexturedTriangle03;

/***************************************************************************\
*                                                                           *
*                        Virtualized RIVA H/W interface.                    *
*                                                                           *
\***************************************************************************/

struct _riva_hw_inst;
struct _riva_hw_state;
/*
 * Virtialized chip interface. Makes RIVA 128 and TNT look alike.
 */
typedef struct _riva_hw_inst
{
    /*
     * Chip specific settings.
     */
    unsigned Architecture;
    unsigned Version;
    unsigned CrystalFreqKHz;
    unsigned RamAmountKBytes;
    unsigned MaxVClockFreqKHz;
    unsigned RamBandwidthKBytesPerSec;
    unsigned EnableIRQ;
    unsigned IO;
    unsigned LockUnlockIO;
    unsigned LockUnlockIndex;
    unsigned VBlankBit;
    unsigned FifoFreeCount;
    /*
     * Non-FIFO registers.
     */
    volatile unsigned *PCRTC;
    volatile unsigned *PRAMDAC;
    volatile unsigned *PFB;
    volatile unsigned *PFIFO;
    volatile unsigned *PGRAPH;
    volatile unsigned *PEXTDEV;
    volatile unsigned *PTIMER;
    volatile unsigned *PMC;
    volatile unsigned *PRAMIN;
    volatile unsigned *FIFO;
    volatile unsigned *CURSOR;
    volatile unsigned *CURSORPOS;
    volatile unsigned *VBLANKENABLE;
    volatile unsigned *VBLANK;
    /*
     * Common chip functions.
     */
    int  (*Busy)(struct _riva_hw_inst *);
    void (*CalcStateExt)(struct _riva_hw_inst *,struct _riva_hw_state *,int,int,int,int,int,int,int,int,int,int,int,int,int);
    void (*LoadStateExt)(struct _riva_hw_inst *,struct _riva_hw_state *);
    void (*UnloadStateExt)(struct _riva_hw_inst *,struct _riva_hw_state *);
    void (*SetStartAddress)(struct _riva_hw_inst *,unsigned);
    void (*SetSurfaces2D)(struct _riva_hw_inst *,unsigned,unsigned);
    void (*SetSurfaces3D)(struct _riva_hw_inst *,unsigned,unsigned);
    int  (*ShowHideCursor)(struct _riva_hw_inst *,int);
    /*
     * Current extended mode settings.
     */
    struct _riva_hw_state *CurrentState;
    /*
     * FIFO registers.
     */
    RivaRop                 *Rop;
    RivaPattern             *Patt;
    RivaClip                *Clip;
    RivaPixmap              *Pixmap;
    RivaScreenBlt           *Blt;
    RivaBitmap              *Bitmap;
    RivaTexturedTriangle03  *Tri03;
} RIVA_HW_INST;
/*
 * Extended mode state information.
 */
typedef struct _riva_hw_state
{
    unsigned bpp;
    unsigned width;
    unsigned height;
    unsigned repaint0;
    unsigned repaint1;
    unsigned screen;
    unsigned pixel;
    unsigned horiz;
    unsigned arbitration0;
    unsigned arbitration1;
    unsigned vpll;
    unsigned pllsel;
    unsigned general;
    unsigned config;
    unsigned cursor0;
    unsigned cursor1;
    unsigned cursor2;
    unsigned offset0;
    unsigned offset1;
    unsigned offset2;
    unsigned offset3;
    unsigned pitch0;
    unsigned pitch1;
    unsigned pitch2;
    unsigned pitch3;
} RIVA_HW_STATE;
/*
 * External routines.
 */
int RivaGetConfig(RIVA_HW_INST *);
/*
 * FIFO Free Count. Should attempt to yield processor if RIVA is busy.
 */
#define RIVA_FIFO_FREE(hwinst,hwptr,cnt)                                    \
{                                                                           \
while ((hwinst).FifoFreeCount < (cnt))                                      \
{                                                                           \
    (hwinst).FifoFreeCount = (hwinst).hwptr->FifoFree >> 2;                 \
}                                                                           \
(hwinst).FifoFreeCount -= (cnt);                                            \
}
#endif /* __RIVA_HW_H__ */

