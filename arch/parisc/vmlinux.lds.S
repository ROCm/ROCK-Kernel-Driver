#include <linux/config.h>
#include <asm-generic/vmlinux.lds.h>
	
/* ld script to make hppa Linux kernel */
#ifndef CONFIG_PARISC64
OUTPUT_FORMAT("elf32-hppa-linux")
OUTPUT_ARCH(hppa)
#else
OUTPUT_FORMAT("elf64-hppa-linux")
OUTPUT_ARCH(hppa:hppa2.0w)
#endif

ENTRY(_stext)
#ifndef CONFIG_PARISC64
jiffies = jiffies_64 + 4;
#else
jiffies = jiffies_64;
#endif
SECTIONS
{

  . = 0x10100000;

  _text = .;			/* Text and read-only data */
  .text BLOCK(16) : {
	*(.text*)
	*(.PARISC.unwind)
	*(.fixup)
	*(.lock.text)		/* out-of-line lock text */
	*(.gnu.warning)
	} = 0

  _etext = .;			/* End of text section */

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  RODATA

  .data BLOCK(8192) : {			/* Data without special */
	data_start = .;
	*(.data)
	}

#ifdef CONFIG_PARISC64
  . = ALIGN(16);               /* Linkage tables */
  .opd : { *(.opd) } PROVIDE (__gp = .); 
  .plt : { *(.plt) } 
  .dlt : { *(.dlt) }
#endif

  . = ALIGN(16384);
  __init_begin = .;
  .init.text : { 
	_sinittext = .;
	*(.init.text)
	_einittext = .;
  }
  .init.data : { *(.init.data) }
  . = ALIGN(16);
  __setup_start = .;
  .init.setup : { *(.init.setup) }
  __setup_end = .;
  __start___param =.; 
  __param : { *(__param) }
  __stop___param = .;
  __initcall_start = .;
  .initcall.init : {
	*(.initcall1.init) 
	*(.initcall2.init) 
	*(.initcall3.init) 
	*(.initcall4.init) 
	*(.initcall5.init) 
	*(.initcall6.init) 
	*(.initcall7.init)
  }
  __initcall_end = .;
  __con_initcall_start = .;
  .con_initcall.init : { *(.con_initcall.init) }
  __con_initcall_end = .;
  SECURITY_INIT
  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;
  . = ALIGN(32);
  __per_cpu_start = .;
  .data.percpu  : { *(.data.percpu) }
  __per_cpu_end = .;
  . = ALIGN(4096);
  __init_end = .;

  init_task BLOCK(16384) : { *(init_task) }  /* The initial task and kernel stack */

  _edata = .;			/* End of data section */


  .bss : { *(.bss) *(COMMON) }		/* BSS */


  _end = . ;

  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
  .note 0 : { *(.note) }	

#ifdef CONFIG_PARISC64
  /* temporary hack until binutils is fixed to not emit these
     for static binaries */
  /DISCARD/ : {
    *(.dynsym)
    *(.dynstr)
    *(.dynamic)
    *(.hash)
  }
#endif
}
