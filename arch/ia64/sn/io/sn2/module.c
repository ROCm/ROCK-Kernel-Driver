/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/string.h>
#include <asm/sn/sgi.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/io.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/module.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/xtalk/xswitch.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/sn_cpuid.h>


/* #define LDEBUG	1  */

#ifdef LDEBUG
#define DPRINTF		printk
#define printf		printk
#else
#define DPRINTF(x...)
#endif

module_t	       *modules[MODULE_MAX];
int			nummodules;

#define SN00_SERIAL_FUDGE	0x3b1af409d513c2
#define SN0_SERIAL_FUDGE	0x6e


static void __init
encode_str_serial(const char *src, char *dest)
{
    int i;

    for (i = 0; i < MAX_SERIAL_NUM_SIZE; i++) {

	dest[i] = src[MAX_SERIAL_NUM_SIZE/2 +
		     ((i%2) ? ((i/2 * -1) - 1) : (i/2))] +
	    SN0_SERIAL_FUDGE;
    }
}

module_t * __init 
module_lookup(moduleid_t id)
{
    int			i;

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
static module_t * __init
module_add_node(geoid_t geoid, cnodeid_t cnodeid)
{
    module_t	       *m;
    int			i;
    char		buffer[16];
    moduleid_t		moduleid;
    slabid_t		slab_number;

    memset(buffer, 0, 16);
    moduleid = geo_module(geoid);
    format_module_id(buffer, moduleid, MODULE_FORMAT_BRIEF);
    DPRINTF("module_add_node: moduleid=%s node=%d\n", buffer, cnodeid);

    if ((m = module_lookup(moduleid)) == 0) {
	m = kmalloc(sizeof (module_t), GFP_KERNEL);
	ASSERT_ALWAYS(m);
	memset(m, 0 , sizeof(module_t));

	for (slab_number = 0; slab_number <= MAX_SLABS; slab_number++) {
		m->nodes[slab_number] = -1;
	}

	m->id = moduleid;
	spin_lock_init(&m->lock);

	/* Insert in sorted order by module number */

	for (i = nummodules; i > 0 && modules[i - 1]->id > moduleid; i--)
	    modules[i] = modules[i - 1];

	modules[i] = m;
	nummodules++;
    }

    /*
     * Save this information in the correct slab number of the node in the 
     * module.
     */
    slab_number = geo_slab(geoid);
    DPRINTF("slab number added 0x%x\n", slab_number);

    if (m->nodes[slab_number] != -1) {
	printk("module_add_node .. slab previously found\n");
	return NULL;
    }

    m->nodes[slab_number] = cnodeid;
    m->geoid[slab_number] = geoid;

    return m;
}

static int __init
module_probe_snum(module_t *m, nasid_t host_nasid, nasid_t nasid)
{
    lboard_t	       *board;
    klmod_serial_num_t *comp;
    char serial_number[16];

    /*
     * record brick serial number
     */
    board = find_lboard_nasid((lboard_t *) KL_CONFIG_INFO(host_nasid), host_nasid, KLTYPE_SNIA);

    if (! board || KL_CONFIG_DUPLICATE_BOARD(board))
    {
	return 0;
    }

    board_serial_number_get( board, serial_number );
    if( serial_number[0] != '\0' ) {
	encode_str_serial( serial_number, m->snum.snum_str );
	m->snum_valid = 1;
    }

    board = find_lboard_nasid((lboard_t *) KL_CONFIG_INFO(nasid),
			nasid, KLTYPE_IOBRICK_XBOW);

    if (! board || KL_CONFIG_DUPLICATE_BOARD(board))
	return 0;

    comp = GET_SNUM_COMP(board);

    if (comp) {
	    if (comp->snum.snum_str[0] != '\0') {
		    memcpy(m->sys_snum, comp->snum.snum_str,
			   MAX_SERIAL_NUM_SIZE);
		    m->sys_snum_valid = 1;
	    }
    }

    if (m->sys_snum_valid)
	return 1;
    else {
	DPRINTF("Invalid serial number for module %d, "
		"possible missing or invalid NIC.", m->id);
	return 0;
    }
}

void __init
io_module_init(void)
{
    cnodeid_t		node;
    lboard_t	       *board;
    nasid_t		nasid;
    int			nserial;
    module_t	       *m;
    extern		int numionodes;

    DPRINTF("*******module_init\n");

    nserial = 0;

    /*
     * First pass just scan for compute node boards KLTYPE_SNIA.
     * We do not support memoryless compute nodes.
     */
    for (node = 0; node < numnodes; node++) {
	nasid = cnodeid_to_nasid(node);
	board = find_lboard_nasid((lboard_t *) KL_CONFIG_INFO(nasid), nasid, KLTYPE_SNIA);
	ASSERT(board);

	HWGRAPH_DEBUG(__FILE__, __FUNCTION__, __LINE__, NULL, NULL, "Found Shub lboard 0x%lx nasid 0x%x cnode 0x%x \n", (unsigned long)board, (int)nasid, (int)node);

	m = module_add_node(board->brd_geoid, node);
	if (! m->snum_valid && module_probe_snum(m, nasid, nasid))
	    nserial++;
    }

    /*
     * Second scan, look for headless/memless board hosted by compute nodes.
     */
    for (node = numnodes; node < numionodes; node++) {
	nasid_t		nasid;
	char		serial_number[16];

        nasid = cnodeid_to_nasid(node);
	board = find_lboard_nasid((lboard_t *) KL_CONFIG_INFO(nasid), 
				nasid, KLTYPE_SNIA);
	ASSERT(board);

	HWGRAPH_DEBUG(__FILE__, __FUNCTION__, __LINE__, NULL, NULL, "Found headless/memless lboard 0x%lx node %d nasid %d cnode %d\n", (unsigned long)board, node, (int)nasid, (int)node);

        m = module_add_node(board->brd_geoid, node);

	/*
	 * Get and initialize the serial number.
	 */
	board_serial_number_get( board, serial_number );
    	if( serial_number[0] != '\0' ) {
        	encode_str_serial( serial_number, m->snum.snum_str );
        	m->snum_valid = 1;
		nserial++;
	}
    }
}
