/*******************************************************************************

This software program is available to you under a choice of one of two 
licenses. You may choose to be licensed under either the GNU General Public 
License 2.0, June 1991, available at http://www.fsf.org/copyleft/gpl.html, 
or the Intel BSD + Patent License, the text of which follows:

Recipient has requested a license and Intel Corporation ("Intel") is willing
to grant a license for the software entitled Linux Base Driver for the 
Intel(R) PRO/100 Family of Adapters (e100) (the "Software") being provided 
by Intel Corporation. The following definitions apply to this license:

"Licensed Patents" means patent claims licensable by Intel Corporation which 
are necessarily infringed by the use of sale of the Software alone or when 
combined with the operating system referred to below.

"Recipient" means the party to whom Intel delivers this Software.

"Licensee" means Recipient and those third parties that receive a license to 
any operating system available under the GNU General Public License 2.0 or 
later.

Copyright (c) 1999 - 2002 Intel Corporation.
All rights reserved.

The license is provided to Recipient and Recipient's Licensees under the 
following terms.

Redistribution and use in source and binary forms of the Software, with or 
without modification, are permitted provided that the following conditions 
are met:

Redistributions of source code of the Software may retain the above 
copyright notice, this list of conditions and the following disclaimer.

Redistributions in binary form of the Software may reproduce the above 
copyright notice, this list of conditions and the following disclaimer in 
the documentation and/or materials provided with the distribution.

Neither the name of Intel Corporation nor the names of its contributors 
shall be used to endorse or promote products derived from this Software 
without specific prior written permission.

Intel hereby grants Recipient and Licensees a non-exclusive, worldwide, 
royalty-free patent license under Licensed Patents to make, use, sell, offer 
to sell, import and otherwise transfer the Software, if any, in source code 
and object code form. This license shall include changes to the Software 
that are error corrections or other minor changes to the Software that do 
not add functionality or features when the Software is incorporated in any 
version of an operating system that has been distributed under the GNU 
General Public License 2.0 or later. This patent license shall apply to the 
combination of the Software and any operating system licensed under the GNU 
General Public License 2.0 or later if, at the time Intel provides the 
Software to Recipient, such addition of the Software to the then publicly 
available versions of such operating systems available under the GNU General 
Public License 2.0 or later (whether in gold, beta or alpha form) causes 
such combination to be covered by the Licensed Patents. The patent license 
shall not apply to any other combinations which include the Software. NO 
hardware per se is licensed hereunder.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
IMPLIED WARRANTIES OF MECHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR IT CONTRIBUTORS BE LIABLE FOR ANY 
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
ANY LOSS OF USE; DATA, OR PROFITS; OR BUSINESS INTERUPTION) HOWEVER CAUSED 
AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR 
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#ifndef _E100_UCODE_H_
#define _E100_UCODE_H_

/*
e100_ucode.h

This file contains the loadable micro code arrays to implement receive 
bundling on the D101 A-step, D101 B-step, D101M (B-step only), D101S, 
D102 B-step, D102 B-step with TCO work around and D102 C-step.

Each controller has its own specific micro code array.  The array for one 
controller is totally incompatible with any other controller, and if used 
will most likely cause the controller to lock up and stop responding to 
the driver.  Each micro code array has its own parameter offsets (described 
below), and they each have their own version number.
*/

