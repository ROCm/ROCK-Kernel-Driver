/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */


/*
 * This is a temporary file that statically initializes the expected 
 * initial klgraph information that is normally provided by prom.
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/sn/sgi.h>
#include <asm/sn/klconfig.h>

void * real_port;
void * real_io_base;
void * real_addr;

char *BW0 = NULL;

kl_config_hdr_t *linux_klcfg;

#ifdef BRINGUP
/* forward declarations */
extern void dump_ii(void), dump_lb(void), dump_crossbow(void);
extern void clear_ii_error(void);
#endif /* BRINGUP */

void
simulated_BW0_init(void)
{

	unsigned long *cnode0_hub;
	unsigned long hub_widget = 0x1000000;
	unsigned long hub_offset = 0x800000;
	unsigned long hub_reg_base = 0;
	extern void * vmalloc(unsigned long);

	memset(&nasid_to_compact_node[0], 0, sizeof(cnodeid_t) * MAX_NASIDS);

	BW0 = vmalloc(0x10000000);
	if (BW0 == NULL) {
		printk("Darn it .. cannot create space for Big Window 0\n");
	}
	printk("BW0: Start Address %p\n", BW0);
	
	memset(BW0+(0x10000000 - 8), 0xf, 0x8);

	printk("BW0: Last WORD address %p has value 0x%lx\n", (char *)(BW0 +(0x10000000 - 8)), *(long *)(BW0 +(0x10000000 - 8)));

	printk("XWIDGET 8 Address = 0x%p\n", (unsigned long *)(NODE_SWIN_BASE(0, 8)) ); 

	/*
	 * Do some HUB Register Hack ..
	 */
	hub_reg_base = (unsigned long)BW0 + hub_widget + hub_offset;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_WID); *cnode0_hub = 0x1c110049;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_WSTAT); *cnode0_hub = 0x0;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_WCR); *cnode0_hub = 0x401b;
	printk("IIO_WCR address = 0x%p\n", cnode0_hub);

        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_ILAPR); *cnode0_hub = 0xffffffffffffffff;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_ILAPO); *cnode0_hub = 0x0;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_IOWA); *cnode0_hub = 0xff01;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_IIWA); *cnode0_hub = 0xff01;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_IIDEM); *cnode0_hub = 0xffffffffffffffff;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_ILCSR); *cnode0_hub = 0x3fc03ff640a;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_ILLR); *cnode0_hub = 0x0;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_IIDSR); *cnode0_hub = 0x1000040;
#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_IGFX0); *cnode0_hub = 0x0;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_IGFX1); *cnode0_hub = 0x0;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_ISCR0); *cnode0_hub = 0x23d;
        cnode0_hub = (unsigned long *)(hub_reg_base + IIO_ISCR1); *cnode0_hub = 0x0;
#endif	/* CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 */
}

#define SYNERGY_WIDGET          ((char *)0xc0000e0000000000)
#define SYNERGY_SWIZZLE         ((char *)0xc0000e0000000400)
#define HUBREG                  ((char *)0xc0000a0001e00000)
#define WIDGET0                 ((char *)0xc0000a0000000000)
#define WIDGET4                 ((char *)0xc0000a0000000004)

#define SYNERGY_WIDGET          ((char *)0xc0000e0000000000)
#define SYNERGY_SWIZZLE         ((char *)0xc0000e0000000400)
#define HUBREG                  ((char *)0xc0000a0001e00000)
#define WIDGET0                 ((char *)0xc0000a0000000000)

int test = 0;

/*
 * Hack to loop for test.
 */
void
test_io_regs(void)
{

	uint32_t reg_32bits;
	uint64_t reg_64bits;

	while (test) {

		reg_32bits = (uint32_t)(*(volatile uint32_t *) SYNERGY_WIDGET);
		reg_64bits = (uint64_t) (*(volatile uint64_t *) SYNERGY_WIDGET);

	}

        printk("Synergy Widget Address = 0x%p, Value = 0x%lx\n", SYNERGY_WIDGET, (uint64_t)*(SYNERGY_WIDGET));

        printk("Synergy swizzle Address = 0x%p, Value = 0x%lx\n", SYNERGY_SWIZZLE, (uint64_t)*(SYNERGY_SWIZZLE));
        printk("HUBREG  Address = 0x%p, Value = 0x%lx\n",  HUBREG, (uint64_t)*(HUBREG));
        printk("WIDGET0 Address = 0x%p, Value = 0x%lx\n", WIDGET0, (uint64_t)*(WIDGET0));
        printk("WIDGET4 Address = 0x%p, Value = 0x%x\n", WIDGET4, (uint32_t)*(WIDGET4));

}

