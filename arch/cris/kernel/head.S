	;; $Id: head.S,v 1.11 2001/01/16 16:31:38 bjornw Exp $
	;; 
	;; Head of the kernel - alter with care
	;;
	;; Copyright (C) 2000, 2001 Axis Communications AB
	;;
	;; Authors:	Bjorn Wesen (bjornw@axis.com)
	;; 
	;; $Log: head.S,v $
	;; Revision 1.11  2001/01/16 16:31:38  bjornw
	;; * Changed name and semantics of running_from_flash to romfs_in_flash,
	;;   set by head.S to indicate to setup.c whether there is a cramfs image
	;;   after the kernels BSS or not. Should work for all three boot-cases
	;;   (DRAM with cramfs in DRAM, DRAM with cramfs in flash (compressed boot),
	;;    and flash with cramfs in flash)
	;;
	;; Revision 1.10  2001/01/16 14:12:21  bjornw
	;; * Check for cramfs start passed in r9 from the decompressor, if all other
	;;   cramfs options fail (if we boot from DRAM but don't find a cramfs image
	;;   after the kernel in DRAM, it is probably still in the flash)
	;; * Check magic in cramfs detection when booting from flash directly
	;;
	;; Revision 1.9  2001/01/15 17:17:02  bjornw
	;; * Corrected the code that detects the cramfs lengths
	;; * Added a comment saying that the above does not work due to other
	;;   reasons..
	;;
	;; Revision 1.8  2001/01/15 16:27:51  jonashg
	;; Made boot after flashing work.
	;; * end destination is __vmlinux_end in RAM.
	;; * _romfs_start moved because of virtual memory.
	;;
	;; Revision 1.7  2000/11/21 13:55:29  bjornw
	;; Use CONFIG_CRIS_LOW_MAP for the low VM map instead of explicit CPU type
	;;
	;; Revision 1.6  2000/10/06 12:36:55  bjornw
	;; Forgot swapper_pg_dir when changing memory map..
	;;
	;; Revision 1.5  2000/10/04 16:49:30  bjornw
	;; * Fixed memory mapping in LX
	;; * Check for cramfs instead of romfs
	;;
	;;
	
#include <linux/config.h>
#define ASSEMBLER_MACROS_ONLY
#include <asm/sv_addr_ag.h>

#define CRAMFS_MAGIC 0x28cd3d45
	
	;; exported symbols
		
	.globl	_etrax_irv
	.globl	_romfs_start
	.globl	_romfs_length
	.globl	_romfs_in_flash
	.globl  _swapper_pg_dir
					
	.text

	;; This is the entry point of the kernel. We are in supervisor mode.
	;; 0x00000000 if Flash, 0x40004000 if DRAM
	;; since etrax actually starts at address 2 when booting from flash, we
	;; put a nop (2 bytes) here first so we dont accidentally skip the di
	;;
	;; NOTICE! The register r9 is used as a parameter carrying register from
	;; the decompressor (if the kernel was compressed). It should not be
	;; used in the code below until it is read.
	
	nop	
	di

	;; First setup the kseg_c mapping from where the kernel is linked
	;; to 0x40000000 (where the actual DRAM resides) otherwise
	;; we cannot do very much! See arch/cris/README.mm
	;;
	;; Notice that since we're potentially running at 0x00 or 0x40 right now,
	;; we will get a fault as soon as we enable the MMU if we dont
	;; temporarily map those segments linearily.
	;;
	;; Due to a bug in Etrax-100 LX version 1 we need to map the memory
	;; slightly different. We also let the simulator get this mapping for now.

#ifdef CONFIG_CRIS_LOW_MAP
	move.d	0x0800b000, r0	; kseg mappings
	move.d	r0, [R_MMU_KBASE_HI]

	move.d	0x04040000, r0	; temporary map of 0x40->0x40 and 0x00->0x00 
	move.d	r0, [R_MMU_KBASE_LO]

	move.d	0x80074871, r0	; mmu enable, segs e,b,6,5,4,0 segment mapped
	move.d	r0, [R_MMU_CONFIG]
