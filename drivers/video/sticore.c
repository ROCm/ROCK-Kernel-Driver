#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/io.h>

#include "sti.h"

struct sti_struct default_sti = {
	SPIN_LOCK_UNLOCKED,
};

static struct sti_font_flags default_font_flags = {
	STI_WAIT, 0, 0, NULL
};

/* The colour indices used by STI are
 *   0 - Black
 *   1 - White
 *   2 - Red
 *   3 - Yellow/Brown
 *   4 - Green
 *   5 - Cyan
 *   6 - Blue
 *   7 - Magenta
 *
 * So we have the same colours as VGA (basically one bit each for R, G, B),
 * but have to translate them, anyway. */

static u8 col_trans[8] = {
        0, 6, 4, 5,
        2, 7, 3, 1
};

#define c_fg(sti, c) col_trans[((c>> 8) & 7)]
#define c_bg(sti, c) col_trans[((c>>11) & 7)]
#define c_index(sti, c) (c&0xff)

static struct sti_init_flags default_init_flags = {
	STI_WAIT, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, NULL
};

void
sti_init_graph(struct sti_struct *sti) 
{
	struct sti_init_inptr_ext inptr_ext = {
		0, { 0 }, 0, NULL
	};
	struct sti_init_inptr inptr = {
		3, STI_PTR(&inptr_ext)
	};
	struct sti_init_outptr outptr = { 0 };
	unsigned long flags;
	s32 ret;

	spin_lock_irqsave(&sti->lock, flags);

	ret = STI_CALL(sti->init_graph, &default_init_flags, &inptr,
		&outptr, sti->glob_cfg);

	spin_unlock_irqrestore(&sti->lock, flags);

	sti->text_planes = outptr.text_planes;
}

static struct sti_conf_flags default_conf_flags = {
	STI_WAIT, 0, NULL
};

void
sti_inq_conf(struct sti_struct *sti)
{
	struct sti_conf_inptr inptr = { NULL };
	struct sti_conf_outptr_ext outptr_ext = { future_ptr: NULL };
	struct sti_conf_outptr outptr = {
		ext_ptr: STI_PTR(&outptr_ext)
	};
	unsigned long flags;
	s32 ret;
	
	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->inq_conf, &default_conf_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while(ret == 1);
}

void
sti_putc(struct sti_struct *sti, int c, int y, int x)
{
	struct sti_font_inptr inptr = {
		(u32) sti->font, c_index(sti, c), c_fg(sti, c), c_bg(sti, c),
		x * sti_font_x(sti), y * sti_font_y(sti), NULL
	};
	struct sti_font_outptr outptr = {
		0, NULL
	};
	s32 ret;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->font_unpmv, &default_font_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while(ret == 1);
}

static struct sti_blkmv_flags clear_blkmv_flags = {
	STI_WAIT, 1, 1, 0, 0, NULL
};

void
sti_set(struct sti_struct *sti, int src_y, int src_x,
	int height, int width, u8 color)
{
	struct sti_blkmv_inptr inptr = {
		color, color,
		src_x, src_y ,
		src_x, src_y ,
		width, height,
		NULL
	};
	struct sti_blkmv_outptr outptr = { 0, NULL };
	s32 ret = 0;
	unsigned long flags;
	
	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->block_move, &clear_blkmv_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while(ret == 1);
}

void
sti_clear(struct sti_struct *sti, int src_y, int src_x,
	  int height, int width)
{
	struct sti_blkmv_inptr inptr = {
		0, 0,
		src_x * sti_font_x(sti), src_y * sti_font_y(sti),
		src_x * sti_font_x(sti), src_y * sti_font_y(sti),
		width * sti_font_x(sti), height* sti_font_y(sti),
		NULL
	};
	struct sti_blkmv_outptr outptr = { 0, NULL };
	s32 ret = 0;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->block_move, &clear_blkmv_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while(ret == 1);
}

static struct sti_blkmv_flags default_blkmv_flags = {
	STI_WAIT, 0, 0, 0, 0, NULL
};

void
sti_bmove(struct sti_struct *sti, int src_y, int src_x,
	  int dst_y, int dst_x, int height, int width)
{
	struct sti_blkmv_inptr inptr = {
		0, 0,
		src_x * sti_font_x(sti), src_y * sti_font_y(sti),
		dst_x * sti_font_x(sti), dst_y * sti_font_y(sti),
		width * sti_font_x(sti), height* sti_font_y(sti),
		NULL
	};
	struct sti_blkmv_outptr outptr = { 0, NULL };
	s32 ret = 0;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->block_move, &default_blkmv_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while(ret == 1);
}


