/*
 *	setup.S		Copyright (C) 1991, 1992 Linus Torvalds
 *
 * setup.s is responsible for getting the system data from the BIOS,
 * and putting them into the appropriate places in system memory.
 * both setup.s and system has been loaded by the bootblock.
 *
 * This code asks the bios for memory/disk/other parameters, and
 * puts them in a "safe" place: 0x90000-0x901FF, ie where the
 * boot-block used to be. It is then up to the protected mode
 * system to read them from there before the area is overwritten
 * for buffer-blocks.
 *
 * Move PS/2 aux init code to psaux.c
 * (troyer@saifr00.cfsat.Honeywell.COM) 03Oct92
 *
 * some changes and additional features by Christoph Niemann,
 * March 1993/June 1994 (Christoph.Niemann@linux.org)
 *
 * add APM BIOS checking by Stephen Rothwell, May 1994
 * (sfr@canb.auug.org.au)
 *
 * High load stuff, initrd support and position independency
 * by Hans Lermen & Werner Almesberger, February 1996
 * <lermen@elserv.ffm.fgan.de>, <almesber@lrc.epfl.ch>
 *
 * Video handling moved to video.S by Martin Mares, March 1996
 * <mj@k332.feld.cvut.cz>
 *
 * Extended memory detection scheme retwiddled by orc@pell.chi.il.us (david
 * parsons) to avoid loadlin confusion, July 1997
 *
 * Transcribed from Intel (as86) -> AT&T (gas) by Chris Noe, May 1999.
 * <stiker@northlink.com>
 *
 * Fix to work around buggy BIOSes which dont use carry bit correctly
 * and/or report extended memory in CX/DX for e801h memory size detection 
 * call.  As a result the kernel got wrong figures.  The int15/e801h docs
 * from Ralf Brown interrupt list seem to indicate AX/BX should be used
 * anyway.  So to avoid breaking many machines (presumably there was a reason
 * to orginally use CX/DX instead of AX/BX), we do a kludge to see
 * if CX/DX have been changed in the e801 call and if so use AX/BX .
 * Michael Miller, April 2001 <michaelm@mjmm.org>
 *
 * New A20 code ported from SYSLINUX by H. Peter Anvin. AMD Elan bugfixes
 * by Robert Schwebel, December 2001 <robert@schwebel.de>
 *
 * Heavily modified for NEC PC-9800 series by Kyoto University Microcomputer
 * Club (KMC) Linux/98 project <seraphim@kmc.kyoto-u.ac.jp>, 1997-1999
 */

#include <linux/config.h>
#include <asm/segment.h>
#include <linux/version.h>
#include <linux/compile.h>
#include <asm/boot.h>
#include <asm/e820.h>
#include <asm/page.h>
	
/* Signature words to ensure LILO loaded us right */
#define SIG1	0xAA55
#define SIG2	0x5A5A

#define HIRESO_TEXT	0xe000
#define NORMAL_TEXT	0xa000

#define BIOS_FLAG2	0x0400
#define BIOS_FLAG5	0x0458
#define RDISK_EQUIP	0x0488
#define BIOS_FLAG	0x0501
#define KB_SHFT_STS	0x053a
#define DISK_EQUIP	0x055c

INITSEG  = DEF_INITSEG		# 0x9000, we move boot here, out of the way
SYSSEG   = DEF_SYSSEG		# 0x1000, system loaded at 0x10000 (65536).
SETUPSEG = DEF_SETUPSEG		# 0x9020, this is the current segment
				# ... and the former contents of CS

DELTA_INITSEG = SETUPSEG - INITSEG	# 0x0020

.code16
.globl begtext, begdata, begbss, endtext, enddata, endbss

.text
begtext:
.data
begdata:
.bss
begbss:
.text

start:
	jmp	trampoline

# This is the setup header, and it must start at %cs:2 (old 0x9020:2)

		.ascii	"HdrS"		# header signature
		.word	0x0203		# header version number (>= 0x0105)
					# or else old loadlin-1.5 will fail)