#else
	move.d	0x0804b000, r0	; kseg mappings
	move.d	r0, [R_MMU_KBASE_HI]

	move.d	0x00040000, r0	; temporary map of 0x40->0x40 and 0x00->0x00 
	move.d	r0, [R_MMU_KBASE_LO]

	move.d	0x8007d811, r0	; mmu enable, segs f,e,c,b,4,0 segment mapped
	move.d	r0, [R_MMU_CONFIG]
#endif

	;; Now we need to sort out the segments and their locations in RAM or
	;; Flash. The image in the Flash (or in DRAM) consists of 3 pieces:
	;; 1) kernel text, 2) kernel data, 3) ROM filesystem image
	;; But the linker has linked the kernel to expect this layout in
	;; DRAM memory:
	;; 1) kernel text, 2) kernel data, 3) kernel BSS
	;; (the location of the ROM filesystem is determined by the krom driver)
	;; If we boot this from Flash, we want to keep the ROM filesystem in
	;; the flash, we want to copy the text and need to copy the data to DRAM.
	;; But if we boot from DRAM, we need to move the ROMFS image
	;; from its position after kernel data, to after kernel BSS, BEFORE the
	;; kernel starts using the BSS area (since its "overlayed" with the ROMFS)
	;;
	;; In both cases, we start in un-cached mode, and need to jump into a
	;; cached PC after we're done fiddling around with the segments.
	;; 
	;; arch/etrax100/etrax100.ld sets some symbols that define the start
	;; and end of each segment.
	
	;; Check if we start from DRAM or FLASH by testing PC
	
	move.d	pc,r0
	and.d	0x7fffffff,r0	; get rid of the non-cache bit
	cmp.d	0x10000,r0	; arbitrary... just something above this code
	bcs	inflash
	nop

	jump	inram		; enter cached ram
	
inflash:

#ifndef CONFIG_SVINTO_SIM	

	;; We need to setup the bus registers before we start using the DRAM

	move.d   DEF_R_WAITSTATES, r0
	move.d   r0, [R_WAITSTATES]

	move.d   DEF_R_BUS_CONFIG, r0
	move.d   r0, [R_BUS_CONFIG]
  
	move.d   DEF_R_DRAM_CONFIG, r0
	move.d   r0, [R_DRAM_CONFIG]

	move.d   DEF_R_DRAM_TIMING, r0
	move.d   r0, [R_DRAM_TIMING]

#endif
	;; Copy text+data to DRAM
	;; This is fragile - the calculation of r4 as the image size depends
	;; on that the labels below actually are the first and last positions
	;; in the linker-script.
	;;
	;; Then the locating of the cramfs image depends on the aforementioned
	;; image being located in the flash at 0. This is most often not true,
	;; thus the following does not work (normally there is a rescue-block
	;; between the physical start of the flash and the flash-image start,
	;; and when run with compression, the kernel is actually unpacked to
	;; DRAM and we never get here in the first place :))
	
	moveq	0, r0			; source
	move.d	_text_start, r1		; destination
	move.d	__vmlinux_end, r2	; end destination
	move.d	r2, r4
	sub.d	r1, r4			; r4=__vmlinux_end in flash, used below
1:	move.w	[r0+], r3
	move.w	r3, [r1+]
	cmp.d	r2, r1
	bcs	1b
	nop

	;; We keep the cramfs in the flash.
	;; There might be none, but that does not matter because
	;; we don't do anything than read some bytes here.

	moveq	0, r0
	move.d	r0, [_romfs_length] ; default if there is no cramfs

	move.d  [r4], r0	; cramfs_super.magic
	cmp.d	CRAMFS_MAGIC, r0
	bne	1f
	nop
	move.d	[r4 + 4], r0	; cramfs_super.size
	move.d	r0, [_romfs_length]
#ifdef CONFIG_CRIS_LOW_MAP
	add.d   0x50000000, r4	; add flash start in virtual memory (cached)
#else
	add.d   0xf0000000, r4	; add flash start in virtual memory (cached)
#endif
	move.d	r4, [_romfs_start]
