/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/cmn_err.h>
#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn1/hubdev.h>
#include <asm/sn/module.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/xtalk/xswitch.h>
#include <asm/sn/nodepda.h>


#define LDEBUG	1	

#define DPRINTF		if (LDEBUG) printk
#define printf		printk

module_t	       *modules[MODULE_MAX];
int			nummodules;

#define SN00_SERIAL_FUDGE	0x3b1af409d513c2
#define SN0_SERIAL_FUDGE	0x6e

void
encode_int_serial(uint64_t src,uint64_t *dest)
{
    uint64_t val;
    int i;

    val = src + SN00_SERIAL_FUDGE;


    for (i = 0; i < sizeof(long long); i++) {
	((char*)dest)[i] =
	    ((char*)&val)[sizeof(long long)/2 +
			 ((i%2) ? ((i/2 * -1) - 1) : (i/2))];
    }
}


void
decode_int_serial(uint64_t src, uint64_t *dest)
{
    uint64_t val;
    int i;

    for (i = 0; i < sizeof(long long); i++) {
	((char*)&val)[sizeof(long long)/2 +
		     ((i%2) ? ((i/2 * -1) - 1) : (i/2))] =
	    ((char*)&src)[i];
    }

    *dest = val - SN00_SERIAL_FUDGE;
}


void
encode_str_serial(const char *src, char *dest)
{
    int i;

    for (i = 0; i < MAX_SERIAL_NUM_SIZE; i++) {

	dest[i] = src[MAX_SERIAL_NUM_SIZE/2 +
		     ((i%2) ? ((i/2 * -1) - 1) : (i/2))] +
	    SN0_SERIAL_FUDGE;
    }
}

void
decode_str_serial(const char *src, char *dest)
{
    int i;

    for (i = 0; i < MAX_SERIAL_NUM_SIZE; i++) {
	dest[MAX_SERIAL_NUM_SIZE/2 +
	    ((i%2) ? ((i/2 * -1) - 1) : (i/2))] = src[i] -
	    SN0_SERIAL_FUDGE;
    }
}


module_t *module_lookup(moduleid_t id)
{
    int			i;

    DPRINTF("module_lookup: id=%d\n", id);

    for (i = 0; i < nummodules; i++)
	if (modules[i]->id == id) {
	    DPRINTF("module_lookup: found m=0x%p\n", modules[i]);
	    return modules[i];
	}

    return NULL;
}

/*
 * module_add_node
 *
 *   The first time a new module number is seen, a module structure is
 *   inserted into the module list in order sorted by module number
 *   and the structure is initialized.
 *
 *   The node number is added to the list of nodes in the module.
 */

module_t *module_add_node(moduleid_t id, cnodeid_t n)
{
    module_t	       *m;
    int			i;

    DPRINTF("module_add_node: id=%x node=%d\n", id, n);

    if ((m = module_lookup(id)) == 0) {
#ifndef CONFIG_IA64_SGI_IO
	m = kmem_zalloc_node(sizeof (module_t), KM_NOSLEEP, n);
#else
	m = kmalloc(sizeof (module_t), GFP_KERNEL);
	memset(m, 0 , sizeof(module_t));
	printk("Module nodecnt = %d\n", m->nodecnt); 
#endif
	ASSERT_ALWAYS(m);

	DPRINTF("module_add_node: m=0x%p\n", m);

	m->id = id;
	spin_lock_init(&m->lock);

	init_MUTEX_LOCKED(&m->thdcnt);

printk("Set elsc to 0x%p on node %d\n", &m->elsc, get_nasid());

set_elsc(&m->elsc);
	elsc_init(&m->elsc, COMPACT_TO_NASID_NODEID(n));
	spin_lock_init(&m->elsclock);

	/* Insert in sorted order by module number */

	for (i = nummodules; i > 0 && modules[i - 1]->id > id; i--)
	    modules[i] = modules[i - 1];

	modules[i] = m;
	nummodules++;
    }

    m->nodes[m->nodecnt++] = n;

printk("module_add_node: module %x now has %d nodes\n", id, m->nodecnt);
    DPRINTF("module_add_node: module %x now has %d nodes\n", id, m->nodecnt);

    return m;
}