realmode_swtch:	.word	0, 0		# default_switch, SETUPSEG
start_sys_seg:	.word	SYSSEG
		.word	kernel_version	# pointing to kernel version string
					# above section of header is compatible
					# with loadlin-1.5 (header v1.5). Don't
					# change it.

type_of_loader:	.byte	0		# = 0, old one (LILO, Loadlin,
					#      Bootlin, SYSLX, bootsect...)
					# See Documentation/i386/boot.txt for
					# assigned ids
	
# flags, unused bits must be zero (RFU) bit within loadflags
loadflags:
LOADED_HIGH	= 1			# If set, the kernel is loaded high
CAN_USE_HEAP	= 0x80			# If set, the loader also has set
					# heap_end_ptr to tell how much
					# space behind setup.S can be used for
					# heap purposes.
					# Only the loader knows what is free
#ifndef __BIG_KERNEL__
		.byte	0
#else
		.byte	LOADED_HIGH
#endif

setup_move_size: .word  0x8000		# size to move, when setup is not
					# loaded at 0x90000. We will move setup 
					# to 0x90000 then just before jumping
					# into the kernel. However, only the
					# loader knows how much data behind
					# us also needs to be loaded.

code32_start:				# here loaders can put a different
					# start address for 32-bit code.
#ifndef __BIG_KERNEL__
		.long	0x1000		#   0x1000 = default for zImage
#else
		.long	0x100000	# 0x100000 = default for big kernel
#endif

ramdisk_image:	.long	0		# address of loaded ramdisk image
					# Here the loader puts the 32-bit
					# address where it loaded the image.
					# This only will be read by the kernel.

ramdisk_size:	.long	0		# its size in bytes

bootsect_kludge:
		.word  bootsect_helper, SETUPSEG

heap_end_ptr:	.word	modelist+1024	# (Header version 0x0201 or later)
					# space from here (exclusive) down to
					# end of setup code can be used by setup
					# for local heap purposes.

pad1:		.word	0
cmd_line_ptr:	.long 0			# (Header version 0x0202 or later)
					# If nonzero, a 32-bit pointer
					# to the kernel command line.
					# The command line should be
					# located between the start of
					# setup and the end of low
					# memory (0xa0000), or it may
					# get overwritten before it
					# gets read.  If this field is
					# used, there is no longer
					# anything magical about the
					# 0x90000 segment; the setup
					# can be located anywhere in
					# low memory 0x10000 or higher.

ramdisk_max:	.long MAXMEM-1		# (Header version 0x0203 or later)
					# The highest safe address for
					# the contents of an initrd

trampoline:	call	start_of_setup
		.space	1024
# End of setup header #####################################################

start_of_setup:
# Set %ds = %cs, we know that SETUPSEG = %cs at this point
	movw	%cs, %ax		# aka SETUPSEG
	movw	%ax, %ds
# Check signature at end of setup
	cmpw	$SIG1, setup_sig1
	jne	bad_sig

	cmpw	$SIG2, setup_sig2
	jne	bad_sig

	jmp	good_sig1

# Routine to print asciiz string at ds:si
prtstr:
	lodsb
	andb	%al, %al
	jz	fin

	call	prtchr
	jmp	prtstr

fin:	ret

no_sig_mess: .string	"No setup signature found ..."

good_sig1:
	jmp	good_sig

# We now have to find the rest of the setup code/data
bad_sig:
	movw	%cs, %ax			# SETUPSEG
	subw	$DELTA_INITSEG, %ax		# INITSEG
	movw	%ax, %ds
	xorb	%bh, %bh
	movb	(497), %bl			# get setup sect from bootsect
	subw	$4, %bx				# LILO loads 4 sectors of setup
	shlw	$8, %bx				# convert to words (1sect=2^8 words)
	movw	%bx, %cx
	shrw	$3, %bx				# convert to segment
	addw	$SYSSEG, %bx
	movw	%bx, %cs:start_sys_seg
