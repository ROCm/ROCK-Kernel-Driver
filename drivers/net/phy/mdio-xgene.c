/* Applied Micro X-Gene SoC MDIO Driver
 *
 * Copyright (c) 2016, Applied Micro Circuits Corporation
 * Author: Iyappan Subramanian <isubramanian@apm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/efi.h>
#include <linux/if_vlan.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/prefetch.h>
#include <linux/phy.h>
#include <net/ip.h>
#include "mdio-xgene.h"

static bool xgene_mdio_status;

static bool xgene_enet_rd_indirect(void __iomem *addr, void __iomem *rd,
				   void __iomem *cmd, void __iomem *cmd_done,
				   u32 rd_addr, u32 *rd_data)
{
	u32 done;
	u8 wait = 10;

	iowrite32(rd_addr, addr);
	iowrite32(XGENE_ENET_RD_CMD, cmd);

	/* wait for read command to complete */
	while (!(done = ioread32(cmd_done)) && wait--)
		udelay(1);

	if (!done)
		return false;

	*rd_data = ioread32(rd);
	iowrite32(0, cmd);

	return true;
}

static void xgene_enet_rd_mcx_mac(struct xgene_mdio_pdata *pdata,
				  u32 rd_addr, u32 *rd_data)
{
	void __iomem *addr, *rd, *cmd, *cmd_done;

	addr = pdata->mac_csr_addr + MAC_ADDR_REG_OFFSET;
	rd = pdata->mac_csr_addr + MAC_READ_REG_OFFSET;
	cmd = pdata->mac_csr_addr + MAC_COMMAND_REG_OFFSET;
	cmd_done = pdata->mac_csr_addr + MAC_COMMAND_DONE_REG_OFFSET;

	if (!xgene_enet_rd_indirect(addr, rd, cmd, cmd_done, rd_addr, rd_data))
		dev_err(pdata->dev, "MCX mac read failed, addr: 0x%04x\n",
			rd_addr);
}

static bool xgene_enet_wr_indirect(void __iomem *addr, void __iomem *wr,
				   void __iomem *cmd, void __iomem *cmd_done,
				   u32 wr_addr, u32 wr_data)
{
	u32 done;
	u8 wait = 10;

	iowrite32(wr_addr, addr);
	iowrite32(wr_data, wr);
	iowrite32(XGENE_ENET_WR_CMD, cmd);

	/* wait for write command to complete */
	while (!(done = ioread32(cmd_done)) && wait--)
		udelay(1);

	if (!done)
		return false;

	iowrite32(0, cmd);

	return true;
}

static void xgene_enet_wr_mcx_mac(struct xgene_mdio_pdata *pdata,
				  u32 wr_addr, u32 wr_data)
{
	void __iomem *addr, *wr, *cmd, *cmd_done;

	addr = pdata->mac_csr_addr + MAC_ADDR_REG_OFFSET;
	wr = pdata->mac_csr_addr + MAC_WRITE_REG_OFFSET;
	cmd = pdata->mac_csr_addr + MAC_COMMAND_REG_OFFSET;
	cmd_done = pdata->mac_csr_addr + MAC_COMMAND_DONE_REG_OFFSET;

	if (!xgene_enet_wr_indirect(addr, wr, cmd, cmd_done, wr_addr, wr_data))
		dev_err(pdata->dev, "MCX mac write failed, addr: 0x%04x\n",
			wr_addr);
}

static int xgene_mii_phy_read(struct xgene_mdio_pdata *pdata,
			      u8 phy_id, u32 reg)
{
	u32 data, done;
	u8 wait = 10;

	data = SET_VAL(PHY_ADDR, phy_id) | SET_VAL(REG_ADDR, reg);
	xgene_enet_wr_mcx_mac(pdata, MII_MGMT_ADDRESS_ADDR, data);
	xgene_enet_wr_mcx_mac(pdata, MII_MGMT_COMMAND_ADDR, READ_CYCLE_MASK);
	do {
		usleep_range(5, 10);
		xgene_enet_rd_mcx_mac(pdata, MII_MGMT_INDICATORS_ADDR, &done);
	} while ((done & BUSY_MASK) && wait--);

	if (done & BUSY_MASK) {
		dev_err(pdata->dev, "MII_MGMT read failed\n");
		return -EBUSY;
	}

	xgene_enet_rd_mcx_mac(pdata, MII_MGMT_STATUS_ADDR, &data);
	xgene_enet_wr_mcx_mac(pdata, MII_MGMT_COMMAND_ADDR, 0);

	return data;
}