void
klgraph_hack_init(void)
{

	kl_config_hdr_t *kl_hdr_ptr;
	lboard_t	*lb_ptr;
	lboard_t	*temp_ptr;
	klhub_t		*klhub_ptr;
	klioc3_t	*klioc3_ptr;
	klbri_t		*klbri_ptr;
	klxbow_t	*klxbow_ptr;
	klinfo_t	*klinfo_ptr;
	klcomp_t	*klcomp_ptr;
	uint64_t	*tmp;
	volatile u32	*tmp32;

#ifdef 0
	/* Preset some values */
	/* Write IOERR clear to clear the CRAZY bit in the status */
	tmp = (uint64_t *)0xc0000a0001c001f8; *tmp = (uint64_t)0xffffffff;
	/* set widget control register...setting bedrock widget id to b */
	/* tmp = (uint64_t *)0xc0000a0001c00020; *tmp = (uint64_t)0x801b; */
	/* set io outbound widget access...allow all */
	tmp = (uint64_t *)0xc0000a0001c00110; *tmp = (uint64_t)0xff01;
	/* set io inbound widget access...allow all */
	tmp = (uint64_t *)0xc0000a0001c00118; *tmp = (uint64_t)0xff01;
	/* set io crb timeout to max */
	tmp = (uint64_t *)0xc0000a0001c003c0; *tmp = (uint64_t)0xffffff;
	tmp = (uint64_t *)0xc0000a0001c003c0; *tmp = (uint64_t)0xffffff;
	
	/* set local block io permission...allow all */
	tmp = (uint64_t *)0xc0000a0001e04010; *tmp = (uint64_t)0xfffffffffffffff;

	/* clear any errors */
	clear_ii_error();

	/* set default read response buffers in bridge */
	tmp32 = (volatile u32 *)0xc0000a000f000280L;
	*tmp32 = 0xba98;
	tmp32 = (volatile u32 *)0xc0000a000f000288L;
	*tmp32 = 0xba98;
#endif

printk("Widget ID Address 0x%p Value 0x%lx\n", (uint64_t *)0xc0000a0001e00000, *( (volatile uint64_t *)0xc0000a0001e00000) );

printk("Widget ID Address 0x%p Value 0x%lx\n", (uint64_t *)0xc0000a0001c00000, *( (volatile uint64_t *)0xc0000a0001c00000) );

printk("Widget ID Address 0x%p Value 0x%lx\n", (uint64_t *)0xc000020001e00000, *( (volatile uint64_t *)0xc000020001e00000) );


printk("Widget ID Address 0x%p Value 0x%lx\n", (uint64_t *)0xc000020001c00000, *( (volatile uint64_t *)0xc000020001c00000) );

printk("Widget ID Address 0x%p Value 0x%lx\n", (uint64_t *)0xc0000a0001e00000, *( (volatile uint64_t *)0xc0000a0001e00000) );

printk("Xbow ID Address 0x%p Value 0x%x\n", (uint64_t *)0xc0000a0000000000, *( (volatile uint32_t *)0xc0000a0000000000) );

printk("Xbow ID Address 0x%p Value 0x%x\n", (uint64_t *)0xc000020000000004, *( (volatile uint32_t *)0xc000020000000004) );


	if ( test )
		test_io_regs();
	/*
	 * Klconfig header.
	 */
	kl_hdr_ptr = kmalloc(sizeof(kl_config_hdr_t), GFP_KERNEL);
        kl_hdr_ptr->ch_magic = 0xbeedbabe;
        kl_hdr_ptr->ch_version = 0x0;
        kl_hdr_ptr->ch_malloc_hdr_off = 0x48;
        kl_hdr_ptr->ch_cons_off = 0x18;
        kl_hdr_ptr->ch_board_info = 0x0;
        kl_hdr_ptr->ch_cons_info.uart_base = 0x920000000f820178;
        kl_hdr_ptr->ch_cons_info.config_base = 0x920000000f024000;
        kl_hdr_ptr->ch_cons_info.memory_base = 0x920000000f800000;
        kl_hdr_ptr->ch_cons_info.baud = 0x2580;
        kl_hdr_ptr->ch_cons_info.flag = 0x1;
        kl_hdr_ptr->ch_cons_info.type = 0x300fafa;
        kl_hdr_ptr->ch_cons_info.nasid = 0x0;
        kl_hdr_ptr->ch_cons_info.wid = 0xf;
        kl_hdr_ptr->ch_cons_info.npci = 0x4;
        kl_hdr_ptr->ch_cons_info.baseio_nic = 0x0;

	/*
	 * We need to know whether we are booting from PROM or 
	 * boot from disk.
	 */
	linux_klcfg = (kl_config_hdr_t *)0xe000000000030000;
	if (linux_klcfg->ch_magic == 0xbeedbabe) {
		printk("Linux Kernel Booted from Disk\n");
	} else {
		printk("Linux Kernel Booted from PROM\n");
		linux_klcfg = kl_hdr_ptr;
	}

	/*
	 * lboard KLTYPE_IP35
	 */
	lb_ptr = kmalloc(sizeof(lboard_t), GFP_KERNEL);
	kl_hdr_ptr->ch_board_info = (klconf_off_t) lb_ptr;
	temp_ptr = lb_ptr;
	printk("First Lboard = %p\n", temp_ptr);

        lb_ptr->brd_next = 0;
        lb_ptr->struct_type = 0x1;
        lb_ptr->brd_type  = 0x11;
        lb_ptr->brd_sversion = 0x3;
        lb_ptr->brd_brevision = 0x1;
        lb_ptr->brd_promver = 0x1;
        lb_ptr->brd_promver = 0x1;
        lb_ptr->brd_slot = 0x0;
        lb_ptr->brd_debugsw = 0x0;
        lb_ptr->brd_module = 0x145;
        lb_ptr->brd_partition = 0x0;
        lb_ptr->brd_diagval = 0x0;
        lb_ptr->brd_diagparm = 0x0;
        lb_ptr->brd_inventory = 0x0;
        lb_ptr->brd_numcompts = 0x5;
        lb_ptr->brd_nic = 0x2a0aed35;
        lb_ptr->brd_nasid = 0x0;
        lb_ptr->brd_errinfo = 0x0;
        lb_ptr->brd_parent = 0x0;
        lb_ptr->brd_graph_link  = (devfs_handle_t)0x26;
        lb_ptr->brd_owner = 0x0;
        lb_ptr->brd_nic_flags = 0x0;
	memcpy(&lb_ptr->brd_name[0], "IP35", 4);

	/*
	 * Hub Component
	 */
	klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
	klhub_ptr = (klhub_t *)klcomp_ptr;
	klinfo_ptr = (klinfo_t *)klcomp_ptr;
	lb_ptr->brd_compts[0] = (klconf_off_t)klcomp_ptr;
	printk("hub info = %p lboard = %p\n", klhub_ptr, lb_ptr);

	klinfo_ptr = (klinfo_t *)klhub_ptr;
        klinfo_ptr->struct_type = 0x2;
        klinfo_ptr->struct_version = 0x1;
        klinfo_ptr->flags = 0x1;
        klinfo_ptr->revision = 0x1;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0x0;
        klinfo_ptr->nic = 0x2a0aed35;
        klinfo_ptr->physid = 0x0;
        klinfo_ptr->virtid = 0x0;
        klinfo_ptr->widid = 0x0;
        klinfo_ptr->nasid = 0x0;

        klhub_ptr->hub_flags = 0x0;
        klhub_ptr->hub_port.port_nasid = (nasid_t)0x0ffffffff;
        klhub_ptr->hub_port.port_flag = 0x0;
        klhub_ptr->hub_port.port_offset = 0x0;
        klhub_ptr->hub_box_nic = 0x0;
        klhub_ptr->hub_mfg_nic = 0x3f420;
        klhub_ptr->hub_speed = 0xbebc200;

	/*
	 * Memory Component
	 */
        klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
        klinfo_ptr = (klinfo_t *)klcomp_ptr;
	lb_ptr->brd_compts[1] = (klconf_off_t)klcomp_ptr;

        klinfo_ptr->struct_type = 0x3;
        klinfo_ptr->struct_version = 0x2;
        klinfo_ptr->flags = 0x1;
        klinfo_ptr->revision = 0xff;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0x0;
        klinfo_ptr->nic = 0xffffffffffffffff;
        klinfo_ptr->physid = 0xff;
        klinfo_ptr->virtid = 0xffffffff;
        klinfo_ptr->widid = 0x0;
        klinfo_ptr->nasid = 0x0;

	/*
	 * KLSTRUCT_HUB_UART Component
	 */
	klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
	klinfo_ptr = (klinfo_t *)klcomp_ptr;
	lb_ptr->brd_compts[2] = (klconf_off_t)klcomp_ptr;

        klinfo_ptr->struct_type = 0x11;
        klinfo_ptr->struct_version = 0x1;
        klinfo_ptr->flags = 0x31;
        klinfo_ptr->revision = 0xff;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0x0;
        klinfo_ptr->nic = 0xffffffffffffffff;
        klinfo_ptr->physid = 0x0;
        klinfo_ptr->virtid = 0x0;
        klinfo_ptr->widid = 0x0;
        klinfo_ptr->nasid = 0x0;

	/*
	 * KLSTRUCT_CPU Component
	 */
	klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
        klinfo_ptr = (klinfo_t *)klcomp_ptr;
	lb_ptr->brd_compts[3] = (klconf_off_t)klcomp_ptr;

        klinfo_ptr->struct_type = 0x1;
        klinfo_ptr->struct_version = 0x2;
        klinfo_ptr->flags = 0x1;
        klinfo_ptr->revision = 0xff;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0x0;
        klinfo_ptr->nic = 0xffffffffffffffff;
        klinfo_ptr->physid = 0x0;
        klinfo_ptr->virtid = 0x0;
        klinfo_ptr->widid = 0x0;
        klinfo_ptr->nasid = 0x0;

	/*
	 * KLSTRUCT_CPU Component
	 */
	klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
        klinfo_ptr = (klinfo_t *)klcomp_ptr;
	lb_ptr->brd_compts[4] = (klconf_off_t)klcomp_ptr;

        klinfo_ptr->struct_type = 0x1;
        klinfo_ptr->struct_version = 0x2;
        klinfo_ptr->flags = 0x1;
        klinfo_ptr->revision = 0xff;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0x0;
        klinfo_ptr->nic = 0xffffffffffffffff;
        klinfo_ptr->physid = 0x1;
        klinfo_ptr->virtid = 0x1;
        klinfo_ptr->widid = 0x0;
        klinfo_ptr->nasid = 0x0;

	lb_ptr->brd_compts[5] = 0; /* Set the next one to 0 .. end */
	lb_ptr->brd_numcompts = 5; /* 0 to 4 */

	/*
	 * lboard(0x42) KLTYPE_PBRICK_XBOW
	 */
	lb_ptr = kmalloc(sizeof(lboard_t), GFP_KERNEL);
	temp_ptr->brd_next = (klconf_off_t)lb_ptr; /* Let the previous point at the new .. */
	temp_ptr = lb_ptr;
	printk("Second Lboard = %p\n", temp_ptr);

        lb_ptr->brd_next = 0;
        lb_ptr->struct_type = 0x1;
        lb_ptr->brd_type  = 0x42;
        lb_ptr->brd_sversion = 0x2;
        lb_ptr->brd_brevision = 0x0;
        lb_ptr->brd_promver = 0x1;
        lb_ptr->brd_promver = 0x1;
        lb_ptr->brd_slot = 0x0;
        lb_ptr->brd_debugsw = 0x0;
        lb_ptr->brd_module = 0x145;
        lb_ptr->brd_partition = 0x1;
        lb_ptr->brd_diagval = 0x0;
        lb_ptr->brd_diagparm = 0x0;
        lb_ptr->brd_inventory = 0x0;
        lb_ptr->brd_numcompts = 0x1;
        lb_ptr->brd_nic = 0xffffffffffffffff;
        lb_ptr->brd_nasid = 0x0;
        lb_ptr->brd_errinfo = 0x0;
        lb_ptr->brd_parent = (struct lboard_s *)0x9600000000030070;
        lb_ptr->brd_graph_link  = (devfs_handle_t)0xffffffff;
        lb_ptr->brd_owner = 0x0;
        lb_ptr->brd_nic_flags = 0x0;
        memcpy(&lb_ptr->brd_name[0], "IOBRICK", 7);

	/*
	 * KLSTRUCT_XBOW Component
	 */
        klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
	memset(klcomp_ptr, 0, sizeof(klcomp_t));
        klxbow_ptr = (klxbow_t *)klcomp_ptr;
        klinfo_ptr = (klinfo_t *)klcomp_ptr;
        lb_ptr->brd_compts[0] = (klconf_off_t)klcomp_ptr;
	printk("xbow_p 0x%p\n", klcomp_ptr);

        klinfo_ptr->struct_type = 0x4;
        klinfo_ptr->struct_version = 0x1;
        klinfo_ptr->flags = 0x1;
        klinfo_ptr->revision = 0x2;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0x0;
        klinfo_ptr->nic = 0xffffffffffffffff;
        klinfo_ptr->physid = 0xff;
        klinfo_ptr->virtid = 0x0;
        klinfo_ptr->widid = 0x0;
        klinfo_ptr->nasid = 0x0;

        klxbow_ptr->xbow_master_hub_link = 0xb;
        klxbow_ptr->xbow_port_info[0].port_nasid = 0x0;
        klxbow_ptr->xbow_port_info[0].port_flag = 0x0;
        klxbow_ptr->xbow_port_info[0].port_offset = 0x0;

        klxbow_ptr->xbow_port_info[1].port_nasid = 0x401;
        klxbow_ptr->xbow_port_info[1].port_flag = 0x0;
        klxbow_ptr->xbow_port_info[1].port_offset = 0x0;

        klxbow_ptr->xbow_port_info[2].port_nasid = 0x0;
        klxbow_ptr->xbow_port_info[2].port_flag = 0x0;
        klxbow_ptr->xbow_port_info[2].port_offset = 0x0;

        klxbow_ptr->xbow_port_info[3].port_nasid = 0x0; /* ffffffff */
        klxbow_ptr->xbow_port_info[3].port_flag = 0x6;
        klxbow_ptr->xbow_port_info[3].port_offset = 0x30070;

        klxbow_ptr->xbow_port_info[4].port_nasid = 0x0; /* ffffff00; */
        klxbow_ptr->xbow_port_info[4].port_flag = 0x0;
        klxbow_ptr->xbow_port_info[4].port_offset = 0x0;

        klxbow_ptr->xbow_port_info[5].port_nasid = 0x0;
        klxbow_ptr->xbow_port_info[5].port_flag = 0x0;
        klxbow_ptr->xbow_port_info[5].port_offset = 0x0;
        klxbow_ptr->xbow_port_info[6].port_nasid = 0x0;
        klxbow_ptr->xbow_port_info[6].port_flag = 0x5;
        klxbow_ptr->xbow_port_info[6].port_offset = 0x30210;
        klxbow_ptr->xbow_port_info[7].port_nasid = 0x3;
        klxbow_ptr->xbow_port_info[7].port_flag = 0x5;
        klxbow_ptr->xbow_port_info[7].port_offset = 0x302e0;
	
	lb_ptr->brd_compts[1] = 0;
        lb_ptr->brd_numcompts = 1;


	/*
	 * lboard KLTYPE_PBRICK
	 */
	lb_ptr = kmalloc(sizeof(lboard_t), GFP_KERNEL);
	temp_ptr->brd_next = (klconf_off_t)lb_ptr; /* Let the previous point at the new .. */
	temp_ptr = lb_ptr;
	printk("Third Lboard %p\n", lb_ptr);

        lb_ptr->brd_next = 0;
        lb_ptr->struct_type = 0x1;
        lb_ptr->brd_type  = 0x72;
        lb_ptr->brd_sversion = 0x2;
        lb_ptr->brd_brevision = 0x0;
        lb_ptr->brd_promver = 0x1;
        lb_ptr->brd_promver = 0x41;
        lb_ptr->brd_slot = 0xe;
        lb_ptr->brd_debugsw = 0x0;
        lb_ptr->brd_module = 0x145;
        lb_ptr->brd_partition = 0x1;
        lb_ptr->brd_diagval = 0x0;
        lb_ptr->brd_diagparm = 0x0;
        lb_ptr->brd_inventory = 0x0;
        lb_ptr->brd_numcompts = 0x1;
        lb_ptr->brd_nic = 0x30e3fd;
        lb_ptr->brd_nasid = 0x0;
        lb_ptr->brd_errinfo = 0x0;
        lb_ptr->brd_parent = (struct lboard_s *)0x9600000000030140;
        lb_ptr->brd_graph_link  = (devfs_handle_t)0xffffffff;
        lb_ptr->brd_owner = 0x0;
        lb_ptr->brd_nic_flags = 0x0;
	memcpy(&lb_ptr->brd_name[0], "IP35", 4);

	/*
	 * KLSTRUCT_BRI Component
	 */
        klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
        klbri_ptr = (klbri_t *)klcomp_ptr;
        klinfo_ptr = (klinfo_t *)klcomp_ptr;
        lb_ptr->brd_compts[0] = (klconf_off_t)klcomp_ptr;

        klinfo_ptr->struct_type = 0x5;
        klinfo_ptr->struct_version = 0x2;
        klinfo_ptr->flags = 0x1;
        klinfo_ptr->revision = 0x2;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0xd002;
        klinfo_ptr->nic = 0x30e3fd;
        klinfo_ptr->physid = 0xe;
        klinfo_ptr->virtid = 0xe;
        klinfo_ptr->widid = 0xe;
        klinfo_ptr->nasid = 0x0;

        klbri_ptr->bri_eprominfo = 0xff;
        klbri_ptr->bri_bustype = 0x7;
        klbri_ptr->bri_mfg_nic = 0x3f4a8;

        lb_ptr->brd_compts[1] = 0;
        lb_ptr->brd_numcompts = 1;

	/*
	 * lboard KLTYPE_PBRICK
	 */
	lb_ptr = kmalloc(sizeof(lboard_t), GFP_KERNEL);
	temp_ptr->brd_next = (klconf_off_t)lb_ptr; /* Let the previous point at the new .. */
	temp_ptr = lb_ptr;
	printk("Fourth Lboard %p\n", lb_ptr);

        lb_ptr->brd_next = 0x0;
        lb_ptr->struct_type = 0x1;
        lb_ptr->brd_type  = 0x72;
        lb_ptr->brd_sversion = 0x2;
        lb_ptr->brd_brevision = 0x0;
        lb_ptr->brd_promver = 0x1;
        lb_ptr->brd_promver = 0x31;
        lb_ptr->brd_slot = 0xf;
        lb_ptr->brd_debugsw = 0x0;
        lb_ptr->brd_module = 0x145;
        lb_ptr->brd_partition = 0x1;
        lb_ptr->brd_diagval = 0x0;
        lb_ptr->brd_diagparm = 0x0;
        lb_ptr->brd_inventory = 0x0;
        lb_ptr->brd_numcompts = 0x6;
        lb_ptr->brd_nic = 0x30e3fd;
        lb_ptr->brd_nasid = 0x0;
        lb_ptr->brd_errinfo = 0x0;
        lb_ptr->brd_parent = (struct lboard_s *)0x9600000000030140;
        lb_ptr->brd_graph_link  = (devfs_handle_t)0xffffffff;
        lb_ptr->brd_owner = 0x0;
        lb_ptr->brd_nic_flags = 0x0;
	memcpy(&lb_ptr->brd_name[0], "IP35", 4);


	/*
	 * KLSTRUCT_BRI Component
	 */
        klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
	klbri_ptr = (klbri_t *)klcomp_ptr;
        klinfo_ptr = (klinfo_t *)klcomp_ptr;
        lb_ptr->brd_compts[0] = (klconf_off_t)klcomp_ptr;

        klinfo_ptr->struct_type = 0x5;
        klinfo_ptr->struct_version = 0x2;
        klinfo_ptr->flags = 0x1;
        klinfo_ptr->revision = 0x2;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0xd002;
        klinfo_ptr->nic = 0x30e3fd;
        klinfo_ptr->physid = 0xf;
        klinfo_ptr->virtid = 0xf;
        klinfo_ptr->widid = 0xf;
        klinfo_ptr->nasid = 0x0;

        klbri_ptr->bri_eprominfo = 0xff;
        klbri_ptr->bri_bustype = 0x7;
        klbri_ptr->bri_mfg_nic = 0x3f528;

	/*
	 * KLSTRUCT_SCSI component
	 */
        klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
        klinfo_ptr = (klinfo_t *)klcomp_ptr;
        lb_ptr->brd_compts[1] = (klconf_off_t)klcomp_ptr;

        klinfo_ptr->struct_type = 0xb;
        klinfo_ptr->struct_version = 0x1;
        klinfo_ptr->flags = 0x31;
        klinfo_ptr->revision = 0x5;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0x0;
        klinfo_ptr->nic = 0xffffffffffffffff;
        klinfo_ptr->physid = 0x1;
        klinfo_ptr->virtid = 0x0;
        klinfo_ptr->widid = 0xf;
        klinfo_ptr->nasid = 0x0;

	/*
	 * KLSTRUCT_IOC3 Component
	 */
        klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
        klioc3_ptr = (klioc3_t *)klcomp_ptr;
        klinfo_ptr = (klinfo_t *)klcomp_ptr;
        lb_ptr->brd_compts[2] = (klconf_off_t)klcomp_ptr;

        klinfo_ptr->struct_type = 0x6;
        klinfo_ptr->struct_version = 0x1;
        klinfo_ptr->flags = 0x31;
        klinfo_ptr->revision = 0x1;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0x0;
        klinfo_ptr->nic = 0xffffffffffffffff;
        klinfo_ptr->physid = 0x4;
        klinfo_ptr->virtid = 0x0;
        klinfo_ptr->widid = 0xf;
        klinfo_ptr->nasid = 0x0;

        klioc3_ptr->ioc3_ssram = 0x0;
        klioc3_ptr->ioc3_nvram = 0x0;

	/*
	 * KLSTRUCT_UNKNOWN Component
	 */
        klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
        klinfo_ptr = (klinfo_t *)klcomp_ptr;
        lb_ptr->brd_compts[3] = (klconf_off_t)klcomp_ptr;

        klinfo_ptr->struct_type = 0x0;
        klinfo_ptr->struct_version = 0x1;
        klinfo_ptr->flags = 0x31;
        klinfo_ptr->revision = 0xff;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0x0;
        klinfo_ptr->nic = 0xffffffffffffffff;
        klinfo_ptr->physid = 0x5;
        klinfo_ptr->virtid = 0x0;
        klinfo_ptr->widid = 0xf;
        klinfo_ptr->nasid = 0x0;

	/*
	 * KLSTRUCT_SCSI Component
	 */
        klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
        klinfo_ptr = (klinfo_t *)klcomp_ptr;
        lb_ptr->brd_compts[4] = (klconf_off_t)klcomp_ptr;

        klinfo_ptr->struct_type = 0xb;
        klinfo_ptr->struct_version = 0x1;
        klinfo_ptr->flags = 0x31;
        klinfo_ptr->revision = 0x1;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0x0;
        klinfo_ptr->nic = 0xffffffffffffffff;
        klinfo_ptr->physid = 0x6;
        klinfo_ptr->virtid = 0x5;
        klinfo_ptr->widid = 0xf;
        klinfo_ptr->nasid = 0x0;

	/*
	 * KLSTRUCT_UNKNOWN
	 */
        klcomp_ptr = kmalloc(sizeof(klcomp_t), GFP_KERNEL);
        klinfo_ptr = (klinfo_t *)klcomp_ptr;
        lb_ptr->brd_compts[5] = (klconf_off_t)klcomp_ptr;

        klinfo_ptr->struct_type = 0x0;
        klinfo_ptr->struct_version = 0x1;
        klinfo_ptr->flags = 0x31;
        klinfo_ptr->revision = 0xff;
        klinfo_ptr->diagval = 0x0;
        klinfo_ptr->diagparm = 0x0;
        klinfo_ptr->inventory = 0x0;
        klinfo_ptr->partid = 0x0;
        klinfo_ptr->nic = 0xffffffffffffffff;
        klinfo_ptr->physid = 0x7;
        klinfo_ptr->virtid = 0x0;
        klinfo_ptr->widid = 0xf;
        klinfo_ptr->nasid = 0x0;

	lb_ptr->brd_compts[6] = 0;
	lb_ptr->brd_numcompts = 6;

}




	
#ifdef BRINGUP
/* 
 * these were useful for printing out registers etc
 * during bringup  
 */