# Move rest of setup code/data to here
	movw	$2048, %di			# four sectors loaded by LILO
	subw	%si, %si
	pushw	%cs
	popw	%es
	movw	$SYSSEG, %ax
	movw	%ax, %ds
	rep
	movsw
	movw	%cs, %ax			# aka SETUPSEG
	movw	%ax, %ds
	cmpw	$SIG1, setup_sig1
	jne	no_sig

	cmpw	$SIG2, setup_sig2
	jne	no_sig

	jmp	good_sig

no_sig:
	lea	no_sig_mess, %si
	call	prtstr

no_sig_loop:
	hlt
	jmp	no_sig_loop

good_sig:
	movw	%cs, %ax			# aka SETUPSEG
	subw	$DELTA_INITSEG, %ax 		# aka INITSEG
	movw	%ax, %ds
# Check if an old loader tries to load a big-kernel
	testb	$LOADED_HIGH, %cs:loadflags	# Do we have a big kernel?
	jz	loader_ok			# No, no danger for old loaders.

	cmpb	$0, %cs:type_of_loader 		# Do we have a loader that
						# can deal with us?
	jnz	loader_ok			# Yes, continue.

	pushw	%cs				# No, we have an old loader,
	popw	%ds				# die. 
	lea	loader_panic_mess, %si
	call	prtstr

	jmp	no_sig_loop

loader_panic_mess: .string "Wrong loader, giving up..."

loader_ok:
# Get memory size (extended mem, kB)

# On PC-9800, memory size detection is done completely in 32-bit
# kernel initialize code (kernel/setup.c).
	pushw	%es
	xorl	%eax, %eax
	movw	%ax, %es
	movb	%al, (E820NR)		# PC-9800 has no E820
	movb	%es:(0x401), %al
	shll	$7, %eax
	addw	$1024, %ax
	movw	%ax, (2)
	movl	%eax, (0x1e0)
	movw	%es:(0x594), %ax
	shll	$10, %eax
	addl	%eax, (0x1e0)
	popw	%es

# Check for video adapter and its parameters and allow the
# user to browse video modes.
	call	video				# NOTE: we need %ds pointing
						# to bootsector

# Get text video mode
	movb	$0x0B, %ah
	int	$0x18		# CRT mode sense
	movw	$(20 << 8) + 40, %cx
	testb	$0x10, %al
	jnz	3f
	movb	$20, %ch
	testb	$0x01, %al
	jnz	1f
	movb	$25, %ch
	jmp	1f
3:	# If bit 4 was 1, it means either 1) 31 lines for hi-reso mode,
	# or 2) 30 lines for PC-9821.
	movb	$31, %ch	# hireso mode value
	pushw	$0
	popw	%es
	testb	$0x08, %es:BIOS_FLAG
	jnz	1f
	movb	$30, %ch
1:	# Now we got # of rows in %ch
	movb	%ch, (14)

	testb	$0x02, %al
	jnz	2f
	movb	$80, %cl
2:	# Now we got # of columns in %cl
	movb	%cl, (7)

	# Next, get horizontal frequency if supported
	movw	$0x3100, %ax
	int	$0x18		# Call CRT bios
	movb	%al, (6)	# If 31h is unsupported, %al remains 0

# Get hd0-3 data...
	pushw	%ds				# aka INITSEG
	popw	%es
	xorw	%ax, %ax
	movw	%ax, %ds
	cld
	movw	$0x0080, %di
	movb	DISK_EQUIP+1, %ah
	movb	$0x80, %al

get_hd_info:
	shrb	%ah
	pushw	%ax
	jnc	1f
	movb	$0x84, %ah
	int	$0x1b
	jnc	2f				# Success
1:	xorw	%cx, %cx			# `0 cylinders' means no drive
2:	# Attention! Work area (drive_info) is arranged for PC-9800.
	movw	%cx, %ax			# # of cylinders
	stosw
	movw	%dx, %ax			# # of sectors / # of heads
	stosw
	movw	%bx, %ax			# sector size in bytes
	stosw
	popw	%ax
	incb	%al
	cmpb	$0x84, %al
	jb	get_hd_info

