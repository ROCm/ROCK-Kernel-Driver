
.text
#include <linux/linkage.h>
#include <asm/segment.h>

# Do we need to deal with A20?

ALIGN
wakeup_start:
wakeup_code:
	wakeup_code_start = .
	.code16

 	movw	$0xb800, %ax
	movw	%ax,%fs
	movw	$0x0e00 + 'L', %fs:(0x10)
	cli
	cld
	  
	# setup data segment
	movw	%cs, %ax

	addw	$(wakeup_data - wakeup_code) >> 4, %ax
	movw	%ax, %ds
	movw	%ax, %ss
	mov	$(wakeup_stack - wakeup_data), %sp		# Private stack is needed for ASUS board
	movw	$0x0e00 + 'S', %fs:(0x12)	

	movl	real_magic - wakeup_data, %eax
	cmpl	$0x12345678, %eax
	jne	bogus_real_magic

	mov	video_mode - wakeup_data, %ax
	call	mode_set

	# set up page table
	movl	(real_save_cr3 - wakeup_data), %eax
	movl	%eax, %cr3

	# make sure %cr4 is set correctly (features, etc)
	movl	(real_save_cr4 - wakeup_data), %eax
	movl	%eax, %cr4
	movw	$0xb800, %ax
	movw	%ax,%fs
	movw	$0x0e00 + 'i', %fs:(0x12)

	# need a gdt
	lgdt	real_save_gdt - wakeup_data

	movl	(real_save_cr0 - wakeup_data), %eax
	movl	%eax, %cr0
	movw	$0x0e00 + 'n', %fs:(0x14)

	movl	real_magic - wakeup_data, %eax
	cmpl	$0x12345678, %eax
	jne	bogus_real_magic

	ljmpl	$__KERNEL_CS,$wakeup_pmode_return

bogus_real_magic:
	movw	$0x0e00 + 'B', %fs:(0x12)
	jmp bogus_real_magic

/* This code uses an extended set of video mode numbers. These include:
 * Aliases for standard modes
 *	NORMAL_VGA (-1)
 *	EXTENDED_VGA (-2)
 *	ASK_VGA (-3)
 * Video modes numbered by menu position -- NOT RECOMMENDED because of lack
 * of compatibility when extending the table. These are between 0x00 and 0xff.
 */
#define VIDEO_FIRST_MENU 0x0000

/* Standard BIOS video modes (BIOS number + 0x0100) */
#define VIDEO_FIRST_BIOS 0x0100

/* VESA BIOS video modes (VESA number + 0x0200) */
#define VIDEO_FIRST_VESA 0x0200

/* Video7 special modes (BIOS number + 0x0900) */
#define VIDEO_FIRST_V7 0x0900

# Setting of user mode (AX=mode ID) => CF=success
mode_set:
	movw	%ax, %bx
#if 0
	cmpb	$0xff, %ah
	jz	setalias

	testb	$VIDEO_RECALC>>8, %ah
	jnz	_setrec

	cmpb	$VIDEO_FIRST_RESOLUTION>>8, %ah
	jnc	setres
	
	cmpb	$VIDEO_FIRST_SPECIAL>>8, %ah
	jz	setspc

	cmpb	$VIDEO_FIRST_V7>>8, %ah
	jz	setv7
#endif
	
	cmpb	$VIDEO_FIRST_VESA>>8, %ah
	jnc	check_vesa
#if 0	
	orb	%ah, %ah
	jz	setmenu
#endif
	
	decb	%ah
#	jz	setbios				  Add bios modes later

setbad:	clc
	ret

check_vesa:
	subb	$VIDEO_FIRST_VESA>>8, %bh
	orw	$0x4000, %bx			# Use linear frame buffer
	movw	$0x4f02, %ax			# VESA BIOS mode set call
	int	$0x10
	cmpw	$0x004f, %ax			# AL=4f if implemented
	jnz	_setbad				# AH=0 if OK

	stc
	ret

_setbad: jmp setbad

	.code32
	ALIGN