void
xdump(long long *addr, int count)
{
	int ii;
	volatile long long *xx = addr;

	for ( ii = 0; ii < count; ii++, xx++ ) {
		printk("0x%p : 0x%p\n", xx, *xx);
	}
}

void
xdump32(unsigned int *addr, int count)
{
	int ii;
	volatile unsigned int *xx = addr;

	for ( ii = 0; ii < count; ii++, xx++ ) {
		printk("0x%p : 0x%0x\n", xx, *xx);
	}
}



void
clear_ii_error(void)
{
	volatile long long *tmp;

	printk("... WSTAT ");
	xdump((long long *)0xc0000a0001c00008, 1);
	printk("... WCTRL ");
	xdump((long long *)0xc0000a0001c00020, 1);
	printk("... WLCSR ");
	xdump((long long *)0xc0000a0001c00128, 1);
	printk("... IIDSR ");
	xdump((long long *)0xc0000a0001c00138, 1);
        printk("... IOPRBs ");
	xdump((long long *)0xc0000a0001c00198, 9);
	printk("... IXSS ");
	xdump((long long *)0xc0000a0001c00210, 1);
	printk("... IBLS0 ");
	xdump((long long *)0xc0000a0001c10000, 1);
	printk("... IBLS1 ");
	xdump((long long *)0xc0000a0001c20000, 1);

        /* Write IOERR clear to clear the CRAZY bit in the status */
        tmp = (long long *)0xc0000a0001c001f8; *tmp = (long long)0xffffffff;

	/* dump out local block error registers */
	printk("... ");
	xdump((long long *)0xc0000a0001e04040, 1);	/* LB_ERROR_BITS */
	printk("... ");
	xdump((long long *)0xc0000a0001e04050, 1);	/* LB_ERROR_HDR1 */
	printk("... ");
	xdump((long long *)0xc0000a0001e04058, 1);	/* LB_ERROR_HDR2 */
	/* and clear the LB_ERROR_BITS */
	tmp = (long long *)0xc0000a0001e04040; *tmp = 0x0;
	printk("clr: ");
	xdump((long long *)0xc0000a0001e04040, 1);	/* LB_ERROR_BITS */
	tmp = (long long *)0xc0000a0001e04050; *tmp = 0x0;
	tmp = (long long *)0xc0000a0001e04058; *tmp = 0x0;
}