# Get fd data...
	movw	DISK_EQUIP, %ax
	andw	$0xf00f, %ax
	orb	%al, %ah
	movb	RDISK_EQUIP, %al
	notb	%al
	andb	%al, %ah			# ignore all `RAM drive'

	movb	$0x30, %al

get_fd_info:
	shrb	%ah
	pushw	%ax
	jnc	1f
	movb	$0xc4, %ah
	int	$0x1b
	movb	%ah, %al
	andb	$4, %al				# 1.44MB support flag
	shrb	%al
	addb	$2, %al				# %al = 2 (1.2MB) or 4 (1.44MB)
	jmp	2f
1:	movb	$0, %al				# no drive
2:	stosb
	popw	%ax
	incb	%al
	testb	$0x04, %al
	jz	get_fd_info

	addb	$(0xb0 - 0x34), %al
	jnc	get_fd_info			# check FDs on 640KB I/F

	pushw	%es
	popw	%ds				# %ds got bootsector again
#if 0
	mov	$0, (0x1ff)			# default is no pointing device
#endif

#if defined(CONFIG_APM) || defined(CONFIG_APM_MODULE)
# Then check for an APM BIOS...
						# %ds points to the bootsector
	movw	$0, 0x40			# version = 0 means no APM BIOS
	movw	$0x09a00, %ax			# APM BIOS installation check
	xorw	%bx, %bx
	int	$0x1f
	jc	done_apm_bios			# Nope, no APM BIOS

	cmpw	$0x0504d, %bx			# Check for "PM" signature
	jne	done_apm_bios			# No signature, no APM BIOS

	testb	$0x02, %cl			# Is 32 bit supported?
	je	done_apm_bios			# No 32-bit, no (good) APM BIOS

	movw	$0x09a04, %ax			# Disconnect first just in case
	xorw	%bx, %bx
	int	$0x1f				# ignore return code
	movw	$0x09a03, %ax			# 32 bit connect
	xorl	%ebx, %ebx
	int	$0x1f
	jc	no_32_apm_bios			# Ack, error.

	movw	%ax,  (66)			# BIOS code segment
	movl	%ebx, (68)			# BIOS entry point offset
	movw	%cx,  (72)			# BIOS 16 bit code segment
	movw	%dx,  (74)			# BIOS data segment
	movl	%esi, (78)			# BIOS code segment length
	movw	%di,  (82)			# BIOS data segment length
# Redo the installation check as the 32 bit connect
# modifies the flags returned on some BIOSs
	movw	$0x09a00, %ax			# APM BIOS installation check
	xorw	%bx, %bx
	int	$0x1f
	jc	apm_disconnect			# error -> shouldn't happen

	cmpw	$0x0504d, %bx			# check for "PM" signature
	jne	apm_disconnect			# no sig -> shouldn't happen

	movw	%ax, (64)			# record the APM BIOS version
	movw	%cx, (76)			# and flags
	jmp	done_apm_bios

apm_disconnect:					# Tidy up
	movw	$0x09a04, %ax			# Disconnect
	xorw	%bx, %bx
	int	$0x1f				# ignore return code

	jmp	done_apm_bios

no_32_apm_bios:
	andw	$0xfffd, (76)			# remove 32 bit support bit
done_apm_bios:
#endif

# Pass cursor position to kernel...
	movw	%cs:cursor_address, %ax
	shrw	%ax		# cursor_address is 2 bytes unit
	movb	$80, %cl
	divb	%cl
	xchgb	%al, %ah	# (0) = %al = X, (1) = %ah = Y
	movw	%ax, (0)

#if 0
	movw	$msg_cpos, %si
	call	prtstr_cs
	call	prthex
	call	prtstr_cs
	movw	%ds, %ax
	call	prthex
	call	prtstr_cs
	movb	$0x11, %ah
	int	$0x18
	movb	$0, %ah
	int	$0x18
	.section .rodata, "a"
msg_cpos:	.string	"Cursor position: 0x"
		.string	", %ds:0x"
		.string	"\r\n"
	.previous
#endif

