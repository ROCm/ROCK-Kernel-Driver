/*	
 *	bootsect.S - boot sector for NEC PC-9800 series
 *
 *	Linux/98 project at Kyoto University Microcomputer Club (KMC)
 *		    FUJITA Norimasa, TAKAI Kousuke  1997-1998
 *	rewritten by TAKAI Kousuke (as86 -> gas), Nov 1999
 *
 * Based on:
 *	bootsect.S		Copyright (C) 1991, 1992 Linus Torvalds
 *	modified by Drew Eckhardt
 *	modified by Bruce Evans (bde)
 *
 * bootsect.S is loaded at 0x1FC00 or 0x1FE00 by the bios-startup routines,
 * and moves itself out of the way to address 0x90000, and jumps there.
 *
 * It then loads 'setup' directly after itself (0x90200), and the system
 * at 0x10000, using BIOS interrupts. 
 *
 * NOTE! currently system is at most (8*65536-4096) bytes long. This should 
 * be no problem, even in the future. I want to keep it simple. This 508 kB
 * kernel size should be enough, especially as this doesn't contain the
 * buffer cache as in minix (and especially now that the kernel is 
 * compressed :-)
 *
 * The loader has been made as simple as possible, and continuous
 * read errors will result in a unbreakable loop. Reboot by hand. It
 * loads pretty fast by getting whole tracks at a time whenever possible.
 */

#include <linux/config.h>		/* for CONFIG_ROOT_RDONLY */
#include <asm/boot.h>

SETUPSECTS	= 4			/* default nr of setup-sectors */
BOOTSEG		= 0x1FC0		/* original address of boot-sector */
INITSEG		= DEF_INITSEG		/* we move boot here - out of the way */
SETUPSEG	= DEF_SETUPSEG		/* setup starts here */
SYSSEG		= DEF_SYSSEG		/* system loaded at 0x10000 (65536) */
SYSSIZE		= DEF_SYSSIZE		/* system size: # of 16-byte clicks */
					/* to be loaded */
ROOT_DEV	= 0 			/* ROOT_DEV is now written by "build" */
SWAP_DEV	= 0			/* SWAP_DEV is now written by "build" */

#ifndef SVGA_MODE
#define SVGA_MODE ASK_VGA
#endif

#ifndef RAMDISK
#define RAMDISK 0
#endif 

#ifndef ROOT_RDONLY
#define ROOT_RDONLY 1
#endif

/* normal/hireso text VRAM segments */
#define NORMAL_TEXT	0xa000
#define HIRESO_TEXT	0xe000

/* bios work area addresses */
#define EXPMMSZ		0x0401
#define BIOS_FLAG	0x0501
#define	DISK_BOOT	0x0584

.code16
.text

.global _start
_start:

#if 0 /* hook for debugger, harmless unless BIOS is fussy (old HP) */
	int	$0x3
#endif
	jmp	real_start
	.ascii	"Linux 98"
	.word	0
real_start:
	xorw	%di, %di		/* %di = 0 */
	movw	%di, %ss		/* %ss = 0 */
	movw	$0x03F0, %sp
	pushw	%cx			/* for hint */

	movw	$0x0A00, %ax		/* normal mode defaults (80x25) */

	testb	$0x08, %ss:BIOS_FLAG	/* check hi-reso bit */
	jnz	set_crt_mode
/*
 * Hi-Reso (high-resolution) machine.
 *
 * Some hi-reso machines have no RAMs on bank 8/A (0x080000 - 0x0BFFFF).
 * On such machines we get two RAM banks from top of protect menory and
 * map them on bank 8/A.
 * These work-around must be done before moving myself on INITSEG (0x090000-).
 */
	movw	$(HIRESO_TEXT >> 8), %cs:(vram + 1)	/* text VRAM segment */

	/* set memory window */
	movb	$0x08, %al
	outb	%al, $0x91		/* map native RAM (if any) */
	movb	$0x0A, %al
	outb	%al, $0x93

	/* check bank ram A */
	pushw	$0xA500
	popw	%ds
	movw	(%di), %cx		/* %si == 0 from entry */
	notw	%cx
	movw	%cx, (%di)

	movw	$0x43F, %dx		/* cache flush for 486 and up. */
	movb	$0xA0, %al
	outb	%al, %dx
	
	cmpw	%cx, (%di)
	je	hireso_done

	/* 
	 * Write test failed; we have no native RAM on 080000h - 0BFFFFh.
	 * Take 256KB of RAM from top of protected memory.
	 */
	movb	%ss:EXPMMSZ, %al
	subb	$2, %al			/* reduce 2 x 128KB */
	movb	%al, %ss:EXPMMSZ
	addb	%al, %al
	addb	$0x10, %al
	outb	%al, $0x91
	addb	$2, %al
	outb	%al, $0x93