/*************************************************************************
*  CPUSaver parameters
*
*  All CPUSaver parameters are 16-bit literals that are part of a
*  "move immediate value" instruction.  By changing the value of
*  the literal in the instruction before the code is loaded, the
*  driver can change algorithm.
*
*  CPUSAVER_DWORD - This is the location of the instruction that loads
*    the dead-man timer with its inital value.  By writing a 16-bit
*    value to the low word of this instruction, the driver can change
*    the timer value.  The current default is either x600 or x800;
*    experiments show that the value probably should stay within the
*    range of x200 - x1000.
*
*  CPUSAVER_BUNDLE_MAX_DWORD - This is the location of the instruction
*    that sets the maximum number of frames that will be bundled.  In
*    some situations, such as the TCP windowing algorithm, it may be
*    better to limit the growth of the bundle size than let it go as
*    high as it can, because that could cause too much added latency.
*    The default is six, because this is the number of packets in the
*    default TCP window size.  A value of 1 would make CPUSaver indicate
*    an interrupt for every frame received.  If you do not want to put
*    a limit on the bundle size, set this value to xFFFF.
*
*  CPUSAVER_MIN_SIZE_DWORD - This is the location of the instruction
*    that contains a bit-mask describing the minimum size frame that
*    will be bundled.  The default masks the lower 7 bits, which means
*    that any frame less than 128 bytes in length will not be bundled,
*    but will instead immediately generate an interrupt.  This does
*    not affect the current bundle in any way.  Any frame that is 128
*    bytes or large will be bundled normally.  This feature is meant
*    to provide immediate indication of ACK frames in a TCP environment.
*    Customers were seeing poor performance when a machine with CPUSaver
*    enabled was sending but not receiving.  The delay introduced when
*    the ACKs were received was enough to reduce total throughput, because
*    the sender would sit idle until the ACK was finally seen.
*
*    The current default is 0xFF80, which masks out the lower 7 bits.
*    This means that any frame which is x7F (127) bytes or smaller
*    will cause an immediate interrupt.  Because this value must be a 
*    bit mask, there are only a few valid values that can be used.  To
*    turn this feature off, the driver can write the value xFFFF to the
*    lower word of this instruction (in the same way that the other
*    parameters are used).  Likewise, a value of 0xF800 (2047) would
*    cause an interrupt to be generated for every frame, because all
*    standard Ethernet frames are <= 2047 bytes in length.
*************************************************************************/

#ifndef UCODE_MAX_DWORDS
#define UCODE_MAX_DWORDS	134
#endif

/********************************************************/
/*  CPUSaver micro code for the D101A                   */
/********************************************************/

/*  Version 2.0  */

/*  This value is the same for both A and B step of 558.  */

#define D101_CPUSAVER_TIMER_DWORD		72
#define D101_CPUSAVER_BUNDLE_DWORD		UCODE_MAX_DWORDS
#define D101_CPUSAVER_MIN_SIZE_DWORD		UCODE_MAX_DWORDS

#define     D101_A_RCVBUNDLE_UCODE \
{\
0x03B301BB, 0x0046FFFF, 0xFFFFFFFF, 0x051DFFFF, 0xFFFFFFFF, 0xFFFFFFFF, \
0x000C0001, 0x00101212, 0x000C0008, 0x003801BC, \
0x00000000, 0x00124818, 0x000C1000, 0x00220809, \
0x00010200, 0x00124818, 0x000CFFFC, 0x003803B5, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x0010009C, 0x0024B81D, 0x00130836, 0x000C0001, \
0x0026081C, 0x0020C81B, 0x00130824, 0x00222819, \
0x00101213, 0x00041000, 0x003A03B3, 0x00010200, \
0x00101B13, 0x00238081, 0x00213049, 0x0038003B, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x0010009C, 0x0024B83E, 0x00130826, 0x000C0001, \
0x0026083B, 0x00010200, 0x00134824, 0x000C0001, \
0x00101213, 0x00041000, 0x0038051E, 0x00101313, \
0x00010400, 0x00380521, 0x00050600, 0x00100824, \
0x00101310, 0x00041000, 0x00080600, 0x00101B10, \
0x0038051E, 0x00000000, 0x00000000, 0x00000000  \
}

/********************************************************/
/*  CPUSaver micro code for the D101B                   */
/********************************************************/

/*  Version 2.0  */