static int xgene_mii_phy_write(struct xgene_mdio_pdata *pdata, int phy_id,
			       u32 reg, u16 data)
{
	u32 val, done;
	u8 wait = 10;

	val = SET_VAL(PHY_ADDR, phy_id) | SET_VAL(REG_ADDR, reg);
	xgene_enet_wr_mcx_mac(pdata, MII_MGMT_ADDRESS_ADDR, val);

	xgene_enet_wr_mcx_mac(pdata, MII_MGMT_CONTROL_ADDR, data);
	do {
		usleep_range(5, 10);
		xgene_enet_rd_mcx_mac(pdata, MII_MGMT_INDICATORS_ADDR, &done);
	} while ((done & BUSY_MASK) && wait--);

	if (done & BUSY_MASK) {
		dev_err(pdata->dev, "MII_MGMT write failed\n");
		return -EBUSY;
	}

	return 0;
}

static int xgene_mdio_rgmii_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct xgene_mdio_pdata *pdata = bus->priv;
	u32 val;

	val = xgene_mii_phy_read(pdata, mii_id, regnum);
	dev_dbg(pdata->dev, "MDIO read: bus=%d reg=%d val=0x%x\n",
		mii_id, regnum, val);

	return val;
}

static int xgene_mdio_rgmii_write(struct mii_bus *bus, int mii_id, int regnum,
				  u16 val)
{
	struct xgene_mdio_pdata *pdata = bus->priv;

	dev_dbg(pdata->dev, "MDIO write: bus=%d reg=%d val=0x%x\n",
		mii_id, regnum, val);

	return xgene_mii_phy_write(pdata, mii_id, regnum, val);
}

static u32 xgene_menet_rd_diag_csr(struct xgene_mdio_pdata *pdata,
				   u32 offset)
{
	return ioread32(pdata->diag_csr_addr + offset);
}

static void xgene_menet_wr_diag_csr(struct xgene_mdio_pdata *pdata,
				    u32 offset, u32 val)
{
	iowrite32(val, pdata->diag_csr_addr + offset);
}

static int xgene_enet_ecc_init(struct xgene_mdio_pdata *pdata)
{
	u32 data;
	u8 wait = 10;

	xgene_menet_wr_diag_csr(pdata, MENET_CFG_MEM_RAM_SHUTDOWN_ADDR, 0x0);
	do {
		usleep_range(100, 110);
		data = xgene_menet_rd_diag_csr(pdata, MENET_BLOCK_MEM_RDY_ADDR);
	} while ((data != 0xffffffff) && wait--);

	if (data != 0xffffffff) {
		dev_err(pdata->dev, "Failed to release memory from shutdown\n");
		return -ENODEV;
	}

	return 0;
}

static void xgene_gmac_reset(struct xgene_mdio_pdata *pdata)
{
	xgene_enet_wr_mcx_mac(pdata, MAC_CONFIG_1_ADDR, SOFT_RESET);
	xgene_enet_wr_mcx_mac(pdata, MAC_CONFIG_1_ADDR, 0);
}

static int xgene_mdio_reset(struct xgene_mdio_pdata *pdata)
{
	int ret;

	if (pdata->mdio_id == XGENE_MDIO_RGMII) {
		if (pdata->dev->of_node) {
			clk_prepare_enable(pdata->clk);
			clk_disable_unprepare(pdata->clk);
			clk_prepare_enable(pdata->clk);
		} else {
#ifdef CONFIG_ACPI
			acpi_evaluate_object(ACPI_HANDLE(pdata->dev),
					     "_RST", NULL, NULL);
#endif
		}
	} else {
#ifdef CONFIG_ACPI
		acpi_evaluate_object(ACPI_HANDLE(pdata->dev),
				     "_RST", NULL, NULL);
#endif
	}

	ret = xgene_enet_ecc_init(pdata);
	if (ret)
		return ret;
	xgene_gmac_reset(pdata);

	return 0;
}

static void xgene_enet_rd_mdio_csr(struct xgene_mdio_pdata  *pdata,
				   u32 offset, u32 *val)
{
	void __iomem *addr = pdata->mdio_csr_addr  + offset;

	*val = ioread32(addr);
}

static void xgene_enet_wr_mdio_csr(struct xgene_mdio_pdata *pdata,
				   u32 offset, u32 val)
{
	void __iomem *addr = pdata->mdio_csr_addr  + offset;

	iowrite32(val, addr);
}

