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

/* $XFree86: xc/programs/Xserver/hw/xfree86/vga256/drivers/nv/riva_tbl.h,v 1.1.2.2 1998/12/22 16:33:20 hohndel Exp $ */
/*
 * RIVA Fixed Functionality Init Tables.
 */
static unsigned RivaTablePMC[][2] =
{
    {0x00000050, 0x00000000},
    {0x00000080, 0xFFFF00FF},
    {0x00000080, 0xFFFFFFFF}
};
static unsigned RivaTablePTIMER[][2] =
{
    {0x00000080, 0x00000008},
    {0x00000084, 0x00000003},
    {0x00000050, 0x00000000},
    {0x00000040, 0xFFFFFFFF}
};

#if 0
static unsigned RivaTableFIFO[][2] =
{
    {0x00000000, 0x80000000},
    {0x00000800, 0x80000001},
    {0x00001000, 0x80000002},
    {0x00001800, 0x80000010},
    {0x00002000, 0x80000011},
    {0x00002800, 0x80000012},
    {0x00003800, 0x80000013}
};
#endif

static unsigned nv3TablePFIFO[][2] =
{
    {0x00000140, 0x00000000},
    {0x00000480, 0x00000000},
    {0x00000490, 0x00000000},
    {0x00000494, 0x00000000},
    {0x00000481, 0x00000000},
    {0x00000084, 0x00000000},
    {0x00000086, 0x00002000},
    {0x00000085, 0x00002200},
    {0x00000484, 0x00000000},
    {0x0000049C, 0x00000000},
    {0x00000104, 0x00000000},
    {0x00000108, 0x00000000},
    {0x00000100, 0x00000000},
    {0x000004A0, 0x00000000},
    {0x000004A4, 0x00000000},
    {0x000004A8, 0x00000000},
    {0x000004AC, 0x00000000},
    {0x000004B0, 0x00000000},
    {0x000004B4, 0x00000000},
    {0x000004B8, 0x00000000},
    {0x000004BC, 0x00000000},
    {0x00000050, 0x00000000},
    {0x00000040, 0xFFFFFFFF},
    {0x00000480, 0x00000001},
    {0x00000490, 0x00000001},
    {0x00000140, 0x00000001}
};
static unsigned nv3TablePGRAPH[][2] =
{
    {0x00000020, 0x1230001F},
    {0x00000021, 0x10113000},
    {0x00000022, 0x1131F101},
    {0x00000023, 0x0100F531},
    {0x00000060, 0x00000000},
    {0x00000065, 0x00000000},
    {0x00000068, 0x00000000},
    {0x00000069, 0x00000000},
    {0x0000006A, 0x00000000},
    {0x0000006B, 0x00000000},
    {0x0000006C, 0x00000000},
    {0x0000006D, 0x00000000},
    {0x0000006E, 0x00000000},
    {0x0000006F, 0x00000000},
    {0x000001A8, 0x00000000},
    {0x00000440, 0xFFFFFFFF},
    {0x00000480, 0x00000001},
    {0x000001A0, 0x00000000},
    {0x000001A2, 0x00000000},
    {0x0000018A, 0xFFFFFFFF},
    {0x00000190, 0x00000000},
    {0x00000142, 0x00000000},
    {0x00000154, 0x00000000},
    {0x00000155, 0xFFFFFFFF},
    {0x00000156, 0x00000000},
    {0x00000157, 0xFFFFFFFF},
    {0x00000064, 0x10010002},
    {0x00000050, 0x00000000},
    {0x00000051, 0x00000000},
    {0x00000040, 0xFFFFFFFF},
    {0x00000041, 0xFFFFFFFF},
    {0x00000440, 0xFFFFFFFF},
    {0x000001A9, 0x00000001}
};
static unsigned nv3TablePGRAPH_8BPP[][2] =
{
    {0x000001AA, 0x00001111}
};
static unsigned nv3TablePGRAPH_15BPP[][2] =
{
    {0x000001AA, 0x00002222}
};
static unsigned nv3TablePGRAPH_32BPP[][2] =
{
    {0x000001AA, 0x00003333}
};
static unsigned nv3TablePRAMIN[][2] =
{
    {0x00000500, 0x00010000},
    {0x00000501, 0x007FFFFF},
    {0x00000200, 0x80000000},
    {0x00000201, 0x00C20341},
    {0x00000204, 0x80000001},
    {0x00000205, 0x00C50342},
    {0x00000208, 0x80000002},
    {0x00000209, 0x00C60343},
    {0x00000240, 0x80000010},
    {0x00000241, 0x00D10344},
    {0x00000244, 0x80000011},
    {0x00000245, 0x00D00345},
    {0x00000248, 0x80000012},
    {0x00000249, 0x00CC0346},
    {0x0000024C, 0x80000013},
    {0x0000024D, 0x00D70347},
    {0x00000D05, 0x00000000},
    {0x00000D06, 0x00000000},
    {0x00000D07, 0x00000000},
    {0x00000D09, 0x00000000},
    {0x00000D0A, 0x00000000},
    {0x00000D0B, 0x00000000},
    {0x00000D0D, 0x00000000},
    {0x00000D0E, 0x00000000},
    {0x00000D0F, 0x00000000},
    {0x00000D11, 0x00000000},
    {0x00000D12, 0x00000000},
    {0x00000D13, 0x00000000},
    {0x00000D15, 0x00000000},
    {0x00000D16, 0x00000000},
    {0x00000D17, 0x00000000},
    {0x00000D19, 0x00000000},
    {0x00000D1A, 0x00000000},
    {0x00000D1B, 0x00000000},
    {0x00000D1D, 0x00000140},
    {0x00000D1E, 0x00000000},
    {0x00000D1F, 0x00000000}
};
static unsigned nv3TablePRAMIN_8BPP[][2] =
{
    {0x00000D04, 0x10110203},
    {0x00000D08, 0x10110203},
    {0x00000D0C, 0x10110203},
    {0x00000D10, 0x10118203},
    {0x00000D14, 0x10110203},
    {0x00000D18, 0x10110203},
    {0x00000D1C, 0x10419208}
};
static unsigned nv3TablePRAMIN_15BPP[][2] =
{
    {0x00000D04, 0x10110200},
    {0x00000D08, 0x10110200},
    {0x00000D0C, 0x10110200},
    {0x00000D10, 0x10118200},
    {0x00000D14, 0x10110200},
    {0x00000D18, 0x10110200},
    {0x00000D1C, 0x10419208}
};
static unsigned nv3TablePRAMIN_32BPP[][2] =
{
    {0x00000D04, 0x10110201},
    {0x00000D08, 0x10110201},
    {0x00000D0C, 0x10110201},
    {0x00000D10, 0x10118201},
    {0x00000D14, 0x10110201},
    {0x00000D18, 0x10110201},
    {0x00000D1C, 0x10419208}
};
static unsigned nv4TablePFIFO[][2] =
{
    {0x00000140, 0x00000000},
    {0x00000480, 0x00000000},
    {0x00000494, 0x00000000},
    {0x00000400, 0x00000000},
    {0x00000414, 0x00000000},
    {0x00000084, 0x03000100},  
    {0x00000085, 0x00000110},
    {0x00000086, 0x00000112},  
    {0x00000143, 0x0000FFFF},
    {0x00000496, 0x0000FFFF},
    {0x00000050, 0x00000000},
    {0x00000040, 0xFFFFFFFF},
    {0x00000415, 0x00000001},
    {0x00000480, 0x00000001},
    {0x00000494, 0x00000001},
    {0x00000495, 0x00000001},
    {0x00000140, 0x00000001}
};
static unsigned nv4TablePGRAPH[][2] =
{
    {0x00000020, 0x1231C001},
    {0x00000021, 0x72111101},
    {0x00000022, 0x11D5F071},
    {0x00000023, 0x10D4FF31},
    {0x00000060, 0x00000000},
    {0x00000068, 0x00000000},
    {0x00000070, 0x00000000},
    {0x00000078, 0x00000000},
    {0x00000061, 0x00000000},
    {0x00000069, 0x00000000},
    {0x00000071, 0x00000000},
    {0x00000079, 0x00000000},
    {0x00000062, 0x00000000},
    {0x0000006A, 0x00000000},
    {0x00000072, 0x00000000},
    {0x0000007A, 0x00000000},
    {0x00000063, 0x00000000},
    {0x0000006B, 0x00000000},
    {0x00000073, 0x00000000},
    {0x0000007B, 0x00000000},
    {0x00000064, 0x00000000},
    {0x0000006C, 0x00000000},
    {0x00000074, 0x00000000},
    {0x0000007C, 0x00000000},
    {0x00000065, 0x00000000},
    {0x0000006D, 0x00000000},
    {0x00000075, 0x00000000},
    {0x0000007D, 0x00000000},
    {0x00000066, 0x00000000},
    {0x0000006E, 0x00000000},
    {0x00000076, 0x00000000},
    {0x0000007E, 0x00000000},
    {0x00000067, 0x00000000},
    {0x0000006F, 0x00000000},
    {0x00000077, 0x00000000},
    {0x0000007F, 0x00000000},
    {0x00000058, 0x00000000},
    {0x00000059, 0x00000000},
    {0x0000005A, 0x00000000},
    {0x0000005B, 0x00000000},
    {0x00000196, 0x00000000},
    {0x000001A1, 0x00FFFFFF},
    {0x00000197, 0x00000000},
    {0x000001A2, 0x00FFFFFF},
    {0x00000198, 0x00000000},
    {0x000001A3, 0x00FFFFFF},
    {0x00000199, 0x00000000},
    {0x000001A4, 0x00FFFFFF},
    {0x00000050, 0x00000000},
    {0x00000040, 0xFFFFFFFF},
    {0x0000005C, 0x10010100},
    {0x000001C8, 0x00000001}
};
static unsigned nv4TablePGRAPH_8BPP[][2] =
{
    {0x000001C4, 0xFFFFFFFF},
    {0x000001C9, 0x00111111},
    {0x00000186, 0x00001010},
    {0x0000020C, 0x01010101}
};
static unsigned nv4TablePGRAPH_15BPP[][2] =
{
    {0x000001C4, 0xFFFFFFFF},
    {0x000001C9, 0x00226222},
    {0x00000186, 0x00002071},
    {0x0000020C, 0x09090909}
};
static unsigned nv4TablePGRAPH_16BPP[][2] =
{
    {0x000001C4, 0xFFFFFFFF},
    {0x000001C9, 0x00556555},
    {0x00000186, 0x000050C2},
    {0x0000020C, 0x0C0C0C0C}
};
static unsigned nv4TablePGRAPH_32BPP[][2] =
{
    {0x000001C4, 0xFFFFFFFF},
    {0x000001C9, 0x0077D777},
    {0x00000186, 0x000070E5},
    {0x0000020C, 0x07070707}
};
static unsigned nv4TablePRAMIN[][2] =
{
    {0x00000000, 0x80000010},
    {0x00000001, 0x80011145},
    {0x00000002, 0x80000011},
    {0x00000003, 0x80011146},
    {0x00000004, 0x80000012},
    {0x00000005, 0x80011147},
    {0x00000006, 0x80000013},
    {0x00000007, 0x80011148},
    {0x00000020, 0x80000000},
    {0x00000021, 0x80011142},
    {0x00000022, 0x80000001},
    {0x00000023, 0x80011143},
    {0x00000024, 0x80000002},
    {0x00000025, 0x80011144}, 
    {0x00000500, 0x00003000},
    {0x00000501, 0x02FFFFFF},
    {0x00000502, 0x00000002},
    {0x00000503, 0x00000002},
    {0x00000508, 0x01008043},
    {0x0000050A, 0x00000000},
    {0x0000050B, 0x00000000},
    {0x0000050C, 0x01008019},
    {0x0000050E, 0x00000000},
    {0x0000050F, 0x00000000},
    {0x00000510, 0x01008018},
    {0x00000512, 0x00000000},
    {0x00000513, 0x00000000},
    {0x00000514, 0x0100A033},
    {0x00000516, 0x00000000},
    {0x00000517, 0x00000000},
    {0x00000518, 0x0100805F},
    {0x0000051A, 0x00000000},
    {0x0000051B, 0x00000000},
    {0x0000051C, 0x0100804B},
    {0x0000051E, 0x00000000},
    {0x0000051F, 0x00000000},
    {0x00000520, 0x0100A048},
    {0x00000521, 0x00000D01},
    {0x00000522, 0x11401140},
    {0x00000523, 0x00000000}
};
static unsigned nv4TablePRAMIN_8BPP[][2] =
{
    {0x00000509, 0x00000301},
    {0x0000050D, 0x00000301},
    {0x00000511, 0x00000301},
    {0x00000515, 0x00000301},
    {0x00000519, 0x00000301},
    {0x0000051D, 0x00000301}
};
static unsigned nv4TablePRAMIN_15BPP[][2] =
{
    {0x00000509, 0x00000901},
    {0x0000050D, 0x00000901},
    {0x00000511, 0x00000901},
    {0x00000515, 0x00000901},
    {0x00000519, 0x00000901},
    {0x0000051D, 0x00000901}
};
static unsigned nv4TablePRAMIN_16BPP[][2] =
{
    {0x00000509, 0x00000C01},
    {0x0000050D, 0x00000C01},
    {0x00000511, 0x00000C01},
    {0x00000515, 0x00000C01},
    {0x00000519, 0x00000C01},
    {0x0000051D, 0x00000C01}
};
static unsigned nv4TablePRAMIN_32BPP[][2] =
{
    {0x00000509, 0x00000E01},
    {0x0000050D, 0x00000E01},
    {0x00000511, 0x00000E01},
    {0x00000515, 0x00000E01},
    {0x00000519, 0x00000E01},
    {0x0000051D, 0x00000E01}
};