static void __init
sti_rom_copy(unsigned long base, unsigned long offset,
	     unsigned long count, void *dest)
{
	void *savedest = dest;
	int savecount = count;

	while(count >= 4) {
		count -= 4;
		*(u32 *)dest = gsc_readl(base + offset);
		offset += 4;
		dest += 4;
	}
	while(count) {
		count--;
		*(u8 *)dest = gsc_readb(base + offset);
		offset++;
		dest++;
	}
	__flush_dcache_range((unsigned long) dest, count);
	__flush_icache_range((unsigned long) dest, count);
}

static void dump_sti_rom(struct sti_rom *rom)
{
	printk("STI word mode ROM type %d\n", rom->type[3]);
	printk(" supports %d monitors\n", rom->num_mons);
	printk(" conforms to STI ROM spec revision %d.%02x\n",
		rom->revno[0] >> 4, rom->revno[0] & 0x0f);
	printk(" graphics id %02x%02x%02x%02x%02x%02x%02x%02x\n",
		rom->graphics_id[0], 
		rom->graphics_id[1], 
		rom->graphics_id[2], 
		rom->graphics_id[3], 
		rom->graphics_id[4], 
		rom->graphics_id[5], 
		rom->graphics_id[6], 
		rom->graphics_id[7]);
	printk(" font start %08x\n", rom->font_start);
	printk(" region list %08x\n", rom->region_list);
	printk(" init_graph %08x\n", rom->init_graph);
	printk(" alternate code type %d\n", rom->alt_code_type);
}

static void __init sti_cook_fonts(struct sti_cooked_rom *cooked_rom,
				  struct sti_rom *raw_rom)
{
	struct sti_rom_font *raw_font;
	struct sti_cooked_font *cooked_font;
	struct sti_rom_font *font_start;
	
	cooked_font =
		kmalloc(sizeof *cooked_font, GFP_KERNEL);
	if(!cooked_font)
		return;

	cooked_rom->font_start = cooked_font;

	raw_font = ((void *)raw_rom) + (raw_rom->font_start);

	font_start = raw_font;
	cooked_font->raw = raw_font;

	while(raw_font->next_font) {
		raw_font = ((void *)font_start) + (raw_font->next_font);
		
		cooked_font->next_font =
			kmalloc(sizeof *cooked_font, GFP_KERNEL);
		if(!cooked_font->next_font)
			return;

		cooked_font = cooked_font->next_font;

		cooked_font->raw = raw_font;
	}

	cooked_font->next_font = NULL;
}

static int font_index, font_height, font_width;

static int __init sti_font_setup(char *str)
{
	char *x;

	/* we accept sti_font=10x20, sti_font=10*20 or sti_font=7 style
	 * command lines. */

	if((x = strchr(str, 'x')) || (x = strchr(str, '*'))) {
		font_height = simple_strtoul(str, NULL, 0);
		font_width = simple_strtoul(x+1, NULL, 0);
	} else {
		font_index = simple_strtoul(str, NULL, 0);
	}

	return 0;
}

__setup("sti_font=", sti_font_setup);

static int __init sti_search_font(struct sti_cooked_rom *rom,
				  int height, int width)
{
	struct sti_cooked_font *font;
	int i = 0;
	
	for(font = rom->font_start; font; font = font->next_font, i++) {
		if((font->raw->width == width) && (font->raw->height == height))
			return i;
	}

	return 0;
}

static struct sti_cooked_font * __init
sti_select_font(struct sti_cooked_rom *rom)
{
	struct sti_cooked_font *font;
	int i;

	if(font_width && font_height)
		font_index = sti_search_font(rom, font_height, font_width);

	for(font = rom->font_start, i = font_index;
	    font && (i > 0);
	    font = font->next_font, i--);

	if(font)
		return font;
	else
		return rom->font_start;
}
	
static void __init
sti_dump_globcfg_ext(struct sti_glob_cfg_ext *cfg)
{
	printk(	"monitor %d\n"
		"in friendly mode: %d\n"
		"power consumption %d watts\n"
		"freq ref %d\n"
		"sti_mem_addr %p\n",
		cfg->curr_mon,
		cfg->friendly_boot,
		cfg->power,
		cfg->freq_ref,
		cfg->sti_mem_addr);
}