# Now we want to move to protected mode ...
	cmpw	$0, %cs:realmode_swtch
	jz	rmodeswtch_normal

	lcall	*%cs:realmode_swtch

	jmp	rmodeswtch_end

rmodeswtch_normal:
        pushw	%cs
	call	default_switch

rmodeswtch_end:
# we get the code32 start address and modify the below 'jmpi'
# (loader may have changed it)
	movl	%cs:code32_start, %eax
	movl	%eax, %cs:code32

# Now we move the system to its rightful place ... but we check if we have a
# big-kernel. In that case we *must* not move it ...
	testb	$LOADED_HIGH, %cs:loadflags
	jz	do_move0			# .. then we have a normal low
						# loaded zImage
						# .. or else we have a high
						# loaded bzImage
	jmp	end_move			# ... and we skip moving

do_move0:
	movw	$0x100, %ax			# start of destination segment
	movw	%cs, %bp			# aka SETUPSEG
	subw	$DELTA_INITSEG, %bp		# aka INITSEG
	movw	%cs:start_sys_seg, %bx		# start of source segment
	cld
do_move:
	movw	%ax, %es			# destination segment
	incb	%ah				# instead of add ax,#0x100
	movw	%bx, %ds			# source segment
	addw	$0x100, %bx
	subw	%di, %di
	subw	%si, %si
	movw 	$0x800, %cx
	rep
	movsw
	cmpw	%bp, %bx			# assume start_sys_seg > 0x200,
						# so we will perhaps read one
						# page more than needed, but
						# never overwrite INITSEG
						# because destination is a
						# minimum one page below source
	jb	do_move

end_move:
# then we load the segment descriptors
	movw	%cs, %ax			# aka SETUPSEG
	movw	%ax, %ds
               
# Check whether we need to be downward compatible with version <=201
	cmpl	$0, cmd_line_ptr
	jne	end_move_self		# loader uses version >=202 features
	cmpb	$0x20, type_of_loader
	je	end_move_self		# bootsect loader, we know of it
 
# Boot loader does not support boot protocol version 2.02.
# If we have our code not at 0x90000, we need to move it there now.
# We also then need to move the params behind it (commandline)
# Because we would overwrite the code on the current IP, we move
# it in two steps, jumping high after the first one.
	movw	%cs, %ax
	cmpw	$SETUPSEG, %ax
	je	end_move_self

	cli					# make sure we really have
						# interrupts disabled !
						# because after this the stack
						# should not be used
	subw	$DELTA_INITSEG, %ax		# aka INITSEG
	movw	%ss, %dx
	cmpw	%ax, %dx
	jb	move_self_1

	addw	$INITSEG, %dx
	subw	%ax, %dx			# this will go into %ss after
						# the move
move_self_1:
	movw	%ax, %ds
	movw	$INITSEG, %ax			# real INITSEG
	movw	%ax, %es
	movw	%cs:setup_move_size, %cx
	std					# we have to move up, so we use
						# direction down because the
						# areas may overlap
	movw	%cx, %di
	decw	%di
	movw	%di, %si
	subw	$move_self_here+0x200, %cx
	rep
	movsb
	ljmp	$SETUPSEG, $move_self_here

move_self_here:
	movw	$move_self_here+0x200, %cx
	rep
	movsb
	movw	$SETUPSEG, %ax
	movw	%ax, %ds
	movw	%dx, %ss

end_move_self:					# now we are at the right place
	lidt	idt_48				# load idt with 0,0
	xorl	%eax, %eax			# Compute gdt_base
	movw	%ds, %ax			# (Convert %ds:gdt to a linear ptr)
	shll	$4, %eax
	addl	$gdt, %eax
	movl	%eax, (gdt_48+2)
	lgdt	gdt_48				# load gdt with whatever is
						# appropriate

# that was painless, now we enable A20

	outb	%al, $0xf2			# A20 on
	movb	$0x02, %al
	outb	%al, $0xf6			# also A20 on; making ITF's
						# way our model

	# PC-9800 seems to enable A20 at the moment of `outb';
	# so we don't wait unlike IBM PCs (see ../setup.S).

