/*
 * QLogic ISP23XX device driver for Linux 2.6.x
 * Copyright (C) 2003 Christoph Hellwig.
 * Copyright (C) 2003 QLogic Corporation (www.qlogic.com)
 *
 * Released under GPL v2.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "qla_os.h"
#include "qla_def.h"

static char qla_driver_name[] = "qla2300";

extern unsigned char  fw2300tpx_version[];
extern unsigned char  fw2300tpx_version_str[];
extern unsigned short fw2300tpx_addr01;
extern unsigned short fw2300tpx_code01[];
extern unsigned short fw2300tpx_length01;

extern unsigned char  fw2322tpx_version[];
extern unsigned char  fw2322tpx_version_str[];
extern unsigned short fw2322tpx_addr01;
extern unsigned short fw2322tpx_code01[];
extern unsigned short fw2322tpx_length01;
extern unsigned long rseqtpx_code_addr01;
extern unsigned short rseqtpx_code01[];
extern unsigned short rseqtpx_code_length01;
extern unsigned long xseqtpx_code_addr01;
extern unsigned short xseqtpx_code01[];
extern unsigned short xseqtpx_code_length01;

static struct qla_fw_info qla_fw_tbl[] = {
	{
		.addressing	= FW_INFO_ADDR_NORMAL,
		.fwcode		= &fw2300tpx_code01[0],
		.fwlen		= &fw2300tpx_length01,
		.fwstart	= &fw2300tpx_addr01,
	},
#if defined(ISP2322)
	/* End of 23xx firmware list */
	{ FW_INFO_ADDR_NOMORE, },

	/* Start of 232x firmware list */
	{
		.addressing	= FW_INFO_ADDR_NORMAL,
		.fwcode		= &fw2322tpx_code01[0],
		.fwlen		= &fw2322tpx_length01,
		.fwstart	= &fw2322tpx_addr01,
	},
	{
		.addressing	= FW_INFO_ADDR_EXTENDED,
		.fwcode		= &rseqtpx_code01[0],
		.fwlen		= &rseqtpx_code_length01,
		.lfwstart	= &rseqtpx_code_addr01,
	},
	{
		.addressing	= FW_INFO_ADDR_EXTENDED,
		.fwcode		= &xseqtpx_code01[0],
		.fwlen		= &xseqtpx_code_length01,
		.lfwstart	= &xseqtpx_code_addr01,
	},
#endif
	{ FW_INFO_ADDR_NOMORE, },
};

static struct qla_board_info qla_board_tbl[] = {
	{
		.drv_name	= qla_driver_name,

		.isp_name	= "ISP2300",
		.fw_info	= qla_fw_tbl,
	},

	{
		.drv_name	= qla_driver_name,

		.isp_name	= "ISP2312",
		.fw_info	= qla_fw_tbl,
	},
#if defined(ISP2322)
	{
		.drv_name	= qla_driver_name,

		.isp_name	= "ISP2322",
		.fw_info	= &qla_fw_tbl[2],
	},
#endif
};

static struct pci_device_id qla2300_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP2300,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[0],
	},

	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP2312,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[1],
	},

#if defined(ISP2322)
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP2322,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[2],
	},
#endif
	{0, 0},
};
MODULE_DEVICE_TABLE(pci, qla2300_pci_tbl);

static int __devinit
qla2300_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return qla2x00_probe_one(pdev,
	    (struct qla_board_info *)id->driver_data);
}

static void __devexit
qla2300_remove_one(struct pci_dev *pdev)
{
	qla2x00_remove_one(pdev);
}

static struct pci_driver qla2300_pci_driver = {
	.name		= "qla2300",
	.id_table	= qla2300_pci_tbl,
	.probe		= qla2300_probe_one,
	.remove		= __devexit_p(qla2300_remove_one),
};

static int __init
qla2300_init(void)
{
	return pci_module_init(&qla2300_pci_driver);
}

static void __exit
qla2300_exit(void)
{
	pci_unregister_driver(&qla2300_pci_driver);
}

module_init(qla2300_init);
module_exit(qla2300_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic ISP23xx FC-SCSI Host Bus Adapter driver");
MODULE_LICENSE("GPL");