#define     D101_B0_RCVBUNDLE_UCODE \
{\
0x03B401BC, 0x0047FFFF, 0xFFFFFFFF, 0x051EFFFF, 0xFFFFFFFF, 0xFFFFFFFF, \
0x000C0001, 0x00101B92, 0x000C0008, 0x003801BD, \
0x00000000, 0x00124818, 0x000C1000, 0x00220809, \
0x00010200, 0x00124818, 0x000CFFFC, 0x003803B6, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x0010009C, 0x0024B81D, 0x0013082F, 0x000C0001, \
0x0026081C, 0x0020C81B, 0x00130837, 0x00222819, \
0x00101B93, 0x00041000, 0x003A03B4, 0x00010200, \
0x00101793, 0x00238082, 0x0021304A, 0x0038003C, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x0010009C, 0x0024B83E, 0x00130826, 0x000C0001, \
0x0026083B, 0x00010200, 0x00134837, 0x000C0001, \
0x00101B93, 0x00041000, 0x0038051F, 0x00101313, \
0x00010400, 0x00380522, 0x00050600, 0x00100837, \
0x00101310, 0x00041000, 0x00080600, 0x00101790, \
0x0038051F, 0x00000000, 0x00000000, 0x00000000  \
}

/********************************************************/
/*  CPUSaver micro code for the D101M (B-step only)     */
/********************************************************/

/*  Version 2.10.1  */

/*  Parameter values for the D101M B-step  */
#define D101M_CPUSAVER_TIMER_DWORD		78
#define D101M_CPUSAVER_BUNDLE_DWORD		65
#define D101M_CPUSAVER_MIN_SIZE_DWORD		126

#define D101M_B_RCVBUNDLE_UCODE \
{\
0x00550215, 0xFFFF0437, 0xFFFFFFFF, 0x06A70789, 0xFFFFFFFF, 0x0558FFFF, \
0x000C0001, 0x00101312, 0x000C0008, 0x00380216, \
0x0010009C, 0x00204056, 0x002380CC, 0x00380056, \
0x0010009C, 0x00244C0B, 0x00000800, 0x00124818, \
0x00380438, 0x00000000, 0x00140000, 0x00380555, \
0x00308000, 0x00100662, 0x00100561, 0x000E0408, \
0x00134861, 0x000C0002, 0x00103093, 0x00308000, \
0x00100624, 0x00100561, 0x000E0408, 0x00100861, \
0x000C007E, 0x00222C21, 0x000C0002, 0x00103093, \
0x00380C7A, 0x00080000, 0x00103090, 0x00380C7A, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x0010009C, 0x00244C2D, 0x00010004, 0x00041000, \
0x003A0437, 0x00044010, 0x0038078A, 0x00000000, \
0x00100099, 0x00206C7A, 0x0010009C, 0x00244C48, \
0x00130824, 0x000C0001, 0x00101213, 0x00260C75, \
0x00041000, 0x00010004, 0x00130826, 0x000C0006, \
0x002206A8, 0x0013C926, 0x00101313, 0x003806A8, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00080600, 0x00101B10, 0x00050004, 0x00100826, \
0x00101210, 0x00380C34, 0x00000000, 0x00000000, \
0x0021155B, 0x00100099, 0x00206559, 0x0010009C, \
0x00244559, 0x00130836, 0x000C0000, 0x00220C62, \
0x000C0001, 0x00101B13, 0x00229C0E, 0x00210C0E, \
0x00226C0E, 0x00216C0E, 0x0022FC0E, 0x00215C0E, \
0x00214C0E, 0x00380555, 0x00010004, 0x00041000, \
0x00278C67, 0x00040800, 0x00018100, 0x003A0437, \
0x00130826, 0x000C0001, 0x00220559, 0x00101313, \
0x00380559, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00130831, 0x0010090B, 0x00124813, \
0x000CFF80, 0x002606AB, 0x00041000, 0x00010004, \
0x003806A8, 0x00000000, 0x00000000, 0x00000000, \
}

/********************************************************/
/*  CPUSaver micro code for the D101S                   */
/********************************************************/

/*  Version 1.20.1  */

/*  Parameter values for the D101S  */
#define D101S_CPUSAVER_TIMER_DWORD		78
#define D101S_CPUSAVER_BUNDLE_DWORD		67
#define D101S_CPUSAVER_MIN_SIZE_DWORD		128