# enable DMA to access memory over 0x100000 (1MB).

	movw	$0x439, %dx
	inb	%dx, %al
	andb	$(~4), %al
	outb	%al, %dx

# Set DMA to increment its bank address automatically at 16MB boundary.
# Initial setting is 64KB boundary mode so that we can't run DMA crossing
# physical address 0xXXXXFFFF.

	movb	$0x0c, %al
	outb	%al, $0x29			# ch. 0
	movb	$0x0d, %al
	outb	%al, $0x29			# ch. 1
	movb	$0x0e, %al
	outb	%al, $0x29			# ch. 2
	movb	$0x0f, %al
	outb	%al, $0x29			# ch. 3
	movb	$0x50, %al
	outb	%al, $0x11			# reinitialize DMAC

# make sure any possible coprocessor is properly reset..
	movb	$0, %al
	outb	%al, $0xf8
	outb	%al, $0x5f			# delay

# well, that went ok, I hope. Now we mask all interrupts - the rest
# is done in init_IRQ().
	movb	$0xFF, %al			# mask all interrupts for now
	outb	%al, $0x0A
	outb	%al, $0x5f			# delay
	
	movb	$0x7F, %al			# mask all irq's but irq7 which
	outb	%al, $0x02			# is cascaded

# Well, that certainly wasn't fun :-(. Hopefully it works, and we don't
# need no steenking BIOS anyway (except for the initial loading :-).
# The BIOS-routine wants lots of unnecessary data, and it's less
# "interesting" anyway. This is how REAL programmers do it.
#
# Well, now's the time to actually move into protected mode. To make
# things as simple as possible, we do no register set-up or anything,
# we let the gnu-compiled 32-bit programs do that. We just jump to
# absolute address 0x1000 (or the loader supplied one),
# in 32-bit protected mode.
#
# Note that the short jump isn't strictly needed, although there are
# reasons why it might be a good idea. It won't hurt in any case.
	movw	$1, %ax				# protected mode (PE) bit
	lmsw	%ax				# This is it!
	jmp	flush_instr

flush_instr:
	xorw	%bx, %bx			# Flag to indicate a boot
	xorl	%esi, %esi			# Pointer to real-mode code
	movw	%cs, %si
	subw	$DELTA_INITSEG, %si
	shll	$4, %esi			# Convert to 32-bit pointer
# NOTE: For high loaded big kernels we need a
#	jmpi    0x100000,__BOOT_CS
#
#	but we yet haven't reloaded the CS register, so the default size 
#	of the target offset still is 16 bit.
#       However, using an operand prefix (0x66), the CPU will properly
#	take our 48 bit far pointer. (INTeL 80386 Programmer's Reference
#	Manual, Mixing 16-bit and 32-bit code, page 16-6)

	.byte 0x66, 0xea			# prefix + jmpi-opcode
code32:	.long	0x1000				# will be set to 0x100000
						# for big kernels
	.word	__BOOT_CS

# Here's a bunch of information about your current kernel..
kernel_version:	.ascii	UTS_RELEASE
		.ascii	" ("
		.ascii	LINUX_COMPILE_BY
		.ascii	"@"
		.ascii	LINUX_COMPILE_HOST
		.ascii	") "
		.ascii	UTS_VERSION
		.byte	0

# This is the default real mode switch routine.
# to be called just before protected mode transition
default_switch:
	cli					# no interrupts allowed !
	outb	%al, $0x50			# disable NMI for bootup
						# sequence
	lret

# This routine only gets called, if we get loaded by the simple
# bootsect loader _and_ have a bzImage to load.
# Because there is no place left in the 512 bytes of the boot sector,
# we must emigrate to code space here.
bootsect_helper:
	cmpw	$0, %cs:bootsect_es
	jnz	bootsect_second

	movb	$0x20, %cs:type_of_loader
	movw	%es, %ax
	shrw	$4, %ax
	movb	%ah, %cs:bootsect_src_base+2
	movw	%es, %ax
	movw	%ax, %cs:bootsect_es
	subw	$SYSSEG, %ax
	lret					# nothing else to do for now