hireso_done:
	movb	$0x10, %al		/* CRT mode 80x31, %ah still 0Ah */

set_crt_mode:
	int	$0x18			/* set CRT mode */

	movb	$0x0C, %ah		/* turn on text displaying */
	int	$0x18

	xorw	%dx, %dx		/* position cursor to home */
	movb	$0x13, %ah
	int	$0x18

	movb	$0x11, %ah		/* turn cursor displaying on */
	int	$0x18

	/* move 1 kilobytes from [BOOTSEG:0000h] to [INITSEG:0000h] */
	cld
	xorw	%si, %si
	pushw	$INITSEG
	popw	%es
	movw	$512, %cx		/* %di == 0 from entry */
	rep
	cs
	movsw

	ljmp	$INITSEG, $go

go:
	pushw	%cs
	popw	%ds		/* %ds = %cs */

	popw	%dx		/* %dh = saved %ch passed from BIOS */
	movb	%ss:DISK_BOOT, %al
	andb	$0xf0, %al	/* %al = Device Address */
	movb	$18, %ch	/* 18 secs/track,  512 b/sec (1440 KB) */
	cmpb	$0x30, %al
	je	try512
	cmpb	$0x90, %al	/* 1 MB I/F, 1 MB floppy */
	je	try1.2M
	cmpb	$0xf0, %al	/* 640 KB I/F, 1 MB floppy */
	je	try1.2M
	movb	$9, %ch		/*  9 secs/track,  512 b/sec ( 720 KB) */
	cmpb	$0x10, %al	/* 1 MB I/F, 640 KB floppy */
	je	try512
	cmpb	$0x70, %al	/* 640 KB I/F, 640 KB floppy */
	jne	error		/* unknown device? */

	/* XXX: Does it make sense to support 8 secs/track, 512 b/sec 
		(640 KB) floppy? */

try512:	movb	$2, %cl		/* 512 b/sec */
lasttry:call	tryload
/*
 * Display error message and halt
 */
error:	movw	$error_msg, %si
	call	print
wait_reboot:
	movb	$0x0, %ah
	int	$0x18			/* wait keyboard input */
1:	movb	$0, %al
	outb	%al, $0xF0		/* reset CPU */
	jmp	1b			/* just in case... */

try1.2M:cmpb	$2, %dh
	je	try2HC
	movw	$0x0803, %cx	/*  8 secs/track, 1024 b/sec (1232 KB) */
	call	tryload
	movb	$15, %ch	/* 15 secs/track,  512 b/sec (1200 KB) */
	jmp	try512
try2HC:	movw	$0x0F02, %cx	/* 15 secs/track,  512 b/sec (1200 KB) */
	call	tryload
	movw	$0x0803, %cx	/*  8 secs/track, 1024 b/sec (1232 KB) */
	jmp	lasttry

/*
 * Try to load SETUP and SYSTEM provided geometry information in %cx.
 * This routine *will not* return on successful load...
 */
tryload:
	movw	%cx, sectlen
	movb	%ss:DISK_BOOT, %al
	movb	$0x7, %ah		/* recalibrate the drive */
	int	$0x1b
	jc	error			/* recalibration should succeed */

	/*
	 * Load SETUP into memory. It is assumed that SETUP fits into
	 * first cylinder (2 tracks, 9KB on 2DD, 15-18KB on 2HD).
	 */
	movb	$0, %bl
	movb	setup_sects, %bh
	incb	%bh
	shlw	%bx			/* %bx = (setup_sects + 1) * 512 */
	movw	$128, %bp
	shlw	%cl, %bp		/* %bp = <sector size> */
	subw	%bp, %bx		/* length to load */
	movw	$0x0002, %dx		/* head 0, sector 2 */
	movb	%cl, %ch		/* `N' for sector address */
	movb	$0, %cl			/* cylinder 0 */
	pushw	%cs
	popw	%es			/* %es = %cs (= INITSEG) */
	movb	$0xd6, %ah		/* read, multi-track, MFM */
	int	$0x1b			/* load it! */
	jc	read_error

	movw	$loading_msg, %si
	call	print

	movw	$SYSSEG, %ax
	movw	%ax, %es		/* %es = SYSSEG */

/*
 * This routine loads the system at address 0x10000, making sure
 * no 64kB boundaries are crossed. We try to load it as fast as
 * possible, loading whole tracks whenever we can.
 *
 * in:	es - starting address segment (normally 0x1000)
 */
	movb	%ch, %cl
	addb	$7, %cl			/* %cl = log2 <sector_size> */
	shrw	%cl, %bx		/* %bx = # of phys. sectors in SETUP */
	addb	%bl, %dl		/* %dl = start sector # of SYSTEM */
	decb	%dl			/* %dl is 0-based in below loop */