void __init
sti_dump_globcfg(struct sti_glob_cfg *glob_cfg)
{
	printk(	"%d text planes\n"
		"%4d x %4d screen resolution\n"
		"%4d x %4d offscreen\n"
		"%4d x %4d layout\n"
		"regions at %08x %08x %08x %08x\n"
		"regions at %08x %08x %08x %08x\n"
		"reent_lvl %d\n"
		"save_addr %p\n",
		glob_cfg->text_planes,
		glob_cfg->onscreen_x, glob_cfg->onscreen_y,
		glob_cfg->offscreen_x, glob_cfg->offscreen_y,
		glob_cfg->total_x, glob_cfg->total_y,
		glob_cfg->region_ptrs[0], glob_cfg->region_ptrs[1],
		glob_cfg->region_ptrs[2], glob_cfg->region_ptrs[3],
		glob_cfg->region_ptrs[4], glob_cfg->region_ptrs[5],
		glob_cfg->region_ptrs[6], glob_cfg->region_ptrs[7],
		glob_cfg->reent_lvl,
		glob_cfg->save_addr);
	sti_dump_globcfg_ext(PTR_STI(glob_cfg->ext_ptr));
}
		
static void __init
sti_init_glob_cfg(struct sti_struct *sti, unsigned long hpa,
		  unsigned long rom_address)
{
	struct sti_glob_cfg *glob_cfg;
	struct sti_glob_cfg_ext *glob_cfg_ext;
	void *save_addr;
	void *sti_mem_addr;

	glob_cfg = kmalloc(sizeof *sti->glob_cfg, GFP_KERNEL);
	glob_cfg_ext = kmalloc(sizeof *glob_cfg_ext, GFP_KERNEL);
	save_addr = kmalloc(1024 /*XXX*/, GFP_KERNEL);
	sti_mem_addr = kmalloc(1024, GFP_KERNEL);

	if((!glob_cfg) || (!glob_cfg_ext) || (!save_addr) || (!sti_mem_addr))
		return;

	memset(glob_cfg, 0, sizeof *glob_cfg);
	memset(glob_cfg_ext, 0, sizeof *glob_cfg_ext);
	memset(save_addr, 0, 1024);
	memset(sti_mem_addr, 0, 1024);

	glob_cfg->ext_ptr = STI_PTR(glob_cfg_ext);
	glob_cfg->save_addr = STI_PTR(save_addr);
	glob_cfg->region_ptrs[0] = ((sti->regions[0]>>18)<<12) + rom_address;
	glob_cfg->region_ptrs[1] = ((sti->regions[1]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[2] = ((sti->regions[2]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[3] = ((sti->regions[3]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[4] = ((sti->regions[4]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[5] = ((sti->regions[5]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[6] = ((sti->regions[6]>>18)<<12) + hpa;
	glob_cfg->region_ptrs[7] = ((sti->regions[7]>>18)<<12) + hpa;
	
	glob_cfg_ext->sti_mem_addr = STI_PTR(sti_mem_addr);

	sti->glob_cfg = STI_PTR(glob_cfg);
}

/* address is a pointer to a word mode or pci rom */
static struct sti_struct * __init
sti_read_rom(unsigned long address)
{
	struct sti_struct *ret = NULL;
	struct sti_cooked_rom *cooked = NULL;
	struct sti_rom *raw = NULL;
	unsigned long size;

	ret = &default_sti;

	if(!ret)
		goto out_err;

	cooked = kmalloc(sizeof *cooked, GFP_KERNEL);
	raw = kmalloc(sizeof *raw, GFP_KERNEL);
	
	if(!(raw && cooked))
		goto out_err;

	/* reallocate raw */
	sti_rom_copy(address, 0, sizeof *raw, raw);

	dump_sti_rom(raw);

	size = raw->last_addr;
	/* kfree(raw); */
	raw = kmalloc(size, GFP_KERNEL);
	if(!raw)
		goto out_err;
	sti_rom_copy(address, 0, size, raw);

	sti_cook_fonts(cooked, raw);
#if 0
	sti_cook_regions(cooked, raw);
	sti_cook_functions(cooked, raw);
#endif

	if(raw->region_list) {
		ret->regions = kmalloc(32, GFP_KERNEL);	/* FIXME */

		memcpy(ret->regions, ((void *)raw)+raw->region_list, 32);
	}

	address = virt_to_phys(raw);

	ret->font_unpmv = address+(raw->font_unpmv & 0x03ffffff);
	ret->block_move = address+(raw->block_move & 0x03ffffff);
	ret->init_graph = address+(raw->init_graph & 0x03ffffff);
	ret->inq_conf = address+(raw->inq_conf     & 0x03ffffff);

	ret->rom = cooked;
	ret->rom->raw = raw;

	ret->font = (struct sti_rom_font *) virt_to_phys(sti_select_font(ret->rom)->raw);

	return ret;

out_err:
	if(raw)
		kfree(raw);
	if(cooked)
		kfree(cooked);

	return NULL;
}

static struct sti_struct * __init
sti_try_rom(unsigned long address, unsigned long hpa)
{
	struct sti_struct *sti = NULL;
	u16 sig;
	
test_rom:
	/* if we can't read the ROM, bail out early.  Not being able
	 * to read the hpa is okay, for romless sti */
	if(pdc_add_valid((void*)address))
		return NULL;

	printk("found potential STI ROM at %08lx\n", address);

	sig = le16_to_cpu(gsc_readw(address));

	if((sig==0x55aa) || (sig==0xaa55)) {
		address += le32_to_cpu(gsc_readl(address+8));
		printk("sig %04x, PCI STI ROM at %08lx\n",
		       sig, address);

		goto test_rom;
	}

	if((sig&0xff) == 0x01) {
		printk("STI byte mode ROM at %08lx, ignored\n",
		       address);

		sti = NULL;
	}

	if(sig == 0x0303) {
		printk("STI word mode ROM at %08lx\n",
		       address);

		sti = sti_read_rom(address);
	}

	if (!sti)
		return NULL;

	/* this is hacked.  We need a better way to find out the HPA for
	 * romless STI (eg search for the graphics devices we know about
	 * by sversion) */
	if (!pdc_add_valid((void *)0xf5000000)) printk("f4000000 g\n");
	if (!pdc_add_valid((void *)0xf7000000)) printk("f6000000 g\n");
	if (!pdc_add_valid((void *)0xf9000000)) printk("f8000000 g\n");
	if (!pdc_add_valid((void *)0xfb000000)) printk("fa000000 g\n");
	sti_init_glob_cfg(sti, hpa, address);

	sti_init_graph(sti);

	sti_inq_conf(sti);
	sti_dump_globcfg(PTR_STI(sti->glob_cfg));

	return sti;
}

static unsigned long sti_address;
static unsigned long sti_hpa;

/* XXX: should build a list of STI ROMs */
struct sti_struct * __init
sti_init_roms(void)
{
	struct sti_struct *tmp = NULL, *sti = NULL;

	/* handle the command line */
	if (sti_address && sti_hpa) {
		return sti_try_rom(sti_address, sti_hpa);
	}

	/* 712, 715, some other boxes don't have a separate STI ROM,
	 * but use part of the regular flash */
	if (PAGE0->proc_sti) {
		printk("STI ROM from PDC at %08x\n", PAGE0->proc_sti);
		if (!pdc_add_valid((void *)0xf9000000))
			sti = sti_try_rom(PAGE0->proc_sti, 0xf8000000);
		else if (!pdc_add_valid((void *)0xf5000000))
			sti = sti_try_rom(PAGE0->proc_sti, 0xf4000000);
		else if (!pdc_add_valid((void *)0xf7000000))
			sti = sti_try_rom(PAGE0->proc_sti, 0xf6000000);
		else if (!pdc_add_valid((void *)0xfb000000))
			sti = sti_try_rom(PAGE0->proc_sti, 0xfa000000);

	}

	/* standard locations for GSC graphic devices */
	if (!pdc_add_valid((void *)0xf4000000))
		tmp = sti_try_rom(0xf4000000, 0xf4000000);
	sti = tmp ? tmp : sti;
	if (!pdc_add_valid((void *)0xf6000000))
		tmp = sti_try_rom(0xf6000000, 0xf6000000);
	sti = tmp ? tmp : sti;
	if (!pdc_add_valid((void *)0xf8000000))
		tmp = sti_try_rom(0xf8000000, 0xf8000000);
	sti = tmp ? tmp : sti;
	if (!pdc_add_valid((void *)0xfa000000))
		tmp = sti_try_rom(0xfa000000, 0xfa000000);
	sti = tmp ? tmp : sti;

	return sti;
}

static int __init
sti_setup(char *str)
{
	char *end;

	if(strcmp(str, "pdc") == 0) {
		sti_address = PAGE0->proc_sti;

		return 1;
	} else {
		sti_address = simple_strtoul(str, &end, 16);

		if((end == str) || (sti_address < 0xf0000000)) {
			sti_address = 0;
			return 0;
		}

		sti_hpa = sti_address;

		return 1;
	}

	return 0;
}

__setup("sti=", sti_setup);
