/*
 *  Machine specific resource allocation for PC-9800.
 *  Written by Osamu Tomita <tomita@cinet.co.jp>
 */

#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/std_resources.h>

static char str_pic1[] = "pic1";
static char str_dma[] = "dma";
static char str_pic2[] = "pic2";
static char str_calender_clock[] = "calender clock";
static char str_system[] = "system";
static char str_nmi_control[] = "nmi control";
static char str_kanji_rom[] = "kanji rom";
static char str_keyboard[] = "keyboard";
static char str_text_gdc[] = "text gdc";
static char str_crtc[] = "crtc";
static char str_timer[] = "timer";
static char str_graphic_gdc[] = "graphic gdc";
static char str_dma_ex_bank[] = "dma ex. bank";
static char str_beep_freq[] = "beep freq.";
static char str_mouse_pio[] = "mouse pio";
struct resource standard_io_resources[] = {
	{ str_pic1, 0x00, 0x00, IORESOURCE_BUSY },
	{ str_dma, 0x01, 0x01, IORESOURCE_BUSY },
	{ str_pic1, 0x02, 0x02, IORESOURCE_BUSY },
	{ str_dma, 0x03, 0x03, IORESOURCE_BUSY },
	{ str_dma, 0x05, 0x05, IORESOURCE_BUSY },
	{ str_dma, 0x07, 0x07, IORESOURCE_BUSY },
	{ str_pic2, 0x08, 0x08, IORESOURCE_BUSY },
	{ str_dma, 0x09, 0x09, IORESOURCE_BUSY },
	{ str_pic2, 0x0a, 0x0a, IORESOURCE_BUSY },
	{ str_dma, 0x0b, 0x0b, IORESOURCE_BUSY },
	{ str_dma, 0x0d, 0x0d, IORESOURCE_BUSY },
	{ str_dma, 0x0f, 0x0f, IORESOURCE_BUSY },
	{ str_dma, 0x11, 0x11, IORESOURCE_BUSY },
	{ str_dma, 0x13, 0x13, IORESOURCE_BUSY },
	{ str_dma, 0x15, 0x15, IORESOURCE_BUSY },
	{ str_dma, 0x17, 0x17, IORESOURCE_BUSY },
	{ str_dma, 0x19, 0x19, IORESOURCE_BUSY },
	{ str_dma, 0x1b, 0x1b, IORESOURCE_BUSY },
	{ str_dma, 0x1d, 0x1d, IORESOURCE_BUSY },
	{ str_dma, 0x1f, 0x1f, IORESOURCE_BUSY },
	{ str_calender_clock, 0x20, 0x20, 0 },
	{ str_dma, 0x21, 0x21, IORESOURCE_BUSY },
	{ str_calender_clock, 0x22, 0x22, 0 },
	{ str_dma, 0x23, 0x23, IORESOURCE_BUSY },
	{ str_dma, 0x25, 0x25, IORESOURCE_BUSY },
	{ str_dma, 0x27, 0x27, IORESOURCE_BUSY },
	{ str_dma, 0x29, 0x29, IORESOURCE_BUSY },
	{ str_dma, 0x2b, 0x2b, IORESOURCE_BUSY },
	{ str_dma, 0x2d, 0x2d, IORESOURCE_BUSY },
	{ str_system, 0x31, 0x31, IORESOURCE_BUSY },
	{ str_system, 0x33, 0x33, IORESOURCE_BUSY },
	{ str_system, 0x35, 0x35, IORESOURCE_BUSY },
	{ str_system, 0x37, 0x37, IORESOURCE_BUSY },
	{ str_nmi_control, 0x50, 0x50, IORESOURCE_BUSY },
	{ str_nmi_control, 0x52, 0x52, IORESOURCE_BUSY },
	{ "time stamp", 0x5c, 0x5f, IORESOURCE_BUSY },
	{ str_kanji_rom, 0xa1, 0xa1, IORESOURCE_BUSY },
	{ str_kanji_rom, 0xa3, 0xa3, IORESOURCE_BUSY },
	{ str_kanji_rom, 0xa5, 0xa5, IORESOURCE_BUSY },
	{ str_kanji_rom, 0xa7, 0xa7, IORESOURCE_BUSY },
	{ str_kanji_rom, 0xa9, 0xa9, IORESOURCE_BUSY },
	{ str_keyboard, 0x41, 0x41, IORESOURCE_BUSY },
	{ str_keyboard, 0x43, 0x43, IORESOURCE_BUSY },
	{ str_text_gdc, 0x60, 0x60, IORESOURCE_BUSY },
	{ str_text_gdc, 0x62, 0x62, IORESOURCE_BUSY },
	{ str_text_gdc, 0x64, 0x64, IORESOURCE_BUSY },
	{ str_text_gdc, 0x66, 0x66, IORESOURCE_BUSY },
	{ str_text_gdc, 0x68, 0x68, IORESOURCE_BUSY },
	{ str_text_gdc, 0x6a, 0x6a, IORESOURCE_BUSY },
	{ str_text_gdc, 0x6c, 0x6c, IORESOURCE_BUSY },
	{ str_text_gdc, 0x6e, 0x6e, IORESOURCE_BUSY },
	{ str_crtc, 0x70, 0x70, IORESOURCE_BUSY },
	{ str_crtc, 0x72, 0x72, IORESOURCE_BUSY },
	{ str_crtc, 0x74, 0x74, IORESOURCE_BUSY },
	{ str_crtc, 0x74, 0x74, IORESOURCE_BUSY },
	{ str_crtc, 0x76, 0x76, IORESOURCE_BUSY },
	{ str_crtc, 0x78, 0x78, IORESOURCE_BUSY },
	{ str_crtc, 0x7a, 0x7a, IORESOURCE_BUSY },
	{ str_timer, 0x71, 0x71, IORESOURCE_BUSY },
	{ str_timer, 0x73, 0x73, IORESOURCE_BUSY },
	{ str_timer, 0x75, 0x75, IORESOURCE_BUSY },
	{ str_timer, 0x77, 0x77, IORESOURCE_BUSY },
	{ str_graphic_gdc, 0xa0, 0xa0, IORESOURCE_BUSY },
	{ str_graphic_gdc, 0xa2, 0xa2, IORESOURCE_BUSY },
	{ str_graphic_gdc, 0xa4, 0xa4, IORESOURCE_BUSY },
	{ str_graphic_gdc, 0xa6, 0xa6, IORESOURCE_BUSY },
	{ "cpu", 0xf0, 0xf7, IORESOURCE_BUSY },
	{ "fpu", 0xf8, 0xff, IORESOURCE_BUSY },
	{ str_dma_ex_bank, 0x0e05, 0x0e05, 0 },
	{ str_dma_ex_bank, 0x0e07, 0x0e07, 0 },
	{ str_dma_ex_bank, 0x0e09, 0x0e09, 0 },
	{ str_dma_ex_bank, 0x0e0b, 0x0e0b, 0 },
	{ str_beep_freq, 0x3fd9, 0x3fd9, IORESOURCE_BUSY },
	{ str_beep_freq, 0x3fdb, 0x3fdb, IORESOURCE_BUSY },
	{ str_beep_freq, 0x3fdd, 0x3fdd, IORESOURCE_BUSY },
	{ str_beep_freq, 0x3fdf, 0x3fdf, IORESOURCE_BUSY },
	/* All PC-9800 have (exactly) one mouse interface.  */
	{ str_mouse_pio, 0x7fd9, 0x7fd9, 0 },
	{ str_mouse_pio, 0x7fdb, 0x7fdb, 0 },
	{ str_mouse_pio, 0x7fdd, 0x7fdd, 0 },
	{ str_mouse_pio, 0x7fdf, 0x7fdf, 0 },
	{ "mouse timer", 0xbfdb, 0xbfdb, 0 },
	{ "mouse irq", 0x98d7, 0x98d7, 0 },
};

