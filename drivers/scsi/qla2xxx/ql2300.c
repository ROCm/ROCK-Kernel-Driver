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

/* 2300/2310/2312 Firmware. */
extern unsigned char  fw2300ipx_version[];
extern unsigned char  fw2300ipx_version_str[];
extern unsigned short fw2300ipx_addr01;
extern unsigned short fw2300ipx_code01[];
extern unsigned short fw2300ipx_length01;
/* 2322 Firmware. */
extern unsigned char  fw2322ipx_version[];
extern unsigned char  fw2322ipx_version_str[];
extern unsigned short fw2322ipx_addr01;
extern unsigned short fw2322ipx_code01[];
extern unsigned short fw2322ipx_length01;
extern unsigned long rseqipx_code_addr01;
extern unsigned short rseqipx_code01[];
extern unsigned short rseqipx_code_length01;
extern unsigned long xseqipx_code_addr01;
extern unsigned short xseqipx_code01[];
extern unsigned short xseqipx_code_length01;
/* 6312 Firmware. */
extern unsigned char  fw2300flx_version[];
extern unsigned char  fw2300flx_version_str[];
extern unsigned short fw2300flx_addr01;
extern unsigned short fw2300flx_code01[];
extern unsigned short fw2300flx_length01;
/* 6322 Firmware. */
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
	/* Start of 23xx firmware list */
	{
		.addressing	= FW_INFO_ADDR_NORMAL,
		.fwcode		= &fw2300ipx_code01[0],
		.fwlen		= &fw2300ipx_length01,
		.fwstart	= &fw2300ipx_addr01,
	},
	{ FW_INFO_ADDR_NOMORE, },

	/* Start of 232x firmware list */
	{
		.addressing	= FW_INFO_ADDR_NORMAL,
		.fwcode		= &fw2322ipx_code01[0],
		.fwlen		= &fw2322ipx_length01,
		.fwstart	= &fw2322ipx_addr01,
	},
	{
		.addressing	= FW_INFO_ADDR_EXTENDED,
		.fwcode		= &rseqipx_code01[0],
		.fwlen		= &rseqipx_code_length01,
		.lfwstart	= &rseqipx_code_addr01,
	},
	{
		.addressing	= FW_INFO_ADDR_EXTENDED,
		.fwcode		= &xseqipx_code01[0],
		.fwlen		= &xseqipx_code_length01,
		.lfwstart	= &xseqipx_code_addr01,
	},
	{ FW_INFO_ADDR_NOMORE, },

	/* Start of 631x firmware list */
	{
		.addressing	= FW_INFO_ADDR_NORMAL,
		.fwcode		= &fw2300flx_code01[0],
		.fwlen		= &fw2300flx_length01,
		.fwstart	= &fw2300flx_addr01,
	},
	{ FW_INFO_ADDR_NOMORE, },

	/* Start of 632x firmware list */
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
		.isp_name	= "ISP2300",
		.fw_info	= qla_fw_tbl,
	},
	{
		.drv_name	= qla_driver_name,
		.isp_name	= "ISP2312",
		.fw_info	= qla_fw_tbl,
	},
	{
		.drv_name	= qla_driver_name,
		.isp_name	= "ISP2322",
		.fw_info	= &qla_fw_tbl[2],
	},
	{
		.drv_name	= qla_driver_name,
		.isp_name	= "ISP6312",
		.fw_info	= &qla_fw_tbl[6],
	},
	{
		.drv_name	= qla_driver_name,
		.isp_name	= "ISP6322",
		.fw_info	= &qla_fw_tbl[8],
	},
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
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP2322,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[2],
	},
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP6312,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[3],
	},
	{
		.vendor		= PCI_VENDOR_ID_QLOGIC,
		.device		= PCI_DEVICE_ID_QLOGIC_ISP6322,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long)&qla_board_tbl[4],
	},
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
