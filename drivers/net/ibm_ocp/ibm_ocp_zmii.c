/*
 * ibm_ocp_zmii.c
 *
 *      Armin Kuster akuster@mvista.com
 *      Sept, 2001
 *
 * Copyright 2002 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/netdevice.h>

#include <asm/ocp.h>
#include <asm/io.h>
#include <asm/pgtable.h>

#include "ocp_zmii.h"
#include "ibm_ocp_enet.h"

static unsigned int zmii_enable[][4] = {
	{ZMII_SMII0, ZMII_RMII0, ZMII_MII0,
	 ~(ZMII_MDI1 | ZMII_MDI2 | ZMII_MDI3)},
	{ZMII_SMII1, ZMII_RMII1, ZMII_MII1,
	 ~(ZMII_MDI0 | ZMII_MDI2 | ZMII_MDI3)},
	{ZMII_SMII2, ZMII_RMII2, ZMII_MII2,
	 ~(ZMII_MDI0 | ZMII_MDI1 | ZMII_MDI3)},
	{ZMII_SMII3, ZMII_RMII3, ZMII_MII3, ~(ZMII_MDI0 | ZMII_MDI1 | ZMII_MDI2)}
};
static unsigned int mdi_enable[] =
    { ZMII_MDI0, ZMII_MDI1, ZMII_MDI2, ZMII_MDI3 };

static unsigned int zmii_speed = 0x0;
static unsigned int zmii_speed100[] = { ZMII_MII0_100MB, ZMII_MII1_100MB };

void
zmii_enable_port(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	zmii_t *zmiip = fep->zmii_base;
	int emac_num = (fep->ocpdev)->num;
	unsigned int mask;

	mask = in_be32(&zmiip->fer);

	mask &= zmii_enable[emac_num][MDI];	/* turn all non enable MDI's off */
	mask |= zmii_enable[emac_num][fep->zmii_mode]
	    | mdi_enable[emac_num];
	out_be32(&zmiip->fer, mask);

#ifdef EMAC_DEBUG
	printk("EMAC# %d zmiip 0x%x  = 0x%x\n", emac_num, zmiip,
	       zmiip->fer);
#endif
}

void
zmii_set_port_speed(int speed, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	int emac_num = (fep->ocpdev)->num;
	zmii_t *zmiip = fep->zmii_base;

	if (speed == 100)
		zmii_speed |= zmii_speed100[emac_num];

	out_be32(&zmiip->ssr, zmii_speed);
	return;
}

int
zmii_init(int mode, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	struct zmii_regs *zmiip;
	char *mode_name[] = { "SMII", "RMII", "MII" };

	/*
	 * FIXME: Need to handle multiple ZMII case that worked
	 * in the older code.  This is a kludge.
	 */
	fep->zmii_ocpdev = ocp_get_dev(OCP_FUNC_ZMII, 0);

	zmiip = (struct zmii_regs *)
	    __ioremap((fep->zmii_ocpdev)->paddr, sizeof (*zmiip), _PAGE_NO_CACHE);

	fep->zmii_base = zmiip;
	fep->zmii_mode = mode;
	if (mode == ZMII_AUTO) {
		if (zmiip->fer & (ZMII_MII0 | ZMII_MII1 | 
				  ZMII_MII2 | ZMII_MII3))
			fep->zmii_mode = MII;
		if (zmiip->fer & (ZMII_RMII0 | ZMII_RMII1 |
				  ZMII_RMII2 | ZMII_RMII3))
			fep->zmii_mode = RMII;
		if (zmiip->fer & (ZMII_SMII0 | ZMII_SMII1 |
				  ZMII_SMII2 | ZMII_SMII3))
			fep->zmii_mode = SMII;

		/* Failsafe: ZMII_AUTO is invalid index into the arrays,
		   so force SMII if all else fails. */

		if (fep->zmii_mode == ZMII_AUTO)
			fep->zmii_mode = SMII;
	}

	printk(KERN_NOTICE "IBM ZMII: %s mode\n",
			mode_name[fep->zmii_mode]);
	return (fep->zmii_mode);
}