#define STANDARD_IO_RESOURCES (sizeof(standard_io_resources)/sizeof(struct resource))

static struct resource tvram_resource = { "Text VRAM/CG window", 0xa0000, 0xa4fff, IORESOURCE_BUSY };
static struct resource gvram_brg_resource = { "Graphic VRAM (B/R/G)", 0xa8000, 0xbffff, IORESOURCE_BUSY };
static struct resource gvram_e_resource = { "Graphic VRAM (E)", 0xe0000, 0xe7fff, IORESOURCE_BUSY };

/* System ROM resources */
#define MAXROMS 6
static struct resource rom_resources[MAXROMS] = {
	{ "System ROM", 0xe8000, 0xfffff, IORESOURCE_BUSY }
};

void __init probe_roms(void)
{
	int i;
	__u8 *xrom_id;
	int roms = 1;

	request_resource(&iomem_resource, rom_resources+0);

	xrom_id = (__u8 *) isa_bus_to_virt(PC9800SCA_XROM_ID + 0x10);

	for (i = 0; i < 16; i++) {
		if (xrom_id[i] & 0x80) {
			int j;

			for (j = i + 1; j < 16 && (xrom_id[j] & 0x80); j++)
				;
			rom_resources[roms].start = 0x0d0000 + i * 0x001000;
			rom_resources[roms].end = 0x0d0000 + j * 0x001000 - 1;
			rom_resources[roms].name = "Extension ROM";
			rom_resources[roms].flags = IORESOURCE_BUSY;

			request_resource(&iomem_resource,
					  rom_resources + roms);
			if (++roms >= MAXROMS)
				return;
		}
	}
}

void __init request_graphics_resource(void)
{
	int i;

	if (PC9800_HIGHRESO_P()) {
		tvram_resource.start = 0xe0000;
		tvram_resource.end   = 0xe4fff;
		gvram_brg_resource.name  = "Graphic VRAM";
		gvram_brg_resource.start = 0xc0000;
		gvram_brg_resource.end   = 0xdffff;
	}

	request_resource(&iomem_resource, &tvram_resource);
	request_resource(&iomem_resource, &gvram_brg_resource);
	if (!PC9800_HIGHRESO_P())
		request_resource(&iomem_resource, &gvram_e_resource);

	if (PC9800_HIGHRESO_P() || PC9800_9821_P()) {
		static char graphics[] = "graphics";
		static struct resource graphics_resources[] = {
			{ graphics, 0x9a0, 0x9a0, 0 },
			{ graphics, 0x9a2, 0x9a2, 0 },
			{ graphics, 0x9a4, 0x9a4, 0 },
			{ graphics, 0x9a6, 0x9a6, 0 },
			{ graphics, 0x9a8, 0x9a8, 0 },
			{ graphics, 0x9aa, 0x9aa, 0 },
			{ graphics, 0x9ac, 0x9ac, 0 },
			{ graphics, 0x9ae, 0x9ae, 0 },
		};

#define GRAPHICS_RESOURCES (sizeof(graphics_resources)/sizeof(struct resource))

		for (i = 0; i < GRAPHICS_RESOURCES; i++)
			request_resource(&ioport_resource, graphics_resources + i);
	}
}

void __init request_standard_io_resources(void)
{
	int i;

	for (i = 0; i < STANDARD_IO_RESOURCES; i++)
		request_resource(&ioport_resource, standard_io_resources+i);
}
