#ifndef __SOUND_INITVAL_H
#define __SOUND_INITVAL_H

/*
 *  Init values for soundcard modules
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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

#ifndef MODULE_GENERIC_STRING
#ifdef MODULE
#define MODULE_GENERIC_STRING(name, string) \
static const char __module_generic_string_##name [] \
  __attribute__ ((unused, __section__(".modstring"))) = #name "=" string;
#else
#define MODULE_GENERIC_STRING(name, string)
#endif
#endif

#define MODULE_CLASSES(val) MODULE_GENERIC_STRING(info_classes, val)
#define MODULE_DEVICES(val) MODULE_GENERIC_STRING(info_devices, val)
#define MODULE_PARM_SYNTAX(id, val) MODULE_GENERIC_STRING(info_parm_##id, val)

#define SNDRV_AUTO_PORT		1
#define SNDRV_AUTO_IRQ		0xffff
#define SNDRV_AUTO_DMA		0xffff
#define SNDRV_AUTO_DMA_SIZE	(0x7fffffff)

#define SNDRV_DEFAULT_IDX1	(-1)
#define SNDRV_DEFAULT_STR1	NULL
#define SNDRV_DEFAULT_ENABLE1	1
#define SNDRV_DEFAULT_PORT1	SNDRV_AUTO_PORT
#define SNDRV_DEFAULT_IRQ1	SNDRV_AUTO_IRQ
#define SNDRV_DEFAULT_DMA1	SNDRV_AUTO_DMA
#define SNDRV_DEFAULT_DMA_SIZE1	SNDRV_AUTO_DMA_SIZE
#define SNDRV_DEFAULT_PTR1	SNDRV_DEFAULT_STR1

#define SNDRV_DEFAULT_IDX	{ [0 ... (SNDRV_CARDS-1)] = -1 }
#define SNDRV_DEFAULT_STR	{ [0 ... (SNDRV_CARDS-1)] = NULL }
#define SNDRV_DEFAULT_ENABLE	{ 1, [1 ... (SNDRV_CARDS-1)] = 0 }
#define SNDRV_DEFAULT_ENABLE_PNP { [0 ... (SNDRV_CARDS-1)] = 1 }
#ifdef CONFIG_PNP
#define SNDRV_DEFAULT_ENABLE_ISAPNP SNDRV_DEFAULT_ENABLE_PNP
#else
#define SNDRV_DEFAULT_ENABLE_ISAPNP SNDRV_DEFAULT_ENABLE
#endif
#define SNDRV_DEFAULT_PORT	{ [0 ... (SNDRV_CARDS-1)] = SNDRV_AUTO_PORT }
#define SNDRV_DEFAULT_IRQ	{ [0 ... (SNDRV_CARDS-1)] = SNDRV_AUTO_IRQ }
#define SNDRV_DEFAULT_DMA	{ [0 ... (SNDRV_CARDS-1)] = SNDRV_AUTO_DMA }
#define SNDRV_DEFAULT_DMA_SIZE	{ [0 ... (SNDRV_CARDS-1)] = SNDRV_AUTO_DMA_SIZE }
#define SNDRV_DEFAULT_PTR	SNDRV_DEFAULT_STR

#define SNDRV_BOOLEAN_TRUE_DESC	"allows:{{0,Disabled},{1,Enabled}},default:1,dialog:check"
#define SNDRV_BOOLEAN_FALSE_DESC "allows:{{0,Disabled},{1,Enabled}},default:0,dialog:check"

#define SNDRV_ENABLED		"enable:(enable)"

#define SNDRV_INDEX_DESC	SNDRV_ENABLED ",allows:{{0,7}},unique,skill:required,dialog:list"
#define SNDRV_ID_DESC		SNDRV_ENABLED ",unique"
#define SNDRV_ENABLE_DESC	SNDRV_BOOLEAN_FALSE_DESC
#define SNDRV_ISAPNP_DESC	SNDRV_ENABLED "," SNDRV_BOOLEAN_TRUE_DESC
#define SNDRV_DMA8_DESC		SNDRV_ENABLED ",allows:{{0,1},{3}},dialog:list"
#define SNDRV_DMA16_DESC	SNDRV_ENABLED ",allows:{{5,7}},dialog:list"
#define SNDRV_DMA_DESC		SNDRV_ENABLED ",allows:{{0,1},{3},{5,7}},dialog:list"
#define SNDRV_IRQ_DESC		SNDRV_ENABLED ",allows:{{5},{7},{9},{10,12},{14,15}},dialog:list"
#define SNDRV_DMA_SIZE_DESC	SNDRV_ENABLED ",allows:{{4,128}},default:64,skill:advanced"
#define SNDRV_DMA8_SIZE_DESC	SNDRV_ENABLED ",allows:{{4, 64}},default:64,skill:advanced"
#define SNDRV_DMA16_SIZE_DESC	SNDRV_ENABLED ",allows:{{4,128}},default:64,skill:advanced"
#define SNDRV_PORT12_DESC	SNDRV_ENABLED ",allows:{{0,0x3fff}},base:16"
#define SNDRV_PORT_DESC		SNDRV_ENABLED ",allows:{{0,0xffff}},base:16"

#ifdef SNDRV_LEGACY_AUTO_PROBE
static int snd_legacy_auto_probe(unsigned long *ports, int (*probe)(unsigned long port))
{
	int result = 0;	/* number of detected cards */

	while ((signed long)*ports != -1) {
		if (probe(*ports) >= 0)
			result++;
		ports++;
	}
	return result;
}
#endif

#ifdef SNDRV_LEGACY_FIND_FREE_IRQ
#include <linux/interrupt.h>

static irqreturn_t snd_legacy_empty_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	return IRQ_HANDLED;
}

static int snd_legacy_find_free_irq(int *irq_table)
{
	while (*irq_table != -1) {
		if (!request_irq(*irq_table, snd_legacy_empty_irq_handler,
				 SA_INTERRUPT, "ALSA Test IRQ", (void *) irq_table)) {
			free_irq(*irq_table, (void *) irq_table);
			return *irq_table;
		}
		irq_table++;
	}
	return -1;
}
#endif

#ifdef SNDRV_LEGACY_FIND_FREE_DMA
static int snd_legacy_find_free_dma(int *dma_table)
{
	while (*dma_table != -1) {
		if (!request_dma(*dma_table, "ALSA Test DMA")) {
			free_dma(*dma_table);
			return *dma_table;
		}
		dma_table++;
	}
	return -1;
}
#endif

#endif /* __SOUND_INITVAL_H */
