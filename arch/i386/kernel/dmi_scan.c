#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/apm_bios.h>
#include <asm/io.h>

struct dmi_header
{
	u8	type;
	u8	length;
	u16	handle;
};

static char * __init dmi_string(struct dmi_header *dm, u8 s)
{
	u8 *bp=(u8 *)dm;
	bp+=dm->length;
	s--;
	while(s>0)
	{
		bp+=strlen(bp);
		bp++;
		s--;
	}
	return bp;
}

static int __init dmi_table(u32 base, int len, int num, void (*decode)(struct dmi_header *))
{
	u8 *buf;
	struct dmi_header *dm;
	u8 *data;
	int i=1;
	int last = 0;	
		
	buf = ioremap(base, len);
	if(buf==NULL)
		return -1;

	data = buf;
	while(i<num && (data - buf) < len)
	{
		dm=(struct dmi_header *)data;
		if(dm->type < last)
			break;
		last = dm->type;
		decode(dm);		
		data+=dm->length;
		while(*data || data[1])
			data++;
		data+=2;
		i++;
	}
	iounmap(buf);
	return 0;
}


int __init dmi_iterate(void (*decode)(struct dmi_header *))
{
	unsigned char buf[20];
	long fp=0xE0000L;
	fp -= 16;
	
	while( fp < 0xFFFFF)
	{
		fp+=16;
		isa_memcpy_fromio(buf, fp, 20);
		if(memcmp(buf, "_DMI_", 5)==0)
		{
			u16 num=buf[13]<<8|buf[12];
			u16 len=buf[7]<<8|buf[6];
			u32 base=buf[11]<<24|buf[10]<<16|buf[9]<<8|buf[8];

			printk(KERN_INFO "DMI %d.%d present.\n",
				buf[14]>>4, buf[14]&0x0F);
			printk(KERN_INFO "%d structures occupying %d bytes.\n",
				buf[13]<<8|buf[12],
				buf[7]<<8|buf[6]);
			printk(KERN_INFO "DMI table at 0x%08X.\n",
				buf[11]<<24|buf[10]<<16|buf[9]<<8|buf[8]);
			if(dmi_table(base,len, num, decode)==0)
				return 0;
		}
	}
	return -1;
}


/*
 *	Process a DMI table entry. Right now all we care about are the BIOS
 *	and machine entries. For 2.4 we should pull the smbus controller info
 *	out of here.
 */

static void __init dmi_decode(struct dmi_header *dm)
{
	u8 *data = (u8 *)dm;
	char *p;
	
	switch(dm->type)
	{
		case  0:
			p=dmi_string(dm,data[4]);

			if(*p && *p!=' ')
			{
				printk("BIOS Vendor: %s\n", p);
				printk("BIOS Version: %s\n", 
					dmi_string(dm, data[5]));
				printk("BIOS Release: %s\n",
					dmi_string(dm, data[8]));
			}
				
			/*
			 *  Check for clue free BIOS implementations who use
			 *  the following QA technique
			 *
			 *      [ Write BIOS Code ]<------
			 *               |                ^
			 *      < Does it Compile >----N--
			 *               |Y               ^
			 *	< Does it Boot Win98 >-N--
			 *               |Y
			 *           [Ship It]
			 *
			 *	Phoenix A04  08/24/2000 is known bad (Dell Inspiron 5000e)
			 *	Phoenix A07  09/29/2000 is known good (Dell Inspiron 5000)
			 */
			 
			if(strcmp(dmi_string(dm, data[4]), "Phoenix Technologies LTD")==0)
			{
				if(strcmp(dmi_string(dm, data[5]), "A04")==0 
					&& strcmp(dmi_string(dm, data[8]), "08/24/2000")==0)
				{
				   	apm_info.get_power_status_broken = 1;
					printk(KERN_WARNING "BIOS strings suggest APM bugs, disabling power status reporting.\n");
				}
			}
			break;
		case 1:
			p=dmi_string(dm,data[4]);

			if(*p && *p!=' ')
			{
				printk("System Vendor: %s.\n",p);
				printk("Product Name: %s.\n",
					dmi_string(dm, data[5]));
				printk("Version %s.\n",
					dmi_string(dm, data[6]));
				printk("Serial Number %s.\n",
					dmi_string(dm, data[7]));
			}
			break;
		case 2:
			p=dmi_string(dm,data[4]);

			if(*p && *p!=' ')
			{
				printk("Board Vendor: %s.\n",p);
				printk("Board Name: %s.\n",
				dmi_string(dm, data[5]));
				printk("Board Version: %s.\n",
					dmi_string(dm, data[6]));
			}
			break;
		case 3:
			p=dmi_string(dm,data[8]);
			if(*p && *p!=' ')
				printk("Asset Tag: %s.\n", p);
			break;
	}
}

static int __init dmi_scan_machine(void)
{
	return dmi_iterate(dmi_decode);
}

module_init(dmi_scan_machine);