static int xgene_xfimii_phy_write(struct xgene_mdio_pdata *pdata, int phy_id,
				  u32 reg, u16 data)
{
	int timeout = 100;
	u32 status, val;

	val = SET_VAL(HSTPHYADX, phy_id) | SET_VAL(HSTREGADX, reg) |
	      SET_VAL(HSTMIIMWRDAT, data);
	xgene_enet_wr_mdio_csr(pdata, MIIM_FIELD_ADDR, data);

	val = HSTLDCMD | SET_VAL(HSTMIIMCMD, MIIM_CMD_LEGACY_WRITE);
	xgene_enet_wr_mdio_csr(pdata, MIIM_COMMAND_ADDR, val);

	do {
		usleep_range(5, 10);
		xgene_enet_rd_mdio_csr(pdata, MIIM_INDICATOR_ADDR, &status);
	} while ((status & BUSY_MASK) && timeout--);

	xgene_enet_wr_mdio_csr(pdata, MIIM_COMMAND_ADDR, 0);
	return 0;
}

static int xgene_xfimii_phy_read(struct xgene_mdio_pdata *pdata,
				 u8 phy_id, u32 reg)
{
	u32 data, status, val;
	int timeout = 100;

	val = SET_VAL(HSTPHYADX, phy_id) | SET_VAL(HSTREGADX, reg);
	xgene_enet_wr_mdio_csr(pdata, MIIM_FIELD_ADDR, val);

	val = HSTLDCMD | SET_VAL(HSTMIIMCMD, MIIM_CMD_LEGACY_READ);
	xgene_enet_wr_mdio_csr(pdata, MIIM_COMMAND_ADDR, val);

	do {
		usleep_range(5, 10);
		xgene_enet_rd_mdio_csr(pdata, MIIM_INDICATOR_ADDR, &status);
	} while ((status & BUSY_MASK) && timeout--);

	if (status & BUSY_MASK) {
		dev_err(pdata->dev, "XGENET_MII_MGMT write failed\n");
		return -EBUSY;
	}
	xgene_enet_rd_mdio_csr(pdata, MIIMRD_FIELD_ADDR, &data);
	xgene_enet_wr_mdio_csr(pdata, MIIM_COMMAND_ADDR, 0);

	return data;
}

static int xgene_xfi_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct xgene_mdio_pdata  *pdata = bus->priv;
	u32 val;

	dev_dbg(pdata->dev, "MDIO read: bus=%d reg=%d val=0x%x\n",
		mii_id, regnum, val);
	val = xgene_xfimii_phy_read(pdata, mii_id, regnum);

	return val;
}

static int xgene_xfi_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
				u16 val)
{
	struct xgene_mdio_pdata *pdata = bus->priv;

	dev_dbg(pdata->dev, "MDIO write: bus=%d reg=%d val=0x%x\n",
		mii_id, regnum, val);

	return xgene_xfimii_phy_write(pdata, mii_id, regnum, val);
}

#ifdef CONFIG_ACPI
static acpi_status acpi_register_phy(acpi_handle handle, u32 lvl,
				     void *context, void **ret)
{
	struct mii_bus *mdio = context;
	struct acpi_device *adev;
	struct phy_device *phy_dev;
	const union acpi_object *obj;
	u32 phy_addr;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;

	if (acpi_dev_get_property(adev, "phy-channel", ACPI_TYPE_INTEGER, &obj))
		return AE_OK;
	phy_addr = obj->integer.value;

	phy_dev = get_phy_device(mdio, phy_addr, false);
	adev->driver_data = phy_dev;
	if (!phy_dev || IS_ERR(phy_dev))
		return AE_OK;

	if (phy_device_register(phy_dev))
		phy_device_free(phy_dev);

	return AE_OK;
}
#endif

bool xgene_mdio_probe_successful(void)
{
	return xgene_mdio_status;
}
EXPORT_SYMBOL(xgene_mdio_probe_successful);

