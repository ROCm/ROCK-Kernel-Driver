/*
 *  ISA DMA support functions
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

/*
 * Defining following add some delay. Maybe this helps for some broken
 * ISA DMA controllers.
 */

#undef HAVE_REALLY_SLOW_DMA_CONTROLLER

#include <sound/driver.h>
#include <sound/core.h>
#include <asm/dma.h>

/*
 *
 */
 
void snd_dma_program(unsigned long dma,
		     unsigned long addr, unsigned int size,
                     unsigned short mode)
{
	unsigned long flags;

	flags = claim_dma_lock();
	disable_dma(dma);
	clear_dma_ff(dma);
	set_dma_mode(dma, mode);
	set_dma_addr(dma, addr);
	set_dma_count(dma, size);
	if (!(mode & DMA_MODE_NO_ENABLE))
		enable_dma(dma);
	release_dma_lock(flags);
}

void snd_dma_disable(unsigned long dma)
{
	unsigned long flags;

	flags = claim_dma_lock();
	clear_dma_ff(dma);
	disable_dma(dma);
	release_dma_lock(flags);
}

unsigned int snd_dma_residue(unsigned long dma)
{
	unsigned long flags;
	unsigned int result;

	flags = claim_dma_lock();
	clear_dma_ff(dma);
	if (!isa_dma_bridge_buggy)
		disable_dma(dma);
	result = get_dma_residue(dma);
	if (!isa_dma_bridge_buggy)
		enable_dma(dma);
	release_dma_lock(flags);
	return result;
}
