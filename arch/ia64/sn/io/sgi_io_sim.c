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
nasid_t console_nasid;
static char console_wid;
static char console_pcislot;

void
set_master_bridge_base(void)
{

        console_nasid = KL_CONFIG_CH_CONS_INFO(master_nasid)->nasid;
        console_wid = WIDGETID_GET(KL_CONFIG_CH_CONS_INFO(master_nasid)->memory_base);
        console_pcislot = KL_CONFIG_CH_CONS_INFO(master_nasid)->npci;
        master_bridge_base = (__psunsigned_t)NODE_SWIN_BASE(console_nasid,
                                                            console_wid);
	FIXME("WARNING: set_master_bridge_base: NON NASID 0 DOES NOT WORK\n");
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

/*
 * Routines provided by ml/SN/nvram.c
 */
void
nvram_baseinit(void)
{
	FIXME("nvram_baseinit : no-op\n");

}
