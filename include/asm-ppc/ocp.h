/*
 * ocp.h
 *
 *
 * 	Current Maintainer
 *      Armin Kuster akuster@pacbell.net
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

#ifdef __KERNEL__
#ifndef __OCP_H__
#define __OCP_H__

#include <linux/list.h>
#include <linux/config.h>
#include <linux/device.h>
#include <linux/errno.h>

#include <asm/ocp_ids.h>
#include <asm/mmu.h>		/* For phys_addr_t */
#undef DEBUG
/* #define DEBUG*/

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

#define OCP_IRQ_NA	-1	/* used when ocp device does not have an irq */
#define OCP_IRQ_MUL	-2	/* used for ocp devices with multiple irqs */
#define OCP_NULL_TYPE	0	/* used to mark end of list */
#define OCP_DEV_NA	-1
#define OCP_CPM_NA	0	/* No Clock or Power Management avaliable */

#define OCP_ANY_ID	(~0)


extern struct list_head ocp_root_buses;
extern struct list_head ocp_devices;

struct ocp_device_id {
	unsigned int vendor, device;		/* Vendor and device ID or PCI_ANY_ID */
	char name[16];
	char desc[50];
	unsigned long driver_data;		/* Data private to the driver */
};

struct func_info {
	char name[16];
	char desc[50];
};

struct ocp_def {
	unsigned int vendor;
	unsigned int device;
	phys_addr_t paddr;
	int irq;
	unsigned long pm;
};


/* Struct for single ocp device managment */
struct ocp_device {
	struct list_head global_list;
	unsigned int	num;		/* instance of device */
	char		name[80];	/* device name */
	unsigned int vendor;
	unsigned int device;
	phys_addr_t paddr;
	int irq;
	unsigned long pm;
	void *ocpdev;		/* driver data for this device */
	struct ocp_driver *driver;
	u32 current_state;	/* Current operating state. In ACPI-speak,
				   this is D0-D3, D0 being fully functional,
				   and D3 being off. */
	struct device dev;
};

struct ocp_driver {
	struct list_head node;
	char *name;
	const struct ocp_device_id *id_table;	/* NULL if wants all devices */
	int  (*probe)  (struct ocp_device *dev);	/* New device inserted */
	void (*remove) (struct ocp_device *dev);	/* Device removed (NULL if not a hot-plug capable driver) */
	int  (*save_state) (struct ocp_device *dev, u32 state);    /* Save Device Context */
	int  (*suspend) (struct ocp_device *dev, u32 state);	/* Device suspended */
	int  (*resume) (struct ocp_device *dev);	                /* Device woken up */
	int  (*enable_wake) (struct ocp_device *dev, u32 state, int enable);   /* Enable wake event */
	struct device_driver driver;
};

#define	to_ocp_dev(n) container_of(n, struct ocp_device, dev)
#define	to_ocp_driver(n) container_of(n, struct ocp_driver, driver)

extern int ocp_register_driver(struct ocp_driver *drv);
extern void ocp_unregister_driver(struct ocp_driver *drv);

#define ocp_dev_g(n) list_entry(n, struct ocp_device, global_list)

#define ocp_for_each_dev(dev) \
	for(dev = ocp_dev_g(ocp_devices.next); dev != ocp_dev_g(&ocp_devices); dev = ocp_dev_g(dev->global_list.next))

/* Similar to the helpers above, these manipulate per-ocp_dev
 * driver-specific data.  Currently stored as ocp_dev::ocpdev,
 * a void pointer, but it is not present on older kernels.
 */
static inline void *
ocp_get_drvdata(struct ocp_device *pdev)
{
	return pdev->ocpdev;
}

static inline void
ocp_set_drvdata(struct ocp_device *pdev, void *data)
{
	pdev->ocpdev = data;
}

/*
 * a helper function which helps ensure correct pci_driver
 * setup and cleanup for commonly-encountered hotplug/modular cases
 *
 * This MUST stay in a header, as it checks for -DMODULE
 */
static inline int ocp_module_init(struct ocp_driver *drv)
{
	int rc = ocp_register_driver(drv);

	if (rc > 0)
		return 0;

	/* iff CONFIG_HOTPLUG and built into kernel, we should
	 * leave the driver around for future hotplug events.
	 * For the module case, a hotplug daemon of some sort
	 * should load a module in response to an insert event. */
#if defined(CONFIG_HOTPLUG) && !defined(MODULE)
	if (rc == 0)
		return 0;
#else
	if (rc == 0)
		rc = -ENODEV;		
#endif

	/* if we get here, we need to clean up pci driver instance
	 * and return some sort of error */
	ocp_unregister_driver (drv);
	
	return rc;
}

#if defined (CONFIG_PM)
/*
 * This is right for the IBM 405 and 440 but will need to be
 * generalized if the OCP stuff gets used on other processors.
 */
static inline void
ocp_force_power_off(struct ocp_device *odev)
{
	mtdcr(DCRN_CPMFR, mfdcr(DCRN_CPMFR) | odev->pm);
}

static inline void
ocp_force_power_on(struct ocp_device *odev)
{
	mtdcr(DCRN_CPMFR, mfdcr(DCRN_CPMFR) & ~odev->pm);
}
#else
#define ocp_force_power_off(x)	(void)(x)
#define ocp_force_power_on(x)	(void)(x)
#endif

extern void ocp_init(void);
extern struct bus_type ocp_bus_type;
extern struct ocp_device *ocp_get_dev(unsigned int device, int index);
extern unsigned int ocp_get_num(unsigned int device);

extern int ocp_generic_suspend(struct ocp_device *pdev, u32 state);
extern int ocp_generic_resume(struct ocp_device *pdev);

#endif				/* __OCP_H__ */
#endif				/* __KERNEL__ */