void
dump_ii()
{
	printk("===== Dump the II regs =====\n");
	xdump((long long *)0xc0000a0001c00000, 2);
	xdump((long long *)0xc0000a0001c00020, 1);
	xdump((long long *)0xc0000a0001c00100, 37);
	xdump((long long *)0xc0000a0001c00300, 98);
	xdump((long long *)0xc0000a0001c10000, 6);
	xdump((long long *)0xc0000a0001c20000, 6);
	xdump((long long *)0xc0000a0001c30000, 2);

	xdump((long long *)0xc0000a0000000000, 1);
	xdump((long long *)0xc0000a0001000000, 1);
	xdump((long long *)0xc0000a0002000000, 1);
	xdump((long long *)0xc0000a0003000000, 1);
	xdump((long long *)0xc0000a0004000000, 1);
	xdump((long long *)0xc0000a0005000000, 1);
	xdump((long long *)0xc0000a0006000000, 1);
	xdump((long long *)0xc0000a0007000000, 1);
	xdump((long long *)0xc0000a0008000000, 1);
	xdump((long long *)0xc0000a0009000000, 1);
	xdump((long long *)0xc0000a000a000000, 1);
	xdump((long long *)0xc0000a000b000000, 1);
	xdump((long long *)0xc0000a000c000000, 1);
	xdump((long long *)0xc0000a000d000000, 1);
	xdump((long long *)0xc0000a000e000000, 1);
	xdump((long long *)0xc0000a000f000000, 1);
}

