/*
 * FILE NAME ibm_ocp_gpio.c
 *
 * BRIEF MODULE DESCRIPTION
 *  API for IBM PowerPC 4xx GPIO device.
 *  Driver for IBM PowerPC 4xx GPIO device.
 *
 *  Armin Kuster akuster@pacbell.net
 *  Sept, 2001
 *
 *  Orignial driver
 *  Author: MontaVista Software, Inc.  <source@mvista.com>
 *          Frank Rowand <frank_rowand@mvista.com>
 *          Debbie Chu   <debbie_chu@mvista.com>
 *
 * Copyright 2000,2001,2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE	LIABLE FOR ANY   DIRECT, INDIRECT,
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

#define VUFX "09.06.02"

#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/pm.h>
#include <asm/ibm_ocp_gpio.h>
#include <asm/ocp.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/machdep.h>


typedef struct gpio_regs {
	u32 or;
	u32 tcr;
	u32 pad[4];
	u32 odr;
	u32 ir;
} gpio_t;

static struct gpio_regs *gpiop;

int
ocp_gpio_config(__u32 device, __u32 mask, __u32 data)
{
	u32 cfg_reg;

	if (device != 0)
		return -ENXIO;

#ifdef CONFIG_40x
	/*
	 * PPC405 uses CPC0_CR0 to select multiplexed GPIO pins.
	 */
	cfg_reg = mfdcr(DCRN_CHCR0);
	cfg_reg = (cfg_reg & ~mask) | (data & mask);
	mtdcr(DCRN_CHCR0, cfg_reg);
#elif CONFIG_44x
	/*
	 * PPC440 uses CPC0_GPIO to select multiplexed GPIO pins.
	 */
	cfg_reg = mfdcr(DCRN_CPC0_GPIO);
	cfg_reg = (cfg_reg & ~mask) | (data & mask);
	mtdcr(DCRN_CPC0_GPIO, cfg_reg);
#else
#error This driver is only supported on PPC40x and PPC440 CPUs
#endif

	return 0;
}

int
ocp_gpio_tristate(__u32 device, __u32 mask, __u32 data)
{
	if (device != 0)
		return -ENXIO;
	gpiop->tcr = (gpiop->tcr & ~mask) | (data & mask);
	return 0;
}

int
ocp_gpio_open_drain(__u32 device, __u32 mask, __u32 data)
{
	if (device != 0)
		return -ENXIO;
	gpiop->odr = (gpiop->odr & ~mask) | (data & mask);

	return 0;
}

int
ocp_gpio_in(__u32 device, __u32 mask, volatile __u32 * data)
{
	if (device != 0)
		return -ENXIO;
	gpiop->tcr = gpiop->tcr & ~mask;
	eieio();

	/*
	   ** If the previous state was OUT, and gpiop->ir is read once, then the
	   ** data that was being OUTput will be read.  One way to get the right
	   ** data is to read gpiop->ir twice.
	 */

	*data = gpiop->ir;
	*data = gpiop->ir & mask;
	eieio();
	return 0;
}

int
ocp_gpio_out(__u32 device, __u32 mask, __u32 data)
{
	if (device != 0)
		return -ENXIO;
	gpiop->or = (gpiop->or & ~mask) | (data & mask);
	eieio();
	gpiop->tcr = gpiop->tcr | mask;
	eieio();
	return 0;
}

