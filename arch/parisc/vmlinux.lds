/* ld script to make hppa Linux kernel */
OUTPUT_FORMAT("elf32-hppa")
OUTPUT_ARCH(hppa)
ENTRY(_stext)
SECTIONS
{

/* right now use 0x10000/0x11000, later when we don't use Console and
 * Boot-Device IODC, we will change this to 0x8000 !!!
 */

  . = 0xc0100000;   
/*	. = 0x10000;  */

  _text = .;			/* Text and read-only data */
  .text BLOCK(16) : {
	*(.text*)
	*(.PARISC.unwind)
	*(.fixup)
	*(.lock.text)		/* out-of-line lock text */
	*(.gnu.warning)
	} = 0

  . = ALIGN(16);
  .rodata : { *(.rodata) }
  .kstrtab : { *(.kstrtab) }

  _etext = .;			/* End of text section */
  
  .data BLOCK(8192) : {			/* Data without special */
	data_start = .;
	*(.data)
	}

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  __start___ksymtab = .;	/* Kernel symbol table */
  __ksymtab : { *(__ksymtab) }
  __stop___ksymtab = .;


  . = ALIGN(16384);
  __init_begin = .;
  .init.text : { *(.init.text) }
  .init.data : { *(.init.data) }
  . = ALIGN(16);
  __setup_start = .;
  .setup.init : { *(.setup.init) }
  __setup_end = .;
  __initcall_start = .;
  .initcall.init : { *(.initcall.init) } 
  __initcall_end = .;
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
	
}