#define D101S_RCVBUNDLE_UCODE \
{\
0x00550242, 0xFFFF047E, 0xFFFFFFFF, 0x06FF0818, 0xFFFFFFFF, 0x05A6FFFF, \
0x000C0001, 0x00101312, 0x000C0008, 0x00380243, \
0x0010009C, 0x00204056, 0x002380D0, 0x00380056, \
0x0010009C, 0x00244F8B, 0x00000800, 0x00124818, \
0x0038047F, 0x00000000, 0x00140000, 0x003805A3, \
0x00308000, 0x00100610, 0x00100561, 0x000E0408, \
0x00134861, 0x000C0002, 0x00103093, 0x00308000, \
0x00100624, 0x00100561, 0x000E0408, 0x00100861, \
0x000C007E, 0x00222FA1, 0x000C0002, 0x00103093, \
0x00380F90, 0x00080000, 0x00103090, 0x00380F90, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x0010009C, 0x00244FAD, 0x00010004, 0x00041000, \
0x003A047E, 0x00044010, 0x00380819, 0x00000000, \
0x00100099, 0x00206FFD, 0x0010009A, 0x0020AFFD, \
0x0010009C, 0x00244FC8, 0x00130824, 0x000C0001, \
0x00101213, 0x00260FF7, 0x00041000, 0x00010004, \
0x00130826, 0x000C0006, 0x00220700, 0x0013C926, \
0x00101313, 0x00380700, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00080600, 0x00101B10, 0x00050004, 0x00100826, \
0x00101210, 0x00380FB6, 0x00000000, 0x00000000, \
0x002115A9, 0x00100099, 0x002065A7, 0x0010009A, \
0x0020A5A7, 0x0010009C, 0x002445A7, 0x00130836, \
0x000C0000, 0x00220FE4, 0x000C0001, 0x00101B13, \
0x00229F8E, 0x00210F8E, 0x00226F8E, 0x00216F8E, \
0x0022FF8E, 0x00215F8E, 0x00214F8E, 0x003805A3, \
0x00010004, 0x00041000, 0x00278FE9, 0x00040800, \
0x00018100, 0x003A047E, 0x00130826, 0x000C0001, \
0x002205A7, 0x00101313, 0x003805A7, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00130831, \
0x0010090B, 0x00124813, 0x000CFF80, 0x00260703, \
0x00041000, 0x00010004, 0x00380700  \
}

/********************************************************/
/*  CPUSaver micro code for the D102 B-step             */
/********************************************************/

/*  Version 2.0  */
/*  Parameter values for the D102 B-step  */
#define D102_B_CPUSAVER_TIMER_DWORD		82
#define D102_B_CPUSAVER_BUNDLE_DWORD		106
#define D102_B_CPUSAVER_MIN_SIZE_DWORD		70

#define     D102_B_RCVBUNDLE_UCODE \
{\
0x006F0276, 0x0EF71FFF, 0x0ED30F86, 0x0D250ED9, 0x1FFF1FFF, 0x1FFF04D2, \
0x00300001, 0x0140D871, 0x00300008, 0x00E00277, \
0x01406C57, 0x00816073, 0x008700FA, 0x00E00070, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x01406CBA, 0x00807F9A, 0x00901F9A, 0x0024FFFF, \
0x014B6F6F, 0x0030FFFE, 0x01407172, 0x01496FBA, \
0x014B6F72, 0x00308000, 0x01406C52, 0x00912EFC, \
0x00E00EF8, 0x00000000, 0x00000000, 0x00000000, \
0x00906F8C, 0x00900F8C, 0x00E00F87, 0x00000000, \
0x00906ED8, 0x01406C55, 0x00E00ED4, 0x00000000, \
0x01406C51, 0x0080DFC2, 0x01406C52, 0x00815FC2, \
0x01406C57, 0x00917FCC, 0x00E01FDD, 0x00000000, \
0x00822D30, 0x01406C51, 0x0080CD26, 0x01406C52, \
0x00814D26, 0x01406C57, 0x00916D26, 0x014C6FD7, \
0x00300000, 0x00841FD2, 0x00300001, 0x0140D772, \
0x00E012B3, 0x014C6F91, 0x0150710B, 0x01496F72, \
0x0030FF80, 0x00940EDD, 0x00102000, 0x00038400, \
0x00E00EDA, 0x00000000, 0x00000000, 0x00000000, \
0x01406C57, 0x00917FE9, 0x00001000, 0x00E01FE9, \
0x00200600, 0x0140D76F, 0x00138400, 0x01406FD8, \
0x0140D96F, 0x00E01FDD, 0x00038400, 0x00102000, \
0x00971FD7, 0x00101000, 0x00050200, 0x00E804D2, \
0x014C6FD8, 0x00300001, 0x00840D26, 0x0140D872, \
0x00E00D26, 0x014C6FD9, 0x00300001, 0x0140D972, \
0x00941FBD, 0x00102000, 0x00038400, 0x014C6FD8, \
0x00300006, 0x00840EDA, 0x014F71D8, 0x0140D872, \
0x00E00EDA, 0x01496F50, 0x00E004D3, 0x00000000, \
}

