/* two abstractions specific to kernel/smpboot.c, mainly to cater to visws
 * which needs to alter them. */

static inline void smpboot_clear_io_apic_irqs(void)
{
	io_apic_irqs = 0;
}

static inline void smpboot_setup_warm_reset_vector(unsigned long start_eip)
{
	/* reset code is stored in 8255 on PC-9800. */
	outb(0x0e, 0x37);	/* SHUT0 = 0 */
	local_flush_tlb();
	Dprintk("1.\n");
	*((volatile unsigned short *) TRAMPOLINE_HIGH) = start_eip >> 4;
	Dprintk("2.\n");
	*((volatile unsigned short *) TRAMPOLINE_LOW) = start_eip & 0xf;
	Dprintk("3.\n");
	/*
	 * On PC-9800, continuation on warm reset is done by loading
	 * %ss:%sp from 0x0000:0404 and executing 'lret', so:
	 */
	/* 0x3f0 is on unused interrupt vector and should be safe... */
	*((volatile unsigned long *) phys_to_virt(0x404)) = 0x000003f0;
	Dprintk("4.\n");
}

static inline void smpboot_restore_warm_reset_vector(void)
{
	/*
	 * Install writable page 0 entry to set BIOS data area.
	 */
	local_flush_tlb();

	/*
	 * Paranoid:  Set warm reset code and vector here back
	 * to default values.
	 */
	outb(0x0f, 0x37);	/* SHUT0 = 1 */

	*((volatile long *) phys_to_virt(0x404)) = 0;
}

static inline void smpboot_setup_io_apic(void)
{
	/*
	 * Here we can be sure that there is an IO-APIC in the system. Let's
	 * go and set it up:
	 */
	if (!skip_ioapic_setup && nr_ioapics)
		setup_IO_APIC();
}
