/*
 * Linker script for gate DSO.  The gate pages are an ELF shared
 * object prelinked to its virtual address, with only one read-only
 * segment and one execute-only segment (both fit in one page).
 * This script controls its layout.
 */

#include <linux/config.h>

#include <asm/system.h>

SECTIONS
{
  . = GATE_ADDR + SIZEOF_HEADERS;

  .hash				: { *(.hash) }					:rodata
  .dynsym			: { *(.dynsym) }
  .dynstr			: { *(.dynstr) }
  .gnu.version			: { *(.gnu.version) }
  .gnu.version_d		: { *(.gnu.version_d) }
  .gnu.version_r		: { *(.gnu.version_r) }
  .rela.dyn			: { *(.rela*) }
  .dynamic			: { *(.dynamic) }				:rodata :dynamic
  .data.patch.mckinley_e9	: { *(.data.patch.mckinley_e9) }		:rodata
  .data.patch.fsyscall_table	: { *(.data.patch.fsyscall_table) }
  .data.patch.vtop		: { *(.data.patch.vtop) }
  .data.patch.brl_fsys_bubble_down : { *(.data.patch.brl_fsys_bubble_down) }
  .IA_64.unwind_info		: { *(.IA_64.unwind_info*) }
  .IA_64.unwind			: { *(.IA_64.unwind*) }				:rodata :unwind

  . = GATE_ADDR + PAGE_SIZE;

  .text				: { *(.text) }					:rodata

  .useless			: {
  	*(.got.plt) *(.got)
	*(.data .data.* .gnu.linkonce.d.*)
	*(.dynbss)
	*(.bss .bss.* .gnu.linkonce.b.*)
	*(__ex_table)
  }
}

/*
 * We must supply the ELF program headers explicitly to get just one
 * PT_LOAD segment, and set the flags explicitly to make segments read-only.
 */
PHDRS
{
  rodata	PT_LOAD	FILEHDR	PHDRS	FLAGS(4);	/* PF_R */
  dynamic	PT_DYNAMIC		FLAGS(4);	/* PF_R */
  unwind	0x70000001;				/* PT_IA_64_UNWIND */
}

/*
 * This controls what symbols we export from the DSO.
 */
VERSION
{
  LINUX_2.5 {
    global:
	__kernel_syscall_via_break;
	__kernel_syscall_via_epc;
	__kernel_sigtramp;

    local: *;
  };
}

/* The ELF entry point can be used to set the AT_SYSINFO value.  */
ENTRY(syscall_via_epc)