void
dump_lb()
{
	printk("===== Dump the LB regs =====\n");
	xdump((long long *)0xc0000a0001e00000, 1);
	xdump((long long *)0xc0000a0001e04000, 13);
	xdump((long long *)0xc0000a0001e04100, 2);
	xdump((long long *)0xc0000a0001e04200, 2);
	xdump((long long *)0xc0000a0001e08000, 5);
	xdump((long long *)0xc0000a0001e08040, 2);
	xdump((long long *)0xc0000a0001e08050, 3);
	xdump((long long *)0xc0000a0001e0c000, 3);
	xdump((long long *)0xc0000a0001e0c020, 4);
}

void
dump_crossbow()
{
	printk("===== Dump the Crossbow regs =====\n");
	clear_ii_error();
	xdump32((unsigned int *)0xc0000a0000000004, 1);
	clear_ii_error();
	xdump32((unsigned int *)0xc0000a0000000000, 1);
	printk("and again..\n");
	xdump32((unsigned int *)0xc0000a0000000000, 1);
	xdump32((unsigned int *)0xc0000a0000000000, 1);


	clear_ii_error();

	xdump32((unsigned int *)0xc000020000000004, 1);
	clear_ii_error();
	xdump32((unsigned int *)0xc000020000000000, 1);
	clear_ii_error();

	xdump32((unsigned int *)0xc0000a0000800004, 1);
	clear_ii_error();
	xdump32((unsigned int *)0xc0000a0000800000, 1);
	clear_ii_error();

	xdump32((unsigned int *)0xc000020000800004, 1);
	clear_ii_error();
	xdump32((unsigned int *)0xc000020000800000, 1);
	clear_ii_error();


}
#endif /* BRINGUP */