bootsect_second:
	pushw	%bx
	pushw	%cx
	pushw	%si
	pushw	%di
	testw	%bp, %bp			# 64K full ?
	jne	bootsect_ex

	xorw	%cx, %cx			# zero means full 64K
	pushw	%cs
	popw	%es
	movw	$bootsect_gdt, %bx
	xorw	%si, %si			# source address
	xorw	%di, %di			# destination address
	movb	$0x90, %ah
	int	$0x1f
	jc	bootsect_panic			# this, if INT1F fails

	movw	%cs:bootsect_es, %es		# we reset %es to always point
	incb	%cs:bootsect_dst_base+2		# to 0x10000
bootsect_ex:
	movb	%cs:bootsect_dst_base+2, %ah
	shlb	$4, %ah				# we now have the number of
						# moved frames in %ax
	xorb	%al, %al
	popw	%di
	popw	%si
	popw	%cx
	popw	%bx
	lret

bootsect_gdt:
	.word	0, 0, 0, 0
	.word	0, 0, 0, 0

bootsect_src:
	.word	0xffff

bootsect_src_base:
	.byte	0x00, 0x00, 0x01		# base = 0x010000
	.byte	0x93				# typbyte
	.word	0				# limit16,base24 =0

bootsect_dst:
	.word	0xffff

bootsect_dst_base:
	.byte	0x00, 0x00, 0x10		# base = 0x100000
	.byte	0x93				# typbyte
	.word	0				# limit16,base24 =0
	.word	0, 0, 0, 0			# BIOS CS
	.word	0, 0, 0, 0			# BIOS DS

bootsect_es:
	.word	0

bootsect_panic:
	pushw	%cs
	popw	%ds
	cld
	leaw	bootsect_panic_mess, %si
	call	prtstr

bootsect_panic_loop:
	jmp	bootsect_panic_loop

bootsect_panic_mess:
	.string	"INT1F refuses to access high mem, giving up."

# This routine prints one character (in %al) on console.
# PC-9800 doesn't have BIOS-function to do it like IBM PC's INT 10h - 0Eh,
# so we hardcode `prtchr' subroutine here.
prtchr:
	pushaw
	pushw	%es
	cmpb	$0, %cs:prtchr_initialized
	jnz	prtchr_ok
	xorw	%cx, %cx
	movw	%cx, %es
	testb	$0x8, %es:BIOS_FLAG
	jz	1f
	movb	$(HIRESO_TEXT >> 8), %cs:cursor_address+3
	movw	$(80 * 31 * 2), %cs:max_cursor_offset
1:	pushw	%ax
	call	get_cursor_position
	movw	%ax, %cs:cursor_address
	popw	%ax
	movb	$1, %cs:prtchr_initialized
prtchr_ok:
	lesw	%cs:cursor_address, %di
	movw	$160, %bx
	movb	$0, %ah
	cmpb	$13, %al
	je	do_cr
	cmpb	$10, %al
	je	do_lf

	# normal (printable) character
	stosw
	movb	$0xe1, %es:0x2000-2(%di)
	jmp	1f

do_cr:	movw	%di, %ax
	divb	%bl				# %al = Y, %ah = X * 2
	mulb	%bl
	movw	%ax, %dx
	jmp	2f

do_lf:	addw	%bx, %di
1:	movw	%cs:max_cursor_offset, %cx
	cmpw	%cx, %di
	movw	%di, %dx
	jb	2f
	# cursor reaches bottom of screen; scroll it
	subw	%bx, %dx
	xorw	%di, %di
	movw	%bx, %si
	cld
	subw	%bx, %cx
	shrw	%cx
	pushw	%cx
	rep; es; movsw
	movb	$32, %al			# clear bottom line characters
	movb	$80, %cl
	rep; stosw
	movw	$0x2000, %di
	popw	%cx
	leaw	(%bx,%di), %si
	rep; es; movsw
	movb	$0xe1, %al			# clear bottom line attributes
	movb	$80, %cl
	rep; stosw