int module_probe_snum(module_t *m, nasid_t nasid)
{
    lboard_t	       *board;
    klmod_serial_num_t *comp;

    board = find_lboard((lboard_t *) KL_CONFIG_INFO(nasid),
			KLTYPE_MIDPLANE8);

    if (! board || KL_CONFIG_DUPLICATE_BOARD(board))
	return 0;

    comp = GET_SNUM_COMP(board);

    if (comp) {
#if LDEBUG
	    int i;

	    printf("********found module with id %x and string", m->id);

	    for (i = 0; i < MAX_SERIAL_NUM_SIZE; i++)
		printf(" %x ", comp->snum.snum_str[i]);

	    printf("\n");	/* Fudged string is not ASCII */
#endif

	    if (comp->snum.snum_str[0] != '\0') {
		bcopy(comp->snum.snum_str,
		      m->snum.snum_str,
		      MAX_SERIAL_NUM_SIZE);
		m->snum_valid = 1;
	    }
    }

    if (m->snum_valid)
	return 1;
    else {
#ifndef CONFIG_IA64_SGI_IO
	cmn_err(CE_WARN | CE_MAINTENANCE,
		"Invalid serial number for module %d, "
		"possible missing or invalid NIC.", m->id);
#else
	printk("Invalid serial number for module %d, "
		"possible missing or invalid NIC.", m->id);
#endif
	return 0;
    }
}

void
io_module_init(void)
{
    cnodeid_t		node;
    lboard_t	       *board;
    nasid_t		nasid;
    int			nserial;
    module_t	       *m;

    DPRINTF("*******module_init\n");

    nserial = 0;

    for (node = 0; node < numnodes; node++) {
	nasid = COMPACT_TO_NASID_NODEID(node);

	board = find_lboard((lboard_t *) KL_CONFIG_INFO(nasid),
			    KLTYPE_IP27);
	ASSERT(board);

	m = module_add_node(board->brd_module, node);

	if (! m->snum_valid && module_probe_snum(m, nasid))
	    nserial++;
    }

    DPRINTF("********found total of %d serial numbers in the system\n",
	    nserial);

    if (nserial == 0)
	cmn_err(CE_WARN, "No serial number found.");
}

#ifdef BRINGUP
elsc_t *Elsc[100];

void
set_elsc(elsc_t *p)
{
      Elsc[get_nasid()] = p;
}
#endif

elsc_t *get_elsc(void)
{
#ifdef BRINGUP
return(Elsc[get_nasid()]);
#else
	if ( NODEPDA(get_nasid())->module == (module_t *)0 ) {
		printf("get_elsc() for nasd %d fails\n", get_nasid());
//		return((elsc_t *)0);
	}
	return &NODEPDA(get_nasid())->module->elsc;

//	return &NODEPDA(NASID_TO_COMPACT_NODEID(0))->module->elsc;
#endif
}

int
get_kmod_info(cmoduleid_t cmod, module_info_t *mod_info)
{
    int i;

    if (cmod < 0 || cmod >= nummodules)
	return EINVAL;

    if (! modules[cmod]->snum_valid)
	return ENXIO;

    mod_info->mod_num = modules[cmod]->id;
    {
	char temp[MAX_SERIAL_NUM_SIZE];

	decode_str_serial(modules[cmod]->snum.snum_str, temp);

	/* if this is an invalid serial number return an error */
	if (temp[0] != 'K')
	    return ENXIO;

	mod_info->serial_num = 0;

	for (i = 0; i < MAX_SERIAL_NUM_SIZE && temp[i] != '\0'; i++) {
	    mod_info->serial_num <<= 4;
	    mod_info->serial_num |= (temp[i] & 0xf);

	    mod_info->serial_str[i] = temp[i];
	}

	mod_info->serial_str[i] = '\0';
    }

    return 0;
}
