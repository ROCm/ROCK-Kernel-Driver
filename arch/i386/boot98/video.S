/*	video.S
 *
 *  Video mode setup, etc. for NEC PC-9800 series.
 *
 *  Copyright (C) 1997,98,99  Linux/98 project  <seraphim@kmc.kyoto-u.ac.jp>
 *
 *  Based on the video.S for IBM PC:
 *	copyright (C) Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

/* Positions of various video parameters passed to the kernel */
/* (see also include/linux/tty.h) */
#define PARAM_CURSOR_POS	0x00
#define PARAM_VIDEO_PAGE	0x04
#define PARAM_VIDEO_MODE	0x06
#define PARAM_VIDEO_COLS	0x07
#define PARAM_VIDEO_EGA_BX	0x0a
#define PARAM_VIDEO_LINES	0x0e
#define PARAM_HAVE_VGA		0x0f
#define PARAM_FONT_POINTS	0x10

#define PARAM_VIDEO98_COMPAT	0x0a
#define PARAM_VIDEO98_HIRESO	0x0b
#define PARAM_VIDEO98_MACHTYPE	0x0c
#define PARAM_VIDEO98_LINES	0x0e
#define PARAM_VIDEO98_COLS	0x0f

# PARAM_LFB_* and PARAM_VESAPM_* are unused on PC-9800.

# This is the main entry point called by setup.S
# %ds *must* be pointing to the bootsector
video:	xorw	%ax, %ax
	movw	%ax, %es			# %es = 0

	movb	%es:BIOS_FLAG, %al
	movb	%al, PARAM_VIDEO_MODE

	movb	$0, PARAM_VIDEO98_HIRESO	# 0 = normal
	movw	$NORMAL_TEXT, PARAM_VIDEO_PAGE
	testb	$0x8, %al
	movw	$(80 * 256 + 25), %ax
	jz	1f
	# hireso machine.
	movb	$1, PARAM_VIDEO98_HIRESO	# !0 = hi-reso
	movb	$(HIRESO_TEXT >> 8), PARAM_VIDEO_PAGE + 1
	movw	$(80 * 256 + 31), %ax
1:	movw	%ax, PARAM_VIDEO98_LINES	# also sets VIDEO98_COLS

	movb	$0xc0, %ch			# 400-line graphic mode
	movb	$0x42, %ah
	int	$0x18

	movw	$80, PARAM_VIDEO_COLS

	movw	$msg_probing, %si
	call	prtstr_cs

# Check vendor from font pattern of `A'...

1:	inb	$0x60, %al			# wait V-sync
	testb	$0x20, %al
	jnz	1b
2:	inb	$0x60, %al
	testb	$0x20, %al
	jz	2b

	movb	$0x00, %al			# select font of `A'
	outb	%al, $0xa1
	movb	$0x41, %al
	outb	%al, $0xa3

	movw	$8, %cx
	movw	PARAM_VIDEO_PAGE, %ax
	cmpw	$NORMAL_TEXT, %ax
	je	3f
	movb	$24, %cl			# for hi-reso machine
3:	addw	$0x400, %ax			# %ax = CG window segment
	pushw	%ds
	movw	%ax, %ds
	xorw	%dx, %dx			# get sum of `A' pattern...
	xorw	%si, %si
4:	lodsw
	addw	%ax, %dx
	loop	4b
	popw	%ds

	movw	%dx, %ax
	movw	$msg_nec, %si
	xorw	%bx, %bx			# vendor info will go into %bx
	testb	$8, %es:BIOS_FLAG
	jnz	check_hireso_vendor
	cmpw	$0xc7f8, %ax
	je	5f
	jmp	6f
check_hireso_vendor:
	cmpw	$0x9639, %ax			# XXX: NOT VERIFIED!!!
	je	5f
6:	incw	%bx				# compatible machine
	movw	$msg_compat, %si
5:	movb	%bl, PARAM_VIDEO98_COMPAT
	call	prtstr_cs

	movw	$msg_fontdata, %si
	call	prtstr_cs			# " (CG sum of A = 0x"
	movw	%dx, %ax
	call	prthex
	call	prtstr_cs			# ") PC-98"

	movb	$'0', %al
	pushw	%ds
	pushw	$0xf8e8
	popw	%ds
	cmpw	$0x2198, (0)
	popw	%ds
	jne	7f
	movb	$'2', %al
7:	call	prtchr
	call	prtstr_cs			# "1 "

	movb	$0, PARAM_VIDEO98_MACHTYPE
