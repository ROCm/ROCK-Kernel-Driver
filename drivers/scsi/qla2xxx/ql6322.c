/*
 * QLogic ISP6322 device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation (www.qlogic.com)
 *
 * Released under GPL v2.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "qla_def.h"

static char qla_driver_name[] = "qla6322";

extern unsigned char  fw2322flx_version[];
extern unsigned char  fw2322flx_version_str[];
extern unsigned short fw2322flx_addr01;
extern unsigned short fw2322flx_code01[];
extern unsigned short fw2322flx_length01;
extern unsigned long rseqflx_code_addr01;
extern unsigned short rseqflx_code01[];
extern unsigned short rseqflx_code_length01;
extern unsigned long xseqflx_code_addr01;
extern unsigned short xseqflx_code01[];
extern unsigned short xseqflx_code_length01;

static struct qla_fw_info qla_fw_tbl[] = {
	{
		.addressing	= FW_INFO_ADDR_NORMAL,
		.fwcode		= &fw2322flx_code01[0],
		.fwlen		= &fw2322flx_length01,
		.fwstart	= &fw2322flx_addr01,
	},
	{
		.addressing	= FW_INFO_ADDR_EXTENDED,
		.fwcode		= &rseqflx_code01[0],
		.fwlen		= &rseqflx_code_length01,
		.lfwstart	= &rseqflx_code_addr01,
	},
	{
		.addressing	= FW_INFO_ADDR_EXTENDED,
		.fwcode		= &xseqflx_code01[0],
		.fwlen		= &xseqflx_code_length01,
		.lfwstart	= &xseqflx_code_addr01,
	},
	{ FW_INFO_ADDR_NOMORE, },
};

static struct qla_board_info qla_board_tbl[] = {
	{
		.drv_name	= qla_driver_name,
		.isp_name	= "ISP6322",
		.fw_info	= qla_fw_tbl,
	},
};

static struct pci_device_id qla6322_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP6322,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[0],
	},
	{0, 0},
};
MODULE_DEVICE_TABLE(pci, qla6322_pci_tbl);

static int __devinit
qla6322_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return qla2x00_probe_one(pdev,
	    (struct qla_board_info *)id->driver_data);
}

static void __devexit
qla6322_remove_one(struct pci_dev *pdev)
{
	qla2x00_remove_one(pdev);
}

static struct pci_driver qla6322_pci_driver = {
	.name		= "qla6322",
	.id_table	= qla6322_pci_tbl,
	.probe		= qla6322_probe_one,
	.remove		= __devexit_p(qla6322_remove_one),
};

static int __init
qla6322_init(void)
{
	return pci_module_init(&qla6322_pci_driver);
}

static void __exit
qla6322_exit(void)
{
	pci_unregister_driver(&qla6322_pci_driver);
}

module_init(qla6322_init);
module_exit(qla6322_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic ISP6322 FC-SCSI Host Bus Adapter driver");
MODULE_LICENSE("GPL");
