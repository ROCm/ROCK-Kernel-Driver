/*
 * FILE NAME
 *	arch/mips/vr41xx/common/cmu.c
 *
 * BRIEF MODULE DESCRIPTION
 *	Clock Mask Unit routines for the NEC VR4100 series.
 *
 * Author: Yoichi Yuasa
 *         yyuasa@mvista.com or source@mvista.com
 *
 * Copyright 2001,2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
/*
 * Changes:
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - Added support for NEC VR4111 and VR4121.
 *
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - New creation, NEC VR4122 and VR4131 are supported.
 */
#include <linux/init.h>
#include <linux/types.h>

#include <asm/cpu.h>
#include <asm/io.h>

#define VR4111_CMUCLKMSK	KSEG1ADDR(0x0b000060)
#define VR4122_CMUCLKMSK	KSEG1ADDR(0x0f000060)

static u32 vr41xx_cmu_base = 0;
static u16 cmuclkmsk = 0;

#define write_cmu(mask)	writew((mask), vr41xx_cmu_base)

void vr41xx_clock_supply(u16 mask)
{
	cmuclkmsk |= mask;
	write_cmu(cmuclkmsk);
}

void vr41xx_clock_mask(u16 mask)
{
	cmuclkmsk &= ~mask;
	write_cmu(cmuclkmsk);
}

void __init vr41xx_cmu_init(u16 mask)
{
	switch (current_cpu_data.cputype) {
        case CPU_VR4111:
        case CPU_VR4121:
                vr41xx_cmu_base = VR4111_CMUCLKMSK;
                break;
        case CPU_VR4122:
        case CPU_VR4131:
                vr41xx_cmu_base = VR4122_CMUCLKMSK;
                break;
	default:
		panic("Unexpected CPU of NEC VR4100 series");
		break;
        }

	cmuclkmsk = mask;
}
