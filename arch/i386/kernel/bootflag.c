/*
 *	Implement 'Simple Boot Flag Specification 1.0'
 *
 */


#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/io.h>

#include <linux/mc146818rtc.h>


#define SBF_RESERVED (0x78)
#define SBF_PNPOS    (1<<0)
#define SBF_BOOTING  (1<<1)
#define SBF_DIAG     (1<<2)
#define SBF_PARITY   (1<<7)


struct sbf_boot
{
	u8 sbf_signature[4];
	u32 sbf_len;
	u8 sbf_revision __attribute((packed));
	u8 sbf_csum __attribute((packed));
	u8 sbf_oemid[6] __attribute((packed));
	u8 sbf_oemtable[8] __attribute((packed));
	u8 sbf_revdata[4] __attribute((packed));
	u8 sbf_creator[4] __attribute((packed));
	u8 sbf_crearev[4] __attribute((packed));
	u8 sbf_cmos __attribute((packed));
	u8 sbf_spare[3] __attribute((packed));
};


static int sbf_port __initdata = -1;

static int __init sbf_struct_valid(unsigned long tptr)
{
	u8 *ap;
	u8 v;
	unsigned int i;
	struct sbf_boot sb;
	
	memcpy_fromio(&sb, (void *)tptr, sizeof(sb));

	if(sb.sbf_len != 40 && sb.sbf_len != 39)
		// 39 on IBM ThinkPad A21m, BIOS version 1.02b (KXET24WW; 2000-12-19).
		return 0;

	ap = (u8 *)&sb;
	v= 0;
	
	for(i=0;i<sb.sbf_len;i++)
		v+=*ap++;
		
	if(v)
		return 0;

	if(memcmp(sb.sbf_signature, "BOOT", 4))
		return 0;

	if (sb.sbf_len == 39)
		printk (KERN_WARNING "SBF: ACPI BOOT descriptor is wrong length (%d)\n",
			sb.sbf_len);

	sbf_port = sb.sbf_cmos;	/* Save CMOS port */
	return 1;
}

static int __init parity(u8 v)
{
	int x = 0;
	int i;
	
	for(i=0;i<8;i++)
	{
		x^=(v&1);
		v>>=1;
	}
	return x;
}

static void __init sbf_write(u8 v)
{
	unsigned long flags;
	if(sbf_port != -1)
	{
		v &= ~SBF_PARITY;
		if(!parity(v))
			v|=SBF_PARITY;

		printk(KERN_INFO "SBF: Setting boot flags 0x%x\n",v);

		spin_lock_irqsave(&rtc_lock, flags);
		CMOS_WRITE(v, sbf_port);
		spin_unlock_irqrestore(&rtc_lock, flags);
	}
}

static u8 __init sbf_read(void)
{
	u8 v;
	unsigned long flags;
	if(sbf_port == -1)
		return 0;
	spin_lock_irqsave(&rtc_lock, flags);
	v = CMOS_READ(sbf_port);
	spin_unlock_irqrestore(&rtc_lock, flags);
	return v;
}

static int __init sbf_value_valid(u8 v)
{
	if(v&SBF_RESERVED)		/* Reserved bits */
		return 0;
	if(!parity(v))
		return 0;
	return 1;
}


static void __init sbf_bootup(void)
{
	u8 v;
	if(sbf_port == -1)
		return;
	v = sbf_read();
	if(!sbf_value_valid(v))
		printk(KERN_WARNING "SBF: Simple boot flag value 0x%x read from CMOS RAM was invalid\n",v);
	v &= ~SBF_RESERVED;
	v &= ~SBF_BOOTING;
	v &= ~SBF_DIAG;
#if defined(CONFIG_ISAPNP)
	v |= SBF_PNPOS;
#endif
	sbf_write(v);
}

static int __init sbf_init(void)
{
	unsigned int i;
	void *rsdt;
	u32 rsdtlen = 0;
	u32 rsdtbase = 0;
	u8 sum = 0;
	int n;
	
	u8 *p;
	
	for(i=0xE0000; i <= 0xFFFE0; i+=16)
	{
		p = phys_to_virt(i);
		
		if(memcmp(p, "RSD PTR ", 8))
			continue;
		
		sum = 0;
		for(n=0; n<20; n++)
			sum+=p[n];
			
		if(sum != 0)
			continue;
			
		/* So it says RSD PTR and it checksums... */

		/*
		 *	Process the RDSP pointer
		 */
	 
		rsdtbase = *(u32 *)(p+16);
		
		/*
		 *	RSDT length is ACPI 2 only, for ACPI 1 we must map
		 *	and remap.
		 */
		 
		if(p[15]>1)
			rsdtlen = *(u32 *)(p+20);
		else
			rsdtlen = 36;

		if(rsdtlen < 36 || rsdtlen > 1024)
			continue;
		break;
	}
	if(i>0xFFFE0)
		return 0;
		
		
	rsdt = ioremap(rsdtbase, rsdtlen);
	if(rsdt == 0)
		return 0;
		
	i = readl(rsdt + 4);
	
	/*
	 *	Remap if needed
	 */
	 
	if(i > rsdtlen)
	{
		rsdtlen = i;
		iounmap(rsdt);
		rsdt = ioremap(rsdtbase, rsdtlen);
		if(rsdt == 0)
			return 0;
	}
	
	for(n = 0; n < i; n++)
		sum += readb(rsdt + n);
		
	if(sum)
	{
		iounmap(rsdt);
		return 0;
	}
	
	/* Ok the RSDT checksums too */
	
	for(n = 36; n+3 < i; n += 4)
	{
		unsigned long rp = readl(rsdt+n);
		int len = 4096;

		if(rp > 0xFFFFFFFFUL - len)
			len = 0xFFFFFFFFUL - rp;
			
		/* Too close to the end!! */
		if(len < 20)
			continue;
		rp = (unsigned long)ioremap(rp, 4096);
		if(rp == 0)
			continue;
		if(sbf_struct_valid(rp))
		{
			/* Found the BOOT table and processed it */
			printk(KERN_INFO "SBF: Simple Boot Flag extension found and enabled.\n");
		}
		iounmap((void *)rp);
	}
	iounmap(rsdt);
	sbf_bootup();
	return 0;
}

module_init(sbf_init);
