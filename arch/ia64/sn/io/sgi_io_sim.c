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
#include <asm/sn/sgi.h>
#include <asm/sn/agent.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/module.h>
#include <asm/sn/nic.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/synergy.h>

cnodeid_t nasid_to_compact_node[MAX_NASIDS];
nasid_t compact_to_nasid_node[MAX_COMPACT_NODES];
cnodeid_t cpuid_to_compact_node[MAXCPUS];
cpuid_t         master_procid = 0;
int maxnodes;
char arg_maxnodes[4];

nodepda_t       *Nodepdaindr[MAX_COMPACT_NODES];
nodepda_t        *nodepda;
subnode_pda_t    *subnodepda;

synergy_da_t	*Synergy_da_indr[MAX_COMPACT_NODES * 2];

extern void init_all_devices(void);


/*
 * Return non-zero if the given variable was specified
 */
int
is_specified(char *s)
{
        return (strlen(s) != 0);
}


void pciba_init(void)
{
	FIXME("pciba_init : no-op\n");
}

void xbmon_init(void)
{
	FIXME("xbmon_init : no-op\n");

}

void pciiox_init(void)
{
	FIXME("pciiox_init : no-op\n");

}

void usrpci_init(void)
{
	FIXME("usrpci_init : no-op\n");

}

void ioc3_init(void)
{
	FIXME("ioc3_init : no-op\n");

}

void initialize_io(void)
{

	init_all_devices();
}

/*
 * Routines provided by ml/SN/promif.c.
 */
static __psunsigned_t master_bridge_base = (__psunsigned_t)NULL;
static nasid_t console_nasid;
static char console_wid;
static char console_pcislot;

void
set_master_bridge_base(void)
{

#ifdef SIMULATED_KLGRAPH
	printk("set_master_bridge_base: SIMULATED_KLGRAPH FIXME hardwired master.\n");
	console_nasid = 0;
	console_wid = 0x8;
	console_pcislot = 0x2;
#else
        console_nasid = KL_CONFIG_CH_CONS_INFO(master_nasid)->nasid;
        console_wid = WIDGETID_GET(KL_CONFIG_CH_CONS_INFO(master_nasid)->memory_base);
        console_pcislot = KL_CONFIG_CH_CONS_INFO(master_nasid)->npci;
#endif /* SIMULATED_KLGRAPH */

        master_bridge_base = (__psunsigned_t)NODE_SWIN_BASE(console_nasid,
                                                            console_wid);
}

int
check_nasid_equiv(nasid_t nasida, nasid_t nasidb)
{
        if ((nasida == nasidb) ||
            (nasida == NODEPDA(NASID_TO_COMPACT_NODEID(nasidb))->xbow_peer))
                return 1;
        else
                return 0;
}

int
is_master_nasid_widget(nasid_t test_nasid, xwidgetnum_t test_wid)
{

        /*
         * If the widget numbers are different, we're not the master.
         */
        if (test_wid != (xwidgetnum_t)console_wid)
                return 0;

        /*
         * If the NASIDs are the same or equivalent, we're the master.
         */
        if (check_nasid_equiv(test_nasid, console_nasid)) {
                return 1;
        } else {
                return 0;
        }
}

cnodeid_t
nasid_to_compact_nodeid(nasid_t nasid)
{
        ASSERT(nasid >= 0 && nasid < MAX_NASIDS);
        return nasid_to_compact_node[nasid];
}

nasid_t
compact_to_nasid_nodeid(cnodeid_t cnode)
{
        ASSERT(cnode >= 0 && cnode <= MAX_COMPACT_NODES);
        ASSERT(compact_to_nasid_node[cnode] >= 0);
        return compact_to_nasid_node[cnode];
}

/*
 * Routines provided by ml/SN/nvram.c
 */
void
nvram_baseinit(void)
{
	FIXME("nvram_baseinit : no-op\n");

}