/********************************************************/
/*  Micro code for the D102 C-step                      */
/********************************************************/

/*  Parameter values for the D102 C-step  */
#define D102_C_CPUSAVER_TIMER_DWORD		46
#define D102_C_CPUSAVER_BUNDLE_DWORD		74
#define D102_C_CPUSAVER_MIN_SIZE_DWORD		54

#define     D102_C_RCVBUNDLE_UCODE \
{ \
0x00700279, 0x0E6604E2, 0x02BF0CAE, 0x1508150C, 0x15190E5B, 0x0E840F13, \
0x00E014D8, 0x00000000, 0x00000000, 0x00000000, \
0x00E014DC, 0x00000000, 0x00000000, 0x00000000, \
0x00E014F4, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00E014E0, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00E014E7, 0x00000000, 0x00000000, 0x00000000, \
0x00141000, 0x015D6F0D, 0x00E002C0, 0x00000000, \
0x00200600, 0x00E0150D, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x0030FF80, 0x00940E6A, 0x00038200, 0x00102000, \
0x00E00E67, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00906E65, 0x00800E60, 0x00E00E5D, 0x00000000, \
0x00300006, 0x00E0151A, 0x00000000, 0x00000000, \
0x00906F19, 0x00900F19, 0x00E00F14, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x01406CBA, 0x00807FDA, 0x00901FDA, 0x0024FFFF, \
0x014B6F6F, 0x0030FFFE, 0x01407172, 0x01496FBA, \
0x014B6F72, 0x00308000, 0x01406C52, 0x00912E89, \
0x00E00E85, 0x00000000, 0x00000000, 0x00000000  \
}

/********************************************************/
/*  Micro code for the D102 E-step                      */
/********************************************************/

/*  Parameter values for the D102 E-step  */
#define D102_E_CPUSAVER_TIMER_DWORD		42
#define D102_E_CPUSAVER_BUNDLE_DWORD		54
#define D102_E_CPUSAVER_MIN_SIZE_DWORD		46

#define     D102_E_RCVBUNDLE_UCODE \
{\
0x007D028F, 0x0E4204F9, 0x14ED0C85, 0x14FA14E9, 0x1FFF1FFF, 0x1FFF1FFF, \
0x00E014B9, 0x00000000, 0x00000000, 0x00000000, \
0x00E014BD, 0x00000000, 0x00000000, 0x00000000, \
0x00E014D5, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00E014C1, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00000000, 0x00000000, 0x00000000, 0x00000000, \
0x00E014C8, 0x00000000, 0x00000000, 0x00000000, \
0x00200600, 0x00E014EE, 0x00000000, 0x00000000, \
0x0030FF80, 0x00940E46, 0x00038200, 0x00102000, \
0x00E00E43, 0x00000000, 0x00000000, 0x00000000, \
0x00300006, 0x00E014FB, 0x00000000, 0x00000000  \
}

#endif /* _E100_UCODE_H_ */
