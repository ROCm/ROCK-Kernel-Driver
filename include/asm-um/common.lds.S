  .fini      : { *(.fini)    } =0x9090
  _etext = .;
  PROVIDE (etext = .);

  . = ALIGN(4096);
  _sdata = .;
  PROVIDE (sdata = .);

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  RODATA

  __start___ksymtab = .;	/* Kernel symbol table */
  __ksymtab : { *(__ksymtab) }
  __stop___ksymtab = .;

  __start___gpl_ksymtab = .;	/* Kernel symbol table:	GPL-only symbols */
  __gpl_ksymtab : { *(__gpl_ksymtab) }
  __stop___gpl_ksymtab = .;

  __start___kallsyms = .;       /* All kernel symbols */
  __kallsyms : { *(__kallsyms) }
  __stop___kallsyms = .;

  .unprotected : { *(.unprotected) }
  . = ALIGN(4096);
  PROVIDE (_unprotected_end = .);

  . = ALIGN(4096);
  __uml_setup_start = .;
  .uml.setup.init : { *(.uml.setup.init) }
  __uml_setup_end = .;
	
  __uml_help_start = .;
  .uml.help.init : { *(.uml.help.init) }
  __uml_help_end = .;
	
  __uml_postsetup_start = .;
  .uml.postsetup.init : { *(.uml.postsetup.init) }
  __uml_postsetup_end = .;
	
  __setup_start = .;
  .init.setup : { *(.init.setup) }
  __setup_end = .;

  __start___param = .;
  __param : { *(__param) }
  __stop___param = .;

  . = ALIGN(32);
  __per_cpu_start = . ; 
  .data.percpu : { *(.data.percpu) }
  __per_cpu_end = . ;
	
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

  __uml_initcall_start = .;
  .uml.initcall.init : { *(.uml.initcall.init) }
  __uml_initcall_end = .;
  __init_end = .;

  __exitcall_begin = .;
  .exitcall : { *(.exitcall.exit) }
  __exitcall_end = .;

  __uml_exitcall_begin = .;
  .uml.exitcall : { *(.uml.exitcall.exit) }
  __uml_exitcall_end = .;

  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;
