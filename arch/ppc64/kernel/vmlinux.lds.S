#include <asm-generic/vmlinux.lds.h>

OUTPUT_ARCH(powerpc:common64)
/* Do we need any of these for elf?
   __DYNAMIC = 0;    */
jiffies = jiffies_64;
SECTIONS
{
  /* Read-only sections, merged into text segment: */
  . = + SIZEOF_HEADERS;
  .interp : { *(.interp) }
  .hash          : { *(.hash)		}
  .dynsym        : { *(.dynsym)		}
  .dynstr        : { *(.dynstr)		}
  .rel.text      : { *(.rel.text)		}
  .rela.text     : { *(.rela.text) 	}
  .rel.data      : { *(.rel.data)		}
  .rela.data     : { *(.rela.data) 	}
  .rel.rodata    : { *(.rel.rodata) 	}
  .rela.rodata   : { *(.rela.rodata) 	}
  .rel.got       : { *(.rel.got)		}
  .rela.got      : { *(.rela.got)		}
  .rel.ctors     : { *(.rel.ctors)	}
  .rela.ctors    : { *(.rela.ctors)	}
  .rel.dtors     : { *(.rel.dtors)	}
  .rela.dtors    : { *(.rela.dtors)	}
  .rel.bss       : { *(.rel.bss)		}
  .rela.bss      : { *(.rela.bss)		}
  .rel.plt       : { *(.rel.plt)		}
  .rela.plt      : { *(.rela.plt)		}
/*  .init          : { *(.init)	} =0*/
  .plt : { *(.plt) }
  .text      :
  {
    *(.text)
    *(.fixup)
    *(.got1)
  }
  . = ALIGN(4096);
  _etext = .;
  PROVIDE (etext = .);

  RODATA

  .fini      : { *(.fini)    } =0
  .ctors     : { *(.ctors)   }
  .dtors     : { *(.dtors)   }
  /* Read-write section, merged into data segment: */
  . = (. + 0x0FFF) & 0xFFFFFFFFFFFFF000;
  .data    :
  {
    *(.data)
    *(.data1)
    *(.sdata)
    *(.sdata2)
    *(.got.plt) *(.got)
    *(.dynamic)
    CONSTRUCTORS
  }
  . = ALIGN(4096);
  _edata  =  .;
  PROVIDE (edata = .);

  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  __start___bug_table = .;
  __bug_table : { *(__bug_table) }
  __stop___bug_table = .;

  __start___ftr_fixup = .;
  __ftr_fixup : { *(__ftr_fixup) }
  __stop___ftr_fixup = .;

  . = ALIGN(16384);		/* init_task */
  .data.init_task : { *(.data.init_task) }

  . = ALIGN(4096);
  .data.page_aligned : { *(.data.page_aligned) }

  . = ALIGN(128);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  /* will be freed after init */
  . = ALIGN(4096);
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
  __start___param = .;
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
  /* freed after init ends here */

  __toc_start = .;
  .toc : { *(.toc) }
  . = ALIGN(4096);
  __toc_end = .;

  __bss_start = .;
  .bss : { *(.bss) }
  __bss_stop = .;

  . = ALIGN(4096);
  _end = . ;
  PROVIDE (end = .);

  /* Sections to be discarded. */
  /DISCARD/ : {
    *(.exitcall.exit)
  }
}