.org	0x300
wakeup_data:
		.word 0
real_save_gdt:	.word 0
		.long 0
real_save_cr0:	.long 0
real_save_cr3:	.long 0
real_save_cr4:	.long 0
real_magic:	.long 0
video_mode:	.long 0

.org	0x500
wakeup_stack:
wakeup_end:

wakeup_pmode_return:
	movl	$__KERNEL_DS, %eax
	movl	%eax, %ds
	movw	$0x0e00 + 'u', %ds:(0xb8016)

	# restore other segment registers
	xorl	%eax, %eax
	movw	%ax, %fs
	movw	%ax, %gs

	# reload the gdt, as we need the full 32 bit address
	lgdt	saved_gdt
	lidt	saved_idt
	lldt	saved_ldt
	ljmp	$(__KERNEL_CS),$1f
1:
	movl	%cr3, %eax
	movl	%eax, %cr3
	wbinvd

	# and restore the stack ... but you need gdt for this to work
	movl	$__KERNEL_DS, %eax
	movw	%ax, %ss
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movl	saved_esp, %esp

	movw	$0x0e00 + 'W', %ds:(0xb8018)
	movl	$(1024*1024*3), %ecx
	movl	$0, %esi
	rep	lodsb
	movw	$0x0e00 + 'O', %ds:(0xb8018)

	movl	%cs:saved_magic2, %eax
	cmpl	$0x12345678, %eax
	jne	bogus_magic

	# restore the other general registers
	movl	saved_ebx, %ebx
	movl	saved_edi, %edi
	movl	saved_esi, %esi
	movl	saved_ebp, %ebp

	# jump to place where we left off
	movl	saved_eip,%eax
	movw	$0x0e00 + 'x', %ds:(0xb8018)
	pushl	%eax
	popl	%eax
	movw	$0x0e00 + '!', %ds:(0xb801a)
	jmp	*%eax

bogus_magic:
	movw	$0x0e00 + 'B', %ds:(0xb8018)
	jmp bogus_magic

bogus_magic2:
	movw	$0x0e00 + '2', %ds:(0xb8018)
	jmp bogus_magic2
		

##
# acpi_copy_wakeup_routine
#
# Copy the above routine to low memory.
#
# Parameters:
# %eax:	place to copy wakeup routine to
#
# Returned address is location of code in low memory (past data and stack)
#
ENTRY(acpi_copy_wakeup_routine)

	pushl	%esi
	pushl	%edi

	sgdt	saved_gdt
	sidt	saved_idt
	sldt	saved_ldt
	str	saved_tss

	movl	%eax, %edi
	leal	wakeup_start, %esi
	movl	$(wakeup_end - wakeup_start) >> 2, %ecx

	rep ;  movsl

	movl    %cr3, %edx
	movl    %edx, real_save_cr3 - wakeup_start (%eax)
	movl    %cr4, %edx
	movl    %edx, real_save_cr4 - wakeup_start (%eax)
	movl	%cr0, %edx
	movl	%edx, real_save_cr0 - wakeup_start (%eax)
	sgdt    real_save_gdt - wakeup_start (%eax)

	movl	saved_videomode, %edx
	movl	%edx, video_mode - wakeup_start (%eax)
	movl	$0x12345678, real_magic - wakeup_start (%eax)
	movl	$0x12345678, saved_magic2

	# restore the regs we used
	popl	%edi
	popl	%esi
	ret

.data
ALIGN
ENTRY(saved_ebp)	.long	0
ENTRY(saved_esi)	.long	0
ENTRY(saved_edi)	.long	0
ENTRY(saved_ebx)	.long	0

ENTRY(saved_eip)	.long	0
ENTRY(saved_esp)	.long	0

ENTRY(saved_magic)	.long	0
ENTRY(saved_magic2)	.long	0	

ALIGN
# saved registers
saved_gdt:	.long	0,0
saved_idt:	.long	0,0
saved_ldt:	.long	0
saved_tss:	.long	0
saved_cr0:	.long	0