2:	movw	%dx, %cs:cursor_address
	movb	$0x13, %ah			# move cursor to right position
	int	$0x18
	popw	%es
	popaw
	ret

cursor_address:
	.word	0
	.word	NORMAL_TEXT
max_cursor_offset:
	.word	80 * 25 * 2			# for normal 80x25 mode

# putstr may called without running through start_of_setup (via bootsect_panic)
# so we should initialize ourselves on demand.
prtchr_initialized:
	.byte	0

# This routine queries GDC (graphic display controller) for current cursor
# position. Cursor position is returned in %ax (CPU offset address).
get_cursor_position:
1:	inb	$0x60, %al
	outb	%al, $0x5f			# delay
	outb	%al, $0x5f			# delay
	testb	$0x04, %al			# Is FIFO empty?
	jz	1b				# no -> wait until empty

	movb	$0xe0, %al			# CSRR command
	outb	%al, $0x62			# command write
	outb	%al, $0x5f			# delay
	outb	%al, $0x5f			# delay

2:	inb	$0x60, %al
	outb	%al, $0x5f			# delay
	outb	%al, $0x5f			# delay
	testb	$0x01, %al			# Is DATA READY?
	jz	2b				# no -> wait until ready

	inb	$0x62, %al			# read xAD (L)
	outb	%al, $0x5f			# delay
	outb	%al, $0x5f			# delay
	movb	%al, %ah
	inb	$0x62, %al			# read xAD (H)
	outb	%al, $0x5f			# delay
	outb	%al, $0x5f			# delay
	xchgb	%al, %ah			# correct byte order
	pushw	%ax
	inb	$0x62, %al			# read yAD (L)
	outb	%al, $0x5f			# delay
	outb	%al, $0x5f			# delay
	inb	$0x62, %al			# read yAD (M)
	outb	%al, $0x5f			# delay
	outb	%al, $0x5f			# delay
	inb	$0x62, %al			# read yAD (H)
						# yAD is not our interest,
						# so discard it.
	popw	%ax
	addw	%ax, %ax			# convert to CPU address
	ret

# Descriptor tables
#
# NOTE: The intel manual says gdt should be sixteen bytes aligned for
# efficiency reasons.  However, there are machines which are known not
# to boot with misaligned GDTs, so alter this at your peril!  If you alter
# GDT_ENTRY_BOOT_CS (in asm/segment.h) remember to leave at least two
# empty GDT entries (one for NULL and one reserved).
#
# NOTE:	On some CPUs, the GDT must be 8 byte aligned.  This is
# true for the Voyager Quad CPU card which will not boot without
# This directive.  16 byte aligment is recommended by intel.
#
	.align 16
gdt:
	.fill GDT_ENTRY_BOOT_CS,8,0

	.word	0xFFFF				# 4Gb - (0x100000*0x1000 = 4Gb)
	.word	0				# base address = 0
	.word	0x9A00				# code read/exec
	.word	0x00CF				# granularity = 4096, 386
						#  (+5th nibble of limit)

	.word	0xFFFF				# 4Gb - (0x100000*0x1000 = 4Gb)
	.word	0				# base address = 0
	.word	0x9200				# data read/write
	.word	0x00CF				# granularity = 4096, 386
						#  (+5th nibble of limit)
gdt_end:
	.align	4
	
	.word	0				# alignment byte
idt_48:
	.word	0				# idt limit = 0
	.word	0, 0				# idt base = 0L

	.word	0				# alignment byte
gdt_48:
	.word	gdt_end - gdt - 1		# gdt limit
	.word	0, 0				# gdt base (filled in later)

# Include video setup & detection code

#include "video.S"

# Setup signature -- must be last
setup_sig1:	.word	SIG1
setup_sig2:	.word	SIG2

# After this point, there is some free space which is used by the video mode
# handling code to store the temporary mode table (not used by the kernel).

modelist:

.text
endtext:
.data
enddata:
.bss
endbss:
