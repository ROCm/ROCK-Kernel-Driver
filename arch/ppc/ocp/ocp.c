/*
 * ocp.c
 *
 *	The is drived from pci.c
 *
 * 	Current Maintainer
 *      Armin Kuster akuster@dslextreme.com
 *      Jan, 2002
 *
 *
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

#include <linux/list.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/ocp.h>
#include <asm/errno.h>

/**
 * ocp_get_num - This determines how many OCP devices of a given
 * device are registered
 * @device: OCP device such as HOST, PCI, GPT, UART, OPB, IIC, GPIO, EMAC, ZMII,
 *
 * The routine returns the number that devices which is registered
 */
unsigned int ocp_get_num(unsigned int device)
{
	unsigned int count = 0;
	struct ocp_device *ocp;
	struct list_head *ocp_l;

	list_for_each(ocp_l, &ocp_devices) {
		ocp = list_entry(ocp_l, struct ocp_device, global_list);
		if (device == ocp->device)
			count++;
	}
	return count;
}

/**
 * ocp_get_dev - get ocp driver pointer for ocp device and instance of it
 * @device: OCP device such as PCI, GPT, UART, OPB, IIC, GPIO, EMAC, ZMII
 * @dev_num: ocp device number whos paddr you want
 *
 * The routine returns ocp device pointer
 * in list based on device and instance of that device
 *
 */
struct ocp_device *
ocp_get_dev(unsigned int device, int dev_num)
{
	struct ocp_device *ocp;
	struct list_head *ocp_l;
	int count = 0;

	list_for_each(ocp_l, &ocp_devices) {
		ocp = list_entry(ocp_l, struct ocp_device, global_list);
		if (device == ocp->device) {
			if (dev_num == count)
				return ocp;
			count++;
		}
	}
	return NULL;
}

EXPORT_SYMBOL(ocp_get_dev);
EXPORT_SYMBOL(ocp_get_num);

#ifdef CONFIG_PM
int ocp_generic_suspend(struct ocp_device *pdev, u32 state)
{
	ocp_force_power_off(pdev);
	return 0;
}

int ocp_generic_resume(struct ocp_device *pdev)
{
	ocp_force_power_on(pdev);
}

EXPORT_SYMBOL(ocp_generic_suspend);
EXPORT_SYMBOL(ocp_generic_resume);
#endif /* CONFIG_PM */
