#define VMLINUX_SYMBOL(_sym_) _##_sym_
#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <linux/config.h>

#ifdef CONFIG_H8300H_GENERIC
#ifdef CONFIG_ROMKERNEL
#include "../platform/h8300h/generic/rom.ld"
#endif
#ifdef CONFIG_RAMKERNEL
#include "../platform/h8300h/generic/ram.ld"
#endif
#endif

#ifdef CONFIG_H8300H_AKI3068NET
#ifdef CONFIG_ROMKERNEL
#include "../platform/h8300h/aki3068net/rom.ld"
#endif
#ifdef CONFIG_RAMKERNEL
#include "../platform/h8300h/aki3068net/ram.ld"
#endif
#endif

#ifdef CONFIG_H8300H_H8MAX
#ifdef CONFIG_ROMKERNEL
#include "../platform/h8300h/h8max/rom.ld"
#endif
#ifdef CONFIG_RAMKERNEL
#include "../platform/h8300h/h8max/ram.ld"
#endif
#endif

#ifdef CONFIG_H8300H_SIM
#ifdef CONFIG_ROMKERNEL
#include "../platform/h8300h/generic/rom.ld"
#endif
#ifdef CONFIG_RAMKERNEL
#include "../platform/h8300h/generic/ram.ld"
#endif
#endif

#ifdef CONFIG_H8S_SIM
#ifdef CONFIG_ROMKERNEL
#include "../platform/h8s/generic/rom.ld"
#endif
#ifdef CONFIG_RAMKERNEL
#include "../platform/h8s/generic/ram.ld"
#endif
#endif

#ifdef CONFIG_H8S_EDOSK2674
#ifdef CONFIG_ROMKERNEL
#include "../platform/h8s/edosk2674/rom.ld"
#endif
#ifdef CONFIG_RAMKERNEL
#include "../platform/h8s/edosk2674/ram.ld"
#endif
#endif

#if defined(CONFIG_H8300H_SIM) || defined(CONFIG_H8S_SIM)
INPUT(romfs.o)
#endif

_jiffies = _jiffies_64 + 4;

SECTIONS
{
#if defined(CONFIG_ROMKERNEL)
	.vectors :
	{
	__vector = . ;
		*(.vectors*)
	} > vector
#endif
#if defined(CONFIG_RAMKERNEL)
	.bootvec :
	{
		*(.bootvec)
	} > ram
#endif
        .text :
	{
#if defined(CONFIG_ROMKERNEL)
		*(.int_redirect)
#endif
	__stext = . ;
        	*(.text)
	SCHED_TEXT
	. = ALIGN(0x4) ;
		*(.exit.text)
		*(.text.*)
	. = ALIGN(0x4) ;
		*(.exitcall.exit)
	. = ALIGN(0x4) ;
		*(.kstrtab)
	. = ALIGN(0x4) ;
		*(.rodata*)
	. = ALIGN(16);          /* Exception table              */
	___start___ex_table = .;
		*(__ex_table)
	___stop___ex_table = .;

        ___start___ksymtab = .;  /* Kernel symbol table          */
		 *(__ksymtab)
	___stop___ksymtab = .;

	___start___ksymtab_gpl = .;	/* Kernel symbol table: GPL-only symbols */

	*(__ksymtab_gpl)
	___stop___ksymtab_gpl = .;

	___start___kcrctab = .;	/* Kernel symbol table: Normal symbols */
	*(__kcrctab)
	___stop___kcrctab = .;

	___start___kcrctab_gpl = .;	/* Kernel symbol table: GPL-only symbols */
	*(__kcrctab_gpl)
	___stop___kcrctab_gpl = .;

	*(__ksymtab_strings)	/* Kernel symbol table: strings */

	. = ALIGN(0x4) ;
	__etext = . ;
#if defined(CONFIG_ROMKERNEL)
	} > rom
#endif
#if defined(CONFIG_RAMKERNEL)
	} > ram
#endif
	.data : AT( ADDR(.text)+SIZEOF(.text))
	{
	__sdata = . ;
	___data_start = . ;

	. = ALIGN(0x2000) ;
		*(.data.init_task)
	. = ALIGN(0x4) ;
		*(.data)
	. = ALIGN(0x4) ;
		*(.data.*)	

	. = ALIGN(0x4) ;
	___init_begin = .;
	__sinittext = .; 
		*(.init.text)
	__einittext = .; 
		*(.init.data)
	. = ALIGN(0x4) ;
	___setup_start = .;
		*(.init.setup)
	. = ALIGN(0x4) ;
	___setup_end = .;
	___start___param = .;
		*(__param)
	___stop___param = .;
	___initcall_start = .;
		*(.initcall1.init)
		*(.initcall2.init)
		*(.initcall3.init)
		*(.initcall4.init)
		*(.initcall5.init)
		*(.initcall6.init)
		*(.initcall7.init)
	___initcall_end = .;
	___con_initcall_start = .;
		*(.con_initcall.init)
	___con_initcall_end = .;
		. = ALIGN(4);
	___initramfs_start = .;
  		*(.init.ramfs)
  	___initramfs_end = .;
	. = ALIGN(0x4) ;
	___init_end = .;
	__edata = . ;
	} > ram
	__begin_data = LOADADDR(.data) ;
#if defined(CONFIG_ROMKERNEL)
	.erom :
	{
	__erom = . ;
	} > erom
#endif
        .bss : 
        {
	. = ALIGN(0x4) ;
	__sbss = . ;
		*(.bss*)
	. = ALIGN(0x4) ;
		*(COMMON)
	. = ALIGN(0x4) ;
	__ebss = . ;
	__end = . ;
	__ramstart = .;
	} > ram
        .romfs :	
	{
		*(.romfs*)
	} > ram
        .dummy :
        {
	COMMAND_START = . - 0x200 ;
	__ramend = . ;
	} > eram
}

