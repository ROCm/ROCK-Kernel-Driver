#include <linux/config.h>

OUTPUT_ARCH(m32r)
ENTRY(startup)
SECTIONS
{
  . = CONFIG_MEMORY_START + 0x00400000;

  _text = .;
  .text : { *(.text) } = 0
  .rodata : { *(.rodata) }
  _etext = .;

  . = ALIGN(32) + (. & (32 - 1));
  .data : { *(.data) }
  _edata  =  .;

  . = ALIGN(32 / 8);
  __bss_start = .;
  .bss : { *(.bss) }
  . = ALIGN(32 / 8);
  _end = . ;
}
