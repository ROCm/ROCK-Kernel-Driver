OUTPUT_ARCH(m68k)
ENTRY(_start)
MEMORY {
	romvec : ORIGIN = 0x10c00000, LENGTH = 0x10400
	flash  : ORIGIN = 0x10c10400, LENGTH = 0xfec00
	eflash : ORIGIN = 0x10d00000, LENGTH = 4
	ramvec : ORIGIN = 0x00000000, LENGTH = 0x400
	ram    : ORIGIN = 0x10000400, LENGTH = 0x100000-0x400
	eram   : ORIGIN = 0x10100000, LENGTH = 4
}

jiffies = jiffies_64 + 4;

SECTIONS {
  . = 0x10c00000 ;
  .romvec : {
	  _flashstart = . ;
	  _romvec = . ;
	} > romvec

  . = 0x10c10400 ;
  .text : {
	  _text = .;                    /* Text and read-only data */
	  text_start = . ;
          *(.text)
          *(.text.*)
          *(.rodata)
          *(.fixup)		
          *(__ex_table)	
	  . = ALIGN(4) ;
	  _etext = . ;
	  __data_rom_start = . ;
	} > flash
  
  . = 0x10d00000 ;
  .eflash :
        {
          _flashend = . ;
        } > eflash
	
  . = 0 ;
  .ramvec :
	{
	  __ramvec = . ;
	} > ramvec
	
  /* . = 0x10000400 ; */
  .data 0x10000400 :
	{
	  _sdata = . ;
	  __data_start = . ;

	  . = ALIGN(0x2000) ;
	  *(.data.init_task)
	  . = ALIGN(0x2000) ;

      *(.data)
      *(.data.*)	
      *(.setup.init)		
      *(.exitcall.exit)		
	
	  . = ALIGN(4096) ;
	  __init_begin = .;
	  *(.init.text)
	  *(.init.data)
	  . = ALIGN(16);
	  __setup_start = .;
	  *(.init.setup)
	  __setup_end = .;
	  __initcall_start = .;
	  *(.initcall1.init)
	  *(.initcall2.init)
	  *(.initcall3.init)
	  *(.initcall4.init)
	  *(.initcall5.init)
	  *(.initcall6.init)
	  *(.initcall7.init)
	  __initcall_end = .;
	  . = ALIGN(4) ;
	  __init_end = .;

	  _edata = . ;
	  edata = . ;
	} > ram

  .bss :
    {
	  . = ALIGN(16) ;
	  _sbss = . ;
	  __bss_start = . ;
	  __data_end = . ;
	  *(.bss)
	  *(COMMON)
	  . = ALIGN(16) ;
	  end = . ;
	  _ebss = . ;	  
	  _end = . ;
    } > ram

  . = 0x10100000 ;
  .eram :
	{
  	  __ramend = . ;
	} > eram
}