1:	
	moveq	1, r0
	move.d	r0, [_romfs_in_flash]
		
	jump	start_it	; enter code, cached this time

inram:
	;; Move the ROM fs to after BSS end. This assumes that the cramfs
	;; second longword contains the length of the cramfs
	
	moveq	0, r0
	move.d	r0, [_romfs_length] ; default if there is no cramfs
	
	;; First check if there is a cramfs (magic value)
	;; Notice that we check for cramfs magic value - which is
	;; the "rom fs" we'll possibly use in 2.4 if not JFFS (which does
	;; not need this mechanism anyway)
	
	move.d	__vmlinux_end, r0 ; the image will be after the vmlinux end address
	move.d	[r0], r1	; cramfs assumes same endian on host/target
	cmp.d	CRAMFS_MAGIC, r1; magic value in cramfs superblock
	bne	no_romfs_in_ram
	nop

	;; Ok. What is its size ? 
	
	move.d	[r0 + 4], r2	; cramfs_super.size (again, no need to swapwb)

	;; We want to copy it to the end of the BSS

	move.d	_end, r1

	;; Remember values so cramfs and setup can find this info

	move.d	r1, [_romfs_start]	; new romfs location
	move.d	r2, [_romfs_length]

	;; We need to copy it backwards, since they can be overlapping

	add.d	r2, r0
	add.d	r2, r1
		
	;; Go ahead. Make my loop.

	lsrq	1, r2		; size is in bytes, we copy words

1:	move.w	[r0=r0-2],r3
	move.w	r3,[r1=r1-2]
	subq	1, r2
	bne	1b
	nop
	
	;; Dont worry that the BSS is tainted. It will be cleared later.

	moveq	0, r0
	move.d	r0, [_romfs_in_flash]

	jump	start_it	; better skip the additional cramfs check below
			
no_romfs_in_ram:

	;; We have still one other possibility at this point - the kernel
	;; could have been unpacked to DRAM by the loader, but the cramfs
	;; image was still in the Flash directly after the compressed kernel
	;; image. The loader passes the address of the byte succeeding the
	;; last compressed byte in the flash in the register r9 when starting
	;; the kernel. Check if r9 points to a decent cramfs image!
	;; (Notice that if this is not booted from the loader, r9 will be
	;;  garbage but we do sanity checks on it, the chance that it points
	;;  to a cramfs magic is small.. )
	
	cmp.d	0x0ffffff8, r9
	bcc	1f		; r9 points outside the flash area
	nop
	move.d	[r9], r0	; cramfs_super.magic
	cmp.d	CRAMFS_MAGIC, r0
	bne	1f
	nop
	move.d	[r9+4], r0	; cramfs_super.length
	move.d	r0, [_romfs_length]
#ifdef CONFIG_CRIS_LOW_MAP
	add.d   0x50000000, r9	; add flash start in virtual memory (cached)
#else
	add.d   0xf0000000, r9	; add flash start in virtual memory (cached)
#endif
	move.d	r9, [_romfs_start]

	moveq	1, r0
	move.d	r0, [_romfs_in_flash]
1:	
	
	jump	start_it	; enter code, cached this time
	
start_it:
	;; the kernel stack is overlayed with the task structure for each
	;; task. thus the initial kernel stack is in the same page as the
	;; init_task (but starts in the top of the page, size 8192)
	move.d	_init_task_union + 8192,sp
	move.d	_ibr_start,r0	; this symbol is set by the linker script 
	move	r0,ibr
	move.d	r0,[_etrax_irv]	; set the interrupt base register and pointer

	;; Clear BSS region, from _bss_start to _end

	move.d	__bss_start, r0
	move.d	_end, r1
1:	clear.d	[r0+]
	cmp.d	r1, r0
	bcs	1b
	nop
	
#ifdef CONFIG_BLK_DEV_ETRAXIDE
	;; disable ATA before enabling it in genconfig below
	moveq	0,r0
	move.d	r0,[R_ATA_CTRL_DATA]
	move.d	r0,[R_ATA_TRANSFER_CNT]
	move.d	r0,[R_ATA_CONFIG]
