/* ld script to make i386 Linux kernel
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>;
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <linux/config.h>
#include <asm/vsyscall-gtod.h>

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(startup_32)
SECTIONS
{
  . = 0xC0000000 + 0x100000;
  /* read-only */
  _text = .;			/* Text and read-only data */
  .text : {
	*(.text)
	*(.fixup)
	*(.gnu.warning)
	} = 0x9090

  _etext = .;			/* End of text section */

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  RODATA

  /* writeable */
  .data : {			/* Data */
	*(.data)
	CONSTRUCTORS
	}

  . = ALIGN(4096);
  __nosave_begin = .;
  .data_nosave : { *(.data.nosave) }
  . = ALIGN(4096);
  __nosave_end = .;

  . = ALIGN(4096);
  .data.page_aligned : { *(.data.idt) }

  . = ALIGN(32);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  _edata = .;			/* End of data section */

/* VSYSCALL_GTOD data */
#ifdef CONFIG_VSYSCALL_GTOD

	/* vsyscall entry */
   . = ALIGN(64);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  .vsyscall_0 VSYSCALL_GTOD_START: AT ((LOADADDR(.data.cacheline_aligned) + SIZEOF(.data.cacheline_aligned) + 4095) & ~(4095)) { *(.vsyscall_0) }
  __vsyscall_0 = LOADADDR(.vsyscall_0);


	/* generic gtod variables */
  . = ALIGN(64);
  .vsyscall_timesource : AT ((LOADADDR(.vsyscall_0) + SIZEOF(.vsyscall_0) + 63) & ~(63)) { *(.vsyscall_timesource) }
  vsyscall_timesource = LOADADDR(.vsyscall_timesource);

    . = ALIGN(16);
  .xtime_lock : AT ((LOADADDR(.vsyscall_timesource) + SIZEOF(.vsyscall_timesource) + 15) & ~(15)) { *(.xtime_lock) }
  xtime_lock = LOADADDR(.xtime_lock);

  . = ALIGN(16);
  .xtime : AT ((LOADADDR(.xtime_lock) + SIZEOF(.xtime_lock) + 15) & ~(15)) { *(.xtime) }
  xtime = LOADADDR(.xtime);

  . = ALIGN(16);
  .jiffies : AT ((LOADADDR(.xtime) + SIZEOF(.xtime) + 15) & ~(15)) { *(.jiffies) }
  jiffies = LOADADDR(.jiffies);

  . = ALIGN(16);
  .wall_jiffies : AT ((LOADADDR(.jiffies) + SIZEOF(.jiffies) + 15) & ~(15)) { *(.wall_jiffies) }
  wall_jiffies = LOADADDR(.wall_jiffies);

  .sys_tz : AT (LOADADDR(.wall_jiffies) + SIZEOF(.wall_jiffies)) { *(.sys_tz) }
  sys_tz = LOADADDR(.sys_tz);

	/* NTP variables */
  .tickadj : AT (LOADADDR(.sys_tz) + SIZEOF(.sys_tz)) { *(.tickadj) }
  tickadj = LOADADDR(.tickadj);

  .time_adjust : AT (LOADADDR(.tickadj) + SIZEOF(.tickadj)) { *(.time_adjust) }
  time_adjust = LOADADDR(.time_adjust);

	/* TSC variables*/
  .last_tsc_low : AT (LOADADDR(.time_adjust) + SIZEOF(.time_adjust)) { *(.last_tsc_low) }
  last_tsc_low = LOADADDR(.last_tsc_low);

  .tsc_delay_at_last_interrupt : AT (LOADADDR(.last_tsc_low) + SIZEOF(.last_tsc_low)) { *(.tsc_delay_at_last_interrupt) }
  tsc_delay_at_last_interrupt = LOADADDR(.tsc_delay_at_last_interrupt);

  .fast_gettimeoffset_quotient : AT (LOADADDR(.tsc_delay_at_last_interrupt) + SIZEOF(.tsc_delay_at_last_interrupt)) { *(.fast_gettimeoffset_quotient) }
  fast_gettimeoffset_quotient = LOADADDR(.fast_gettimeoffset_quotient);


	/*cyclone values*/
  .cyclone_timer : AT (LOADADDR(.fast_gettimeoffset_quotient) + SIZEOF(.fast_gettimeoffset_quotient))  { *(.cyclone_timer) }
  cyclone_timer = LOADADDR(.cyclone_timer);

  .last_cyclone_low : AT (LOADADDR(.cyclone_timer) + SIZEOF(.cyclone_timer)) { *(.last_cyclone_low) }
  last_cyclone_low = LOADADDR(.last_cyclone_low);

  .cyclone_delay_at_last_interrupt : AT (LOADADDR(.last_cyclone_low) + SIZEOF(.last_cyclone_low)) { *(.cyclone_delay_at_last_interrupt) }
  cyclone_delay_at_last_interrupt = LOADADDR(.cyclone_delay_at_last_interrupt);


  .vsyscall_1 ADDR(.vsyscall_0) + 1024: AT (LOADADDR(.vsyscall_0) + 1024) { *(.vsyscall_1) }
  . = LOADADDR(.vsyscall_0) + 4096;

  jiffies_64 = jiffies;
#else
  jiffies = jiffies_64;
#endif
/* END of VSYSCALL_GTOD data*/

  . = ALIGN(THREAD_SIZE);	/* init_task */
  .data.init_task : { *(.data.init_task) }

  /* will be freed after init */
  . = ALIGN(4096);		/* Init code and data */
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
  __kdb_initcall_start = .;
  .kdb_initcall.init : { *(.kdb_initcall.init) }
  __kdb_initcall_end = .;
  SECURITY_INIT
  . = ALIGN(4);
  __alt_instructions = .;
  .altinstructions : { *(.altinstructions) } 
  __alt_instructions_end = .; 
 .altinstr_replacement : { *(.altinstr_replacement) } 
  /* .exit.text is discard at runtime, not link time, to deal with references
     from .altinstructions and .eh_frame */
  .exit.text : { *(.exit.text) }
  .exit.data : { *(.exit.data) }
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
	
  __bss_start = .;		/* BSS */
  .bss : { *(.bss) }
  . = ALIGN(4);
  __bss_stop = .; 

  _end = . ;

  /* This is where the kernel creates the early boot page tables */
  . = ALIGN(4096);
  pg0 = .;

  /* Sections to be discarded */
  /DISCARD/ : {
	*(.exitcall.exit)
	}

  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
}