static int xgene_mdio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mii_bus *mdio_bus;
	const struct of_device_id *of_id;
	struct resource *res;
	struct xgene_mdio_pdata *pdata;
	void __iomem *csr_addr;
	int mdio_id = 0, ret = 0;

	of_id = of_match_device(xgene_mdio_of_match, &pdev->dev);
	if (of_id) {
		mdio_id = (enum xgene_mdio_id)of_id->data;
	} else {
#ifdef CONFIG_ACPI
		const struct acpi_device_id *acpi_id;

		acpi_id = acpi_match_device(xgene_mdio_acpi_match, &pdev->dev);
		if (acpi_id)
			mdio_id = (enum xgene_mdio_id)acpi_id->driver_data;
#endif
	}

	if (!mdio_id)
		return -ENODEV;

	pdata = devm_kzalloc(dev, sizeof(struct xgene_mdio_pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	pdata->mdio_id = mdio_id;
	pdata->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Resource mac_ind_csr not defined\n");
		return -ENODEV;
	}

	csr_addr = devm_ioremap(dev, res->start, resource_size(res));
	if (!csr_addr) {
		dev_err(dev, "Unable to retrieve mac CSR region\n");
		return -ENOMEM;
	}
	pdata->mac_csr_addr = csr_addr;
	pdata->mdio_csr_addr = csr_addr + BLOCK_XG_MDIO_CSR_OFFSET;
	pdata->diag_csr_addr = csr_addr + BLOCK_DIAG_CSR_OFFSET;

	if (dev->of_node) {
		pdata->clk = devm_clk_get(dev, NULL);
		if (IS_ERR(pdata->clk)) {
			dev_err(dev, "Unable to retrieve clk\n");
			return -ENODEV;
		}
	}

	ret = xgene_mdio_reset(pdata);
	if (ret)
		return ret;

	mdio_bus = mdiobus_alloc();
	if (!mdio_bus)
		return -ENOMEM;

	mdio_bus->name = "APM X-Gene MDIO bus";
	snprintf(mdio_bus->id, MII_BUS_ID_SIZE, "%s-%d", "xgene-mii",
		 mdio_id);

	if (mdio_id == XGENE_MDIO_RGMII) {
		mdio_bus->read = xgene_mdio_rgmii_read;
		mdio_bus->write = xgene_mdio_rgmii_write;
	} else {
		mdio_bus->read = xgene_xfi_mdio_read;
		mdio_bus->write = xgene_xfi_mdio_write;
	}

	mdio_bus->priv = pdata;
	mdio_bus->parent = dev;
	platform_set_drvdata(pdev, mdio_bus);

	if (dev->of_node) {
		ret = of_mdiobus_register(mdio_bus, dev->of_node);
	} else {
#ifdef CONFIG_ACPI
		/* Mask out all PHYs from auto probing. */
		mdio_bus->phy_mask = ~0;
		ret = mdiobus_register(mdio_bus);
		if (ret)
			goto out;

		acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_HANDLE(dev), 1,
				    acpi_register_phy, NULL, mdio_bus, NULL);
#endif
	}

	if (ret)
		goto out;

	xgene_mdio_status = true;

	return 0;
out:
	if (mdio_bus->state == MDIOBUS_REGISTERED)
		mdiobus_unregister(mdio_bus);
	mdiobus_free(mdio_bus);

	return ret;
}

static int xgene_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *mdio_bus = platform_get_drvdata(pdev);

	mdiobus_unregister(mdio_bus);
	mdiobus_free(mdio_bus);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id xgene_mdio_of_match[] = {
	{
		.compatible = "apm,xgene-mdio-rgmii",
		.data = (void *)XGENE_MDIO_RGMII
	},
	{
		.compatible = "apm,xgene-mdio-xfi",
		.data = (void *)XGENE_MDIO_XFI},
	{},
};

MODULE_DEVICE_TABLE(of, xgene_mdio_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id xgene_mdio_acpi_match[] = {
	{ "APMC0D65", XGENE_MDIO_RGMII },
	{ "APMC0D66", XGENE_MDIO_XFI },
	{ }
};

MODULE_DEVICE_TABLE(acpi, xgene_mdio_acpi_match);
#endif

static struct platform_driver xgene_mdio_driver = {
	.driver = {
		.name = "xgene-mdio",
		.of_match_table = of_match_ptr(xgene_mdio_of_match),
		.acpi_match_table = ACPI_PTR(xgene_mdio_acpi_match),
	},
	.probe = xgene_mdio_probe,
	.remove = xgene_mdio_remove,
};

module_platform_driver(xgene_mdio_driver);

MODULE_DESCRIPTION("APM X-Gene SoC MDIO driver");
MODULE_AUTHOR("Iyappan Subramanian <isubramanian@apm.com>");
MODULE_LICENSE("GPL");