#if 0	/* XXX - This check is bogus? [0000:BIOS_FLAG2]-bit7 does NOT
		 indicate whether it is a note machine, but merely indicates
		 whether it has ``RAM drive''. */
# check note machine
	testb	$0x80, %es:BIOS_FLAG2
	jnz	is_note
	pushw	%ds
	pushw	$0xfd80
	popw	%ds
	movb	(4), %al
	popw	%ds
	cmpb	$0x20, %al			# EPSON note A
	je	epson_note
	cmpb	$0x22, %al			# EPSON note W
	je	epson_note
	cmpb	$0x27, %al			# EPSON note AE
	je	epson_note
	cmpb	$0x2a, %al			# EPSON note WR
	jne	note_done
epson_note:
	movb	$1, PARAM_VIDEO98_MACHTYPE
	movw	$msg_note, %si
	call	prtstr_cs
note_done:
#endif
	
# print h98 ? (only NEC)
	cmpb	$0, PARAM_VIDEO98_COMPAT
	jnz	8f				# not NEC -> not H98

	testb	$0x80, %es:BIOS_FLAG5
	jz	8f				# have NESA bus -> H98
	movw	$msg_h98, %si
	call	prtstr_cs
	orb	$2, PARAM_VIDEO98_MACHTYPE
8:	testb	$0x40, %es:BIOS_FLAG5
	jz	9f
	movw	$msg_gs, %si
	call	prtstr_cs			# only prints it :-)
9:
	movw	$msg_normal, %si		# "normal"
	testb	$0x8, %es:BIOS_FLAG
	jz	1f
	movw	$msg_hireso, %si
1:	call	prtstr_cs

	movw	$msg_sysclk, %si
	call	prtstr_cs
	movb	$'5', %al
	testb	$0x80, %es:BIOS_FLAG
	jz	2f
	movb	$'8', %al
2:	call	prtchr
	call	prtstr_cs

#if 0
	testb	$0x40, %es:(0x45c)
	jz	no_30line			# no 30-line support

	movb	%es:KB_SHFT_STS, %al
	testb	$0x01, %al			# is SHIFT key pressed?
	jz	no_30line

	testb	$0x10, %al			# is CTRL key pressed?
	jnz	line40

	# switch to 30-line mode
	movb	$30, PARAM_VIDEO98_LINES
	movw	$msg_30line, %si
	jmp	3f

line40:
	movb	$37, PARAM_VIDEO98_LINES
	movw	$40, PARAM_VIDEO_LINES
	movw	$msg_40line, %si
3:	call	prtstr_cs

	movb	$0x32, %bh
	movw	$0x300c, %ax
	int	$0x18				# switch video mode
	movb	$0x0c, %ah
	int	$0x18				# turn on text plane
	movw	%cs:cursor_address, %dx
	movb	$0x13, %ah
	int	$0x18				# move cursor to correct place
	mov	$0x11, %ah
	int	$0x18				# turn on text plane

	call	prtstr_cs			# "Ok.\r\n"
no_30line:
#endif
	ret

prtstr_cs:
	pushw	%ds
	pushw	%cs
	popw	%ds
	call	prtstr
	popw	%ds
	ret

# prthex is for debugging purposes, and prints %ax in hexadecimal.
prthex:	pushw	%cx
	movw	$4, %cx
1:	rolw	$4, %ax
	pushw	%ax
	andb	$0xf, %al
	cmpb	$10, %al
	sbbb	$0x69, %al
	das
	call	prtchr
	popw	%ax
	loop	1b
	popw	%cx
	ret

msg_probing:	.string	"Probing machine: "

msg_nec:	.string	"NEC"
msg_compat:	.string	"compatible"

msg_fontdata:	.string	" (CG sum of A = 0x"
		.string	") PC-98"
		.string	"1 "

msg_gs:		.string	"(GS) "
msg_h98:	.string	"(H98) "

msg_normal:	.string	"normal"
msg_hireso:	.string	"Hi-reso"

msg_sysclk:	.string	" mode, system clock "
		.string	"MHz\r\n"

#if 0
msg_40line:	# cpp will concat following lines, so the assembler can deal.
		.ascii	"\
Video mode will be adjusted to 37-line (so-called ``40-line'') mode later.\r\n\
THIS MODE MAY DAMAGE YOUR MONITOR PHYSICALLY. USE AT YOUR OWN RISK.\r\n"
msg_30line:	.string	"Switching video mode to 30-line (640x480) mode... "
		.string	"Ok.\r\n"
#endif