static int
ocp_gpio_ioctl(struct inode *inode, struct file *file,
	       unsigned int cmd, unsigned long arg)
{
	static struct ocp_gpio_ioctl_data ioctl_data;
	int status;

	switch (cmd) {
	case IBMGPIO_IN:
		if (copy_from_user(&ioctl_data,
				   (struct ocp_gpio_ioctl_data *) arg,
				   sizeof (ioctl_data))) {
			return -EFAULT;
		}

		status = ocp_gpio_in(ioctl_data.device,
				     ioctl_data.mask, &ioctl_data.data);
		if (status != 0)
			return status;

		if (copy_to_user((struct ocp_gpio_ioctl_data *) arg,
				 &ioctl_data, sizeof (ioctl_data))) {
			return -EFAULT;
		}

		break;

	case IBMGPIO_OUT:
		if (copy_from_user(&ioctl_data,
				   (struct ocp_gpio_ioctl_data *) arg,
				   sizeof (ioctl_data))) {
			return -EFAULT;
		}

		return ocp_gpio_out(ioctl_data.device,
				    ioctl_data.mask, ioctl_data.data);

		break;

	case IBMGPIO_OPEN_DRAIN:
		if (copy_from_user(&ioctl_data,
				   (struct ocp_gpio_ioctl_data *) arg,
				   sizeof (ioctl_data))) {
			return -EFAULT;
		}

		return ocp_gpio_open_drain(ioctl_data.device,
					   ioctl_data.mask, ioctl_data.data);

		break;

	case IBMGPIO_TRISTATE:
		if (copy_from_user(&ioctl_data,
				   (struct ocp_gpio_ioctl_data *) arg,
				   sizeof (ioctl_data)))
			return -EFAULT;

		return ocp_gpio_tristate(ioctl_data.device,
					 ioctl_data.mask, ioctl_data.data);

		break;

	case IBMGPIO_CFG:
		if (copy_from_user(&ioctl_data,
				   (struct ocp_gpio_ioctl_data *) arg,
				   sizeof (ioctl_data)))
			return -EFAULT;

		return ocp_gpio_config(ioctl_data.device,
				ioctl_data.mask, ioctl_data.data);

		break;

	default:
		return -ENOIOCTLCMD;

	}
	return 0;
}

static struct file_operations ocp_gpio_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= ocp_gpio_ioctl,
};

struct miscdevice ocp_gpio_miscdev = {
	.minor		= 185,	/*GPIO_MINOR; */
	.name		= "IBM4xx ocp gpio",
	.fops		= &ocp_gpio_fops,
};

static int __devinit ocp_gpio_probe(struct ocp_device *pdev)
{
	printk("IBM gpio driver version %s\n", VUFX);

	misc_register(&ocp_gpio_miscdev);	/*ocp_gpio_miscdev); */

	pdev->vaddr = ioremap(pdev->paddr, sizeof (struct gpio_regs));
	gpiop = (struct gpio_regs *)pdev->vaddr;
	printk("GPIO #%d at 0x%lx\n", pdev->num, (unsigned long) gpiop);

	ocp_force_power_on(pdev);
	return 1;
}

static void __devexit ocp_gpio_remove_one (struct ocp_device *pdev)
{
	ocp_force_power_off(pdev);
	misc_deregister(&ocp_gpio_miscdev);
}

static struct ocp_device_id ocp_gpio_id_tbl[] __devinitdata = {
	{OCP_VENDOR_IBM,OCP_FUNC_GPIO},
	{0,}
};

MODULE_DEVICE_TABLE(ocp,ocp_gpio_id_tbl );

static struct ocp_driver ocp_gpio_driver = {
	.name		= "ocp_gpio",
	.id_table	= ocp_gpio_id_tbl,
	.probe		= ocp_gpio_probe,
	.remove		= __devexit_p(ocp_gpio_remove_one),
#if defined(CONFIG_PM)
	.suspend	= ocp_generic_suspend,
	.resume		= ocp_generic_resume,
#endif /* CONFIG_PM */
};

static int __init
ocp_gpio_init(void)
{
	printk("IBM gpio driver version %s\n", VUFX);
	return ocp_module_init(&ocp_gpio_driver);
}

void __exit
ocp_gpio_fini(void)
{
	ocp_unregister_driver(&ocp_gpio_driver);
}

module_init(ocp_gpio_init);
module_exit(ocp_gpio_fini);

EXPORT_SYMBOL(ocp_gpio_tristate);
EXPORT_SYMBOL(ocp_gpio_open_drain);
EXPORT_SYMBOL(ocp_gpio_in);
EXPORT_SYMBOL(ocp_gpio_out);