rp_read_newseg:
	xorw	%bp, %bp		/* = starting address within segment */
#ifdef __BIG_KERNEL__
	bootsect_kludge = 0x220		/* 0x200 (size of bootsector) + 0x20 (offset */
	lcall	*bootsect_kludge	/* of bootsect_kludge in setup.S */
#else
	movw	%es, %ax
	subw	$SYSSEG, %ax
#endif
	cmpw	syssize, %ax
	ja	boot			/* done! */

rp_read:
	movb	sectors, %al
	addb	%al, %al
	movb	%al, %ch		/* # of sectors on both surface */
	subb	%dl, %al		/* # of sectors left on this track */
	movb	$0, %ah
	shlw	%cl, %ax		/* # of bytes left on this track */
	movw	%ax, %bx		/* transfer length */
	addw	%bp, %ax		/* cross 64K boundary? */
	jnc	1f			/* ok. */
	jz	1f			/* also ok. */
	/*
	 * Oops, we are crossing 64K boundary...
	 * Adjust transfer length to make transfer fit in the boundary.
	 *
	 * Note: sector size is assumed to be a measure of 65536.
	 */
	xorw	%bx, %bx
	subw	%bp, %bx
1:	pushw	%dx
	movw	$dot_msg, %si		/* give progress message */
	call	print
	xchgw	%ax, %dx
	movb	$0, %ah
	divb	sectors
	xchgb	%al, %ah
	xchgw	%ax, %dx		/* %dh = head # / %dl = sector # */
	incb	%dl			/* fix %dl to 1-based */
	pushw	%cx
	movw	cylinder, %cx
	movb	$0xd6, %ah		/* read, multi-track, seek, MFM */
	movb	%ss:DISK_BOOT, %al
	int	$0x1b
	popw	%cx
	popw	%dx
	jc	read_error
	movw	%bx, %ax		/* # of bytes just read */
	shrw	%cl, %ax		/* %ax = # of sectors just read */
	addb	%al, %dl		/* advance sector # */
	cmpb	%ch, %dl		/* %ch = # of sectors/cylinder */
	jb	2f
	incb	cylinder		/* next cylinder */
	xorb	%dl, %dl		/* sector 0 */
2:	addw	%bx, %bp		/* advance offset pointer */
	jnc	rp_read
	/* offset pointer wrapped; advance segment pointer. */
	movw	%es, %ax
	addw	$0x1000, %ax
	movw	%ax, %es
	jmp	rp_read_newseg

read_error:
	ret

boot:	movw	%cs, %ax		/* = INITSEG */
	/* movw	%ax, %ds */
	movw	%ax, %ss
	movw	$0x4000, %sp		/* 0x4000 is arbitrary value >=
					 * length of bootsect + length of
					 * setup + room for stack;
					 * PC-9800 never have BIOS workareas
					 * on high memory.
					 */
/*
 * After that we check which root-device to use. If the device is
 * not defined, /dev/fd0 (2, 0) will be used.
 */
	cmpw	$0, root_dev
	jne	3f
	movb	$2, root_dev+1
3:

/*
 * After that (everything loaded), we jump to the setup-routine
 * loaded directly after the bootblock:
 */
	ljmp	$SETUPSEG, $0

/*
 * Subroutine for print string on console.
 *	%cs:%si	- pointer to message
 */
print:
	pushaw
	pushw	%ds
	pushw	%es
	pushw	%cs
	popw	%ds
	lesw	curpos, %di		/* %es:%di = current text VRAM addr. */
1:	xorw	%ax, %ax
	lodsb
	testb	%al, %al
	jz	2f			/* end of string */
	stosw					/* character code */
	movb	$0xE1, %es:0x2000-2(%di)	/* character attribute */
	jmp	1b
2:	movw	%di, %dx
	movb	$0x13, %ah
	int	$0x18			/* move cursor to current point */
	popw	%es
	popw	%ds
	popaw
	ret

loading_msg:
	.string	"Loading"
dot_msg:
	.string	"."
error_msg:
	.string	"Read Error!"

	.org	490

curpos:	.word	160		/* current cursor position */
vram:	.word	NORMAL_TEXT	/* text VRAM segment */

cylinder:	.byte	0	/* current cylinder (lower byte)	*/
sectlen:	.byte	0	/* (log2 of <sector size>) - 7		*/
sectors:	.byte	0x0F	/* default is 2HD (15 sector/track)	*/

# XXX: This is a fairly snug fit.

.org 497
setup_sects:	.byte SETUPSECTS
root_flags:	.word ROOT_RDONLY
syssize:	.word SYSSIZE
swap_dev:	.word SWAP_DEV
ram_size:	.word RAMDISK
vid_mode:	.word SVGA_MODE
root_dev:	.word ROOT_DEV
boot_flag:	.word 0xAA55