#if 0
	move.d	R_PORT_G_DATA,r1
	move.d	r0,[r1]; assert ATA bus-reset
	nop
	nop
	nop
	nop
	nop
	nop
	move.d	0x08000000,r0
	move.d	r0,[r1]
#endif
#endif

#ifdef CONFIG_JULIETTE
	;; configure external DMA channel 0 before enabling it in genconfig
	
	moveq	0,r0
	move.d	r0,[R_EXT_DMA_0_ADDR]
	move.d	0x860000,r0	; cnt enable, word size, output, stop, size 0
	move.d	r0,[R_EXT_DMA_0_CMD]

	;; reset dma4 and wait for completion
	
	moveq	4,r0
	move.b	r0,[R_DMA_CH4_CMD]
w4u:	move.b	[R_DMA_CH4_CMD],r0
	and.b	7,r0
	cmp.b	4,r0
	beq	w4u
	nop

	;; reset dma5 and wait for completion
	
	moveq	4,r0
	move.b	r0,[R_DMA_CH5_CMD]
w5u:	move.b	[R_DMA_CH5_CMD],r0
	and.b	7,r0
	cmp.b	4,r0
	beq	w5u
	nop
#endif	
			
	;; Etrax product HW genconfig setup

	moveq	0,r0
#if !defined(CONFIG_KGDB) && !defined(CONFIG_DMA_MEMCPY)
	or.d	0x140000,r0	; DMA channels 6 and 7 to ser0, kgdb doesnt want DMA
#endif
#if !defined(CONFIG_KGDB) || !defined(CONFIG_DEBUG_PORT1)
	or.d	0xc00000,r0	; DMA channels 8 and 9 to ser1, kgdb doesnt want DMA
#endif
#ifdef CONFIG_DMA_MEMCPY
	or.d	0x003c0000,r0	; 6/7 memory-memory DMA
#endif
#ifdef CONFIG_ETRAX100_SERIAL_PORT2
	or.d	0x2808,r0	; DMA channels 2 and 3 to serport 2, port 2 enabled
#endif
#ifdef CONFIG_ETRAX100_SERIAL_PORT3
	or.d	0x28100,r0	; DMA channels 4 and 5 to serport 3, port 3 enabled
#endif
#if defined(CONFIG_ETRAX100_PARALLEL_PORT0) || defined(CONFIG_ETRAX_ETHERNET_LPSLAVE)
	or.w	0x4,r0	; parport 0 enabled using DMA 2/3
#endif
#if defined(CONFIG_ETRAX100_PARALLEL_PORT1) || defined(CONFIG_ETRAX_ETHERNET_LPSLAVE)
	or.w	0x80,r0	; parport 1 enabled using DMA 4/5
#endif
#ifdef CONFIG_BLK_DEV_ETRAXIDE
	or.d	0x3c02,r0	; DMA channels 2 and 3 to ATA, ATA enabled
#endif
#ifdef CONFIG_JULIETTE
	or.d	0x3c000,r0	; DMA channels 4 and 5 to EXTDMA0, for Juliette
#ifndef CONFIG_BLK_DEV_ETRAXIDE
	or.d	0x41,r0		; HACK for now! To make G27 connected for the RTC
#endif
#endif
	move.d	r0,[_genconfig_shadow] ; init a shadow register of R_GEN_CONFIG

#ifndef CONFIG_SVINTO_SIM
	move.d	r0,[R_GEN_CONFIG]

#if 0
	moveq	4,r0
	move.b	r0,[R_DMA_CH6_CMD]	; reset (ser0 dma out)
	move.b	r0,[R_DMA_CH7_CMD]	; reset (ser0 dma in)
w61:	move.b	[R_DMA_CH6_CMD],r0	; wait for reset cycle to finish
	and.b	7,r0
	cmp.b	4,r0
	beq	w61
	nop
w71:	move.b	[R_DMA_CH7_CMD],r0	; wait for reset cycle to finish
	and.b	7,r0
	cmp.b	4,r0
	beq	w71
	nop
#endif
	
	moveq	4,r0
	move.b	r0,[R_DMA_CH8_CMD]	; reset (ser1 dma out)
	move.b	r0,[R_DMA_CH9_CMD]	; reset (ser1 dma in)
w81:	move.b	[R_DMA_CH8_CMD],r0	; wait for reset cycle to finish
	and.b	7,r0
	cmp.b	4,r0
	beq	w81
	nop
w91:	move.b	[R_DMA_CH9_CMD],r0	; wait for reset cycle to finish
	and.b	7,r0
	cmp.b	4,r0
	beq	w91
	nop

	;; setup port PA and PB default initial directions and data
	;; including their shadow registers
		
	move.b	DEF_R_PORT_PA_DIR,r0
	move.b	r0,[_port_pa_dir_shadow]
	move.b	r0,[R_PORT_PA_DIR]
	move.b	DEF_R_PORT_PA_DATA,r0
	move.b	r0,[_port_pa_data_shadow]
	move.b	r0,[R_PORT_PA_DATA]
	
	move.b	DEF_R_PORT_PB_CONFIG,r0
	move.b	r0,[_port_pb_config_shadow]
	move.b	r0,[R_PORT_PB_CONFIG]
	move.b	DEF_R_PORT_PB_DIR,r0
	move.b	r0,[_port_pb_dir_shadow]
	move.b	r0,[R_PORT_PB_DIR]
	move.b	DEF_R_PORT_PB_DATA,r0
	move.b	r0,[_port_pb_data_shadow]
	move.b	r0,[R_PORT_PB_DATA]

	moveq	0,r0
	move.d	r0,[_port_g_data_shadow]
	move.d	r0,[R_PORT_G_DATA]
	
	;; setup the serial port 0 at 115200 baud for debug purposes
	
	moveq	0,r0
	move.d	r0,[R_SERIAL0_XOFF] 

	move.b	0x99,r0
	move.b	r0,[R_SERIAL0_BAUD]	; 115.2kbaud for both transmit and receive

	move.b	0x40,r0			; rec enable
	move.b	r0,[R_SERIAL0_REC_CTRL] 
	
	move.b	0x40,r0			; tr enable
	move.b	r0,[R_SERIAL0_TR_CTRL]

	;; setup the serial port 1 at 115200 baud for debug purposes
	
	moveq	0,r0
	move.d	r0,[R_SERIAL1_XOFF] 

	move.b	0x99,r0
	move.b	r0,[R_SERIAL1_BAUD]	; 115.2kbaud for both transmit and receive

	move.b	0x40,r0			; rec enable
	move.b	r0,[R_SERIAL1_REC_CTRL] 
	
	move.b	0x40,r0			; tr enable
	move.b	r0,[R_SERIAL1_TR_CTRL]

#ifdef CONFIG_ETRAX_90000000_LEDS
	;; clear LED's on Stallone and Olga boards
	moveq	-1,r0
	move.d	r0,[_port_90000000_shadow]
	move.d	r0,[0x90000000]
#endif
	
#ifdef CONFIG_ETRAX100_SERIAL_PORT3	
	;; setup the serial port 3 at 115200 baud for debug purposes
	
	moveq	0,r0
	move.d	r0,[R_SERIAL3_XOFF] 

	move.b	0x99,r0
	move.b	r0,[R_SERIAL3_BAUD]	; 115.2kbaud for both transmit and receive

	move.b	0x40,r0			; rec enable
	move.b	r0,[R_SERIAL3_REC_CTRL] 
	
	move.b	0x40,r0			; tr enable
	move.b	r0,[R_SERIAL3_TR_CTRL]
#endif
	
#endif /* CONFIG_SVINTO_SIM */

	jump	_start_kernel	; jump into the C-function _start_kernel in init/main.c

	
	.data
_etrax_irv:	
	.dword	0
_romfs_start:
	.dword	0
_romfs_length:
	.dword	0
_romfs_in_flash:
	.dword	0

	;; put some special pages at the beginning of the kernel aligned
	;; to page boundaries - the kernel cannot start until after this

#ifdef CONFIG_CRIS_LOW_MAP
_swapper_pg_dir = 0x60002000
#else	
_swapper_pg_dir = 0xc0002000
#endif
