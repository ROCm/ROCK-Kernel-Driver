/*
 * libata-acpi.c
 * Provides ACPI support for PATA/SATA.
 *
 * Copyright (C) 2005 Intel Corp.
 * Copyright (C) 2005 Randy Dunlap
 */

#include <linux/ata.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <acpi/acpi.h>
#include "scsi.h"
#include <linux/libata.h>
#include <linux/pci.h>
#include "libata.h"

#include <acpi/acpi_bus.h>
#include <acpi/acnames.h>
#include <acpi/acnamesp.h>
#include <acpi/acparser.h>
#include <acpi/acexcep.h>
#include <acpi/acmacros.h>
#include <acpi/actypes.h>

#define SATA_ROOT_PORT(x)	(((x) >> 16) & 0xffff)
#define SATA_PORT_NUMBER(x)	((x) & 0xffff)	/* or NO_PORT_MULT */
#define NO_PORT_MULT		0xffff
#define SATA_ADR_RSVD		0xffffffff

#define REGS_PER_GTF		7
struct taskfile_array {
	u8	tfa[REGS_PER_GTF];	/* regs. 0x1f1 - 0x1f7 */
};

struct GTM_buffer {
	__u32	PIO_speed0;
	__u32	DMA_speed0;
	__u32	PIO_speed1;
	__u32	DMA_speed1;
	__u32	GTM_flags;
};

#define DEBUGGING	1
/* note: adds function name and KERN_DEBUG */
#ifdef DEBUGGING
#define DEBPRINT(fmt, args...)	\
		printk(KERN_DEBUG "%s: " fmt, __FUNCTION__, ## args)
#else
#define DEBPRINT(fmt, args...)	do {} while (0)
#endif	/* DEBUGGING */

/**
 * sata_get_dev_handle - finds acpi_handle and PCI device.function
 * @dev: device to locate
 * @handle: returned acpi_handle for @dev
 * @pcidevfn: return PCI device.func for @dev
 *
 * This function is somewhat SATA-specific.  Or at least the
 * IDE and SCSI versions of this function are different,
 * so it's not entirely generic code.
 *
 * Returns 0 on success, <0 on error.
 */
static int sata_get_dev_handle(struct device *dev, acpi_handle *handle,
					acpi_integer *pcidevfn)
{
	struct pci_dev	*pci_dev;
	acpi_integer	addr;

	pci_dev = to_pci_dev(dev);	/* NOTE: PCI-specific */
	/* Please refer to the ACPI spec for the syntax of _ADR. */
	addr = (PCI_SLOT(pci_dev->devfn) << 16) | PCI_FUNC(pci_dev->devfn);
	*pcidevfn = addr;
	*handle = acpi_get_child(DEVICE_ACPI_HANDLE(dev->parent), addr);
	printk(KERN_DEBUG "%s: SATA dev addr=0x%llx, handle=0x%p\n",
		__FUNCTION__, (unsigned long long)addr, *handle);
	if (!*handle)
		return -ENODEV;
	return 0;
}

/**
 * pata_get_dev_handle - finds acpi_handle and PCI device.function
 * @dev: device to locate
 * @handle: returned acpi_handle for @dev
 * @pcidevfn: return PCI device.func for @dev
 *
 * The PATA and SATA versions of this function are different.
 *
 * Returns 0 on success, <0 on error.
 */
static int pata_get_dev_handle(struct device *dev, acpi_handle *handle,
					acpi_integer *pcidevfn)
{
	unsigned int domain, bus, devnum, func;
	acpi_integer addr;
	acpi_handle dev_handle, parent_handle;
	int scanned;
	struct acpi_buffer buffer = {.length = ACPI_ALLOCATE_BUFFER,
					.pointer = NULL};
	acpi_status status;
	struct acpi_device_info	*dinfo = NULL;
	int ret = -ENODEV;

	printk(KERN_DEBUG "%s: enter: dev->bus_id='%s'\n",
		__FUNCTION__, dev->bus_id);
	if ((scanned = sscanf(dev->bus_id, "%x:%x:%x.%x",
			&domain, &bus, &devnum, &func)) != 4) {
		printk(KERN_DEBUG "%s: sscanf ret. %d\n",
			__FUNCTION__, scanned);
		goto err;
	}

	dev_handle = DEVICE_ACPI_HANDLE(dev);
	parent_handle = DEVICE_ACPI_HANDLE(dev->parent);

	status = acpi_get_object_info(parent_handle, &buffer);
	if (ACPI_FAILURE(status)) {
		printk(KERN_DEBUG "%s: get_object_info for parent failed\n",
			__FUNCTION__);
		goto err;
	}
	dinfo = buffer.pointer;
	if (dinfo && (dinfo->valid & ACPI_VALID_ADR) &&
	    dinfo->address == bus) {
		/* ACPI spec for _ADR for PCI bus: */
		addr = (acpi_integer)(devnum << 16 | func);
		*pcidevfn = addr;
		*handle = dev_handle;
	} else {
		printk(KERN_DEBUG "%s: get_object_info for parent has wrong "
			" bus: %llu, should be %d\n",
			__FUNCTION__,
			dinfo ? (unsigned long long)dinfo->address : -1ULL,
			bus);
		goto err;
	}

	printk(KERN_DEBUG "%s: dev_handle: 0x%p, parent_handle: 0x%p\n",
		__FUNCTION__, dev_handle, parent_handle);
	printk(KERN_DEBUG
		"%s: for dev=0x%x.%x, addr=0x%llx, parent=0x%p, *handle=0x%p\n",
		__FUNCTION__, devnum, func, (unsigned long long)addr,
		dev->parent, *handle);
	if (!*handle)
		goto err;
	ret = 0;
err:
	acpi_os_free(dinfo);
	return ret;
}

struct walk_info {		/* can be trimmed some */
	struct device	*dev;
	struct acpi_device *adev;
	acpi_handle	handle;
	acpi_integer	pcidevfn;
	unsigned int	drivenum;
	acpi_handle	obj_handle;
	struct ata_port *ataport;
	struct ata_device *atadev;
	u32		sata_adr;
	int		status;
	char		basepath[ACPI_PATHNAME_MAX];
	int		basepath_len;
};

static acpi_status get_devices(acpi_handle handle,
				u32 level, void *context, void **return_value)
{
	acpi_status		status;
	struct walk_info	*winfo = context;
	struct acpi_buffer	namebuf = {ACPI_ALLOCATE_BUFFER, NULL};
	char			*pathname;
	struct acpi_buffer	buffer;
	struct acpi_device_info	*dinfo;

	status = acpi_get_name(handle, ACPI_FULL_PATHNAME, &namebuf);
	if (status)
		goto ret;
	pathname = namebuf.pointer;

	buffer.length = ACPI_ALLOCATE_BUFFER;
	buffer.pointer = NULL;
	status = acpi_get_object_info(handle, &buffer);

	if (ACPI_SUCCESS(status)) {
		dinfo = buffer.pointer;

		/* find full device path name for pcidevfn */
		if (dinfo && (dinfo->valid & ACPI_VALID_ADR) &&
		    dinfo->address == winfo->pcidevfn) {
			if (ata_msg_probe(winfo->ataport))
				printk(KERN_DEBUG
					":%s: matches pcidevfn (0x%llx)\n",
					pathname, winfo->pcidevfn);
			strlcpy(winfo->basepath, pathname,
				sizeof(winfo->basepath));
			winfo->basepath_len = strlen(pathname);
			goto out;
		}

		/* if basepath is not yet known, ignore this object */
		if (!winfo->basepath_len)
			goto out;

		/* if this object is in scope of basepath, maybe use it */
		if (strncmp(pathname, winfo->basepath,
		    winfo->basepath_len) == 0) {
			if (!(dinfo->valid & ACPI_VALID_ADR))
				goto out;
			if (ata_msg_probe(winfo->ataport))
				printk(KERN_DEBUG "GOT ONE: (%s) "
					"root_port = 0x%llx, port_num = 0x%llx\n",
					pathname,
					SATA_ROOT_PORT(dinfo->address),
					SATA_PORT_NUMBER(dinfo->address));
			/* heuristics: */
			if (SATA_PORT_NUMBER(dinfo->address) != NO_PORT_MULT)
				if (ata_msg_probe(winfo->ataport))
					printk(KERN_DEBUG
						"warning: don't know how to handle SATA port multiplier\n");
			if (SATA_ROOT_PORT(dinfo->address) ==
				winfo->ataport->port_no &&
			    SATA_PORT_NUMBER(dinfo->address) == NO_PORT_MULT) {
				if (ata_msg_probe(winfo->ataport))
					printk(KERN_DEBUG
						"THIS ^^^^^ is the requested SATA drive (handle = 0x%p)\n",
						handle);
				winfo->sata_adr = dinfo->address;
				winfo->obj_handle = handle;
			}
		}
out:
		acpi_os_free(dinfo);
	}
	acpi_os_free(pathname);

ret:
	return status;
}

/* Get the SATA drive _ADR object. */
static int get_sata_adr(struct device *dev, acpi_handle handle,
			acpi_integer pcidevfn, unsigned int drive,
			struct ata_port *ap,
			struct ata_device *atadev, u32 *dev_adr)
{
	acpi_status	status;
	struct walk_info *winfo;
	int		err = -ENOMEM;

	winfo = kzalloc(sizeof(struct walk_info), GFP_KERNEL);
	if (!winfo)
		goto out;

	winfo->dev = dev;
	winfo->atadev = atadev;
	winfo->ataport = ap;
	if (acpi_bus_get_device(handle, &winfo->adev) < 0)
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "acpi_bus_get_device failed\n");
	winfo->handle = handle;
	winfo->pcidevfn = pcidevfn;
	winfo->drivenum = drive;

	status = acpi_get_devices(NULL, get_devices, winfo, NULL);
	if (ACPI_FAILURE(status)) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "%s: acpi_get_devices failed\n",
				__FUNCTION__);
		err = -ENODEV;
	} else {
		*dev_adr = winfo->sata_adr;
		atadev->obj_handle = winfo->obj_handle;
		err = 0;
	}
	kfree(winfo);
out:
	return err;
}

/**
 * ata_acpi_push_id - send Identify data to a drive
 * @ap: the ata_port for the drive
 * @ix: drive index
 *
 * _SDD ACPI object:  for SATA mode only.
 * Must be after Identify (Packet) Device -- uses its data.
 */
int ata_acpi_push_id(struct ata_port *ap, unsigned int ix)
{
	acpi_handle			handle;
	acpi_integer			pcidevfn;
	int				err = -ENODEV;
	struct device			*dev = ap->host_set->dev;
	struct ata_device		*atadev = &ap->device[ix];
	u32				dev_adr;
	acpi_status			status;
	struct acpi_object_list		input;
	union acpi_object 		in_params[1];

	if (ap->legacy_mode) {
		printk(KERN_DEBUG "%s: should not be here for PATA mode\n",
			__FUNCTION__);
		return 0;
	}
	if (noacpi)
		return 0;

	if (ata_msg_probe(ap))
		printk(KERN_DEBUG
			"%s: ap->id: %d, ix = %d, port#: %d, hard_port#: %d\n",
			__FUNCTION__, ap->id, ix,
			ap->port_no, ap->hard_port_no);

	/* Don't continue if not a SATA device. */
	if (!ata_id_is_sata(atadev->id)) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "%s: ata_id_is_sata is False\n",
				__FUNCTION__);
		goto out;
	}

	/* Don't continue if device has no _ADR method.
	 * _SDD is intended for known motherboard devices. */
	err = sata_get_dev_handle(dev, &handle, &pcidevfn);
	if (err < 0) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG
				"%s: sata_get_dev_handle failed (%d\n",
				__FUNCTION__, err);
		goto out;
	}

	/* Get this drive's _ADR info. if not already known. */
	if (!atadev->obj_handle) {
		dev_adr = SATA_ADR_RSVD;
		err = get_sata_adr(dev, handle, pcidevfn, ix, ap, atadev,
				&dev_adr);
		if (err < 0 || dev_adr == SATA_ADR_RSVD ||
		    !atadev->obj_handle) {
			if (ata_msg_probe(ap))
				printk(KERN_DEBUG "%s: get_sata_adr failed: "
					"err=%d, dev_adr=%u, obj_handle=0x%p\n",
					__FUNCTION__, err, dev_adr,
					atadev->obj_handle);
			goto out;
		}
	}

	/* Give the drive Identify data to the drive via the _SDD method */
	/* _SDD: set up input parameters */
	input.count = 1;
	input.pointer = in_params;
	in_params[0].type = ACPI_TYPE_BUFFER;
	in_params[0].buffer.length = sizeof(atadev->id);
	in_params[0].buffer.pointer = (u8 *)atadev->id;
	/* Output buffer: _SDD has no output */

	/* It's OK for _SDD to be missing too. */
	swap_buf_le16(atadev->id, ATA_ID_WORDS);
	status = acpi_evaluate_object(atadev->obj_handle, "_SDD", &input, NULL);
	swap_buf_le16(atadev->id, ATA_ID_WORDS);

	err = ACPI_FAILURE(status) ? -EIO : 0;
	if (err < 0) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG
				"ata%u(%u): %s _SDD error: status = 0x%x\n",
				ap->id, ap->device->devno,
				__FUNCTION__, status);
	}
out:
	return err;
}
EXPORT_SYMBOL_GPL(ata_acpi_push_id);

/**
 * do_drive_get_GTF - get the drive bootup default taskfile settings
 * @ap: the ata_port for the drive
 * @atadev: target ata_device
 * @gtf_length: number of bytes of _GTF data returned at @gtf_address
 * @gtf_address: buffer containing _GTF taskfile arrays
 *
 * This applies to both PATA and SATA drives.
 *
 * The _GTF method has no input parameters.
 * It returns a variable number of register set values (registers
 * hex 1F1..1F7, taskfiles).
 * The <variable number> is not known in advance, so have ACPI-CA
 * allocate the buffer as needed and return it, then free it later.
 *
 * The returned @gtf_length and @gtf_address are only valid if the
 * function return value is 0.
 */
int do_drive_get_GTF(struct ata_port *ap, struct ata_device *atadev,
			unsigned int *gtf_length, unsigned long *gtf_address,
			unsigned long *obj_loc)
{
	acpi_status			status;
	acpi_handle			handle;
	acpi_integer			pcidevfn;
	u32				dev_adr;
	struct acpi_buffer		output;
	union acpi_object 		*out_obj;
	struct device			*dev = ap->host_set->dev;
	int				err = -ENODEV;

	if (ata_msg_probe(ap))
		printk(KERN_DEBUG
			"%s: ENTER: ap->id: %d, port#: %d, hard_port#: %d\n",
			__FUNCTION__, ap->id,
		ap->port_no, ap->hard_port_no);

	*gtf_length = 0;
	*gtf_address = 0UL;
	*obj_loc = 0UL;

	if (noacpi)
		return 0;

	if (!ata_dev_present(atadev) ||
	    (ap->flags & ATA_FLAG_PORT_DISABLED)) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "%s: ERR: "
				"ata_dev_present: %d, PORT_DISABLED: %lu\n",
				__FUNCTION__, ata_dev_present(atadev),
				ap->flags & ATA_FLAG_PORT_DISABLED);
		goto out;
	}

	/* Don't continue if device has no _ADR method.
	 * _GTF is intended for known motherboard devices. */
	if (ata_id_is_ata(atadev->id)) {
		err = pata_get_dev_handle(dev, &handle, &pcidevfn);
		if (err < 0) {
			if (ata_msg_probe(ap))
				printk(KERN_DEBUG
					"%s: pata_get_dev_handle failed (%d)\n",
					__FUNCTION__, err);
			goto out;
		}
	} else {
		err = sata_get_dev_handle(dev, &handle, &pcidevfn);
		if (err < 0) {
			if (ata_msg_probe(ap))
				printk(KERN_DEBUG
					"%s: sata_get_dev_handle failed (%d\n",
					__FUNCTION__, err);
			goto out;
		}
	}

	/* Get this drive's _ADR info. if not already known. */
	if (!atadev->obj_handle) {
		dev_adr = SATA_ADR_RSVD;
		err = get_sata_adr(dev, handle, pcidevfn, 0, ap, atadev,
				&dev_adr);
		if (ata_id_is_ata(atadev->id)) {
			printk(KERN_DEBUG "%s: early exit\n", __FUNCTION__);
			err = -1;
			goto out;
		}
		if (err < 0 || dev_adr == SATA_ADR_RSVD ||
		    !atadev->obj_handle) {
			if (ata_msg_probe(ap))
				printk(KERN_DEBUG "%s: get_sata_adr failed: "
					"err=%d, dev_adr=%u, obj_handle=0x%p\n",
					__FUNCTION__, err, dev_adr,
					atadev->obj_handle);
			goto out;
		}
	}

	/* Setting up output buffer */
	output.length = ACPI_ALLOCATE_BUFFER;
	output.pointer = NULL;	/* ACPI-CA sets this; save/free it later */

	/* _GTF has no input parameters */
	err = -EIO;
	status = acpi_evaluate_object(atadev->obj_handle, "_GTF",
					NULL, &output);
	if (ACPI_FAILURE(status)) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG
				"%s: Run _GTF error: status = 0x%x\n",
				__FUNCTION__, status);
		goto out;
	}

	if (!output.length || !output.pointer) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "%s: Run _GTF: "
				"length or ptr is NULL (0x%llx, 0x%p)\n",
				__FUNCTION__,
				(unsigned long long)output.length,
				output.pointer);
		acpi_os_free(output.pointer);
		goto out;
	}

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER) {
		acpi_os_free(output.pointer);
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "%s: Run _GTF: error: "
				"expected object type of ACPI_TYPE_BUFFER, "
				"got 0x%x\n",
				__FUNCTION__, out_obj->type);
		err = -ENOENT;
		goto out;
	}

	if (!out_obj->buffer.length || !out_obj->buffer.pointer ||
	    out_obj->buffer.length % REGS_PER_GTF) {
		if (ata_msg_drv(ap))
			printk(KERN_ERR
				"%s: unexpected GTF length (%d) or addr (0x%p)\n",
				__FUNCTION__, out_obj->buffer.length,
				out_obj->buffer.pointer);
		err = -ENOENT;
		goto out;
	}

	*gtf_length = out_obj->buffer.length;
	*gtf_address = (unsigned long)out_obj->buffer.pointer;
	*obj_loc = (unsigned long)out_obj;
	if (ata_msg_probe(ap))
		printk(KERN_DEBUG "%s: returning "
			"gtf_length=%d, gtf_address=0x%lx, obj_loc=0x%lx\n",
			__FUNCTION__, *gtf_length, *gtf_address, *obj_loc);
	err = 0;
out:
	return err;
}
EXPORT_SYMBOL_GPL(do_drive_get_GTF);

/**
 * taskfile_load_raw - send taskfile registers to host controller
 * @ap: Port to which output is sent
 * @gtf: raw ATA taskfile register set (0x1f1 - 0x1f7)
 *
 * Outputs ATA taskfile to standard ATA host controller using MMIO
 * or PIO as indicated by the ATA_FLAG_MMIO flag.
 * Writes the control, feature, nsect, lbal, lbam, and lbah registers.
 * Optionally (ATA_TFLAG_LBA48) writes hob_feature, hob_nsect,
 * hob_lbal, hob_lbam, and hob_lbah.
 *
 * This function waits for idle (!BUSY and !DRQ) after writing
 * registers.  If the control register has a new value, this
 * function also waits for idle after writing control and before
 * writing the remaining registers.
 *
 * LOCKING: TBD:
 * Inherited from caller.
 */
static void taskfile_load_raw(struct ata_port *ap,
				struct ata_device *atadev,
				const struct taskfile_array *gtf)
{
	if (ata_msg_probe(ap))
		printk(KERN_DEBUG "%s: (0x1f1-1f7): hex: "
			"%02x %02x %02x %02x %02x %02x %02x\n",
			__FUNCTION__,
			gtf->tfa[0], gtf->tfa[1], gtf->tfa[2],
			gtf->tfa[3], gtf->tfa[4], gtf->tfa[5], gtf->tfa[6]);

	if (ap->ops->qc_issue) {
		struct ata_taskfile tf;
		unsigned int err;

		ata_tf_init(ap, &tf, atadev->devno);

		/* convert gtf to tf */
		tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE; /* TBD */
		tf.protocol = atadev->class == ATA_DEV_ATAPI ?
			ATA_PROT_ATAPI_NODATA : ATA_PROT_NODATA;
		tf.feature = gtf->tfa[0];	/* 0x1f1 */
		tf.nsect   = gtf->tfa[1];	/* 0x1f2 */
		tf.lbal    = gtf->tfa[2];	/* 0x1f3 */
		tf.lbam    = gtf->tfa[3];	/* 0x1f4 */
		tf.lbah    = gtf->tfa[4];	/* 0x1f5 */
		tf.device  = gtf->tfa[5];	/* 0x1f6 */
		tf.command = gtf->tfa[6];	/* 0x1f7 */

		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "call ata_exec_internal:\n");
		err = ata_exec_internal(ap, atadev, &tf, DMA_NONE, NULL, 0);
		if (err && ata_msg_probe(ap))
			printk(KERN_ERR "%s: ata_exec_internal failed: %u\n",
				__FUNCTION__, err);
	} else
		if (ata_msg_warn(ap))
			printk(KERN_WARNING
				"%s: SATA driver is missing qc_issue function entry points\n",
				__FUNCTION__);
}

/**
 * do_drive_set_taskfiles - write the drive taskfile settings from _GTF
 * @ap: the ata_port for the drive
 * @atadev: target ata_device
 * @gtf_length: total number of bytes of _GTF taskfiles
 * @gtf_address: location of _GTF taskfile arrays
 *
 * This applies to both PATA and SATA drives.
 *
 * Write {gtf_address, length gtf_length} in groups of
 * REGS_PER_GTF bytes.
 */
int do_drive_set_taskfiles(struct ata_port *ap, struct ata_device *atadev,
			unsigned int gtf_length, unsigned long gtf_address)
{
	int			err = -ENODEV;
	int			gtf_count = gtf_length / REGS_PER_GTF;
	int			ix;
	struct taskfile_array	*gtf;

	if (ata_msg_probe(ap))
		printk(KERN_DEBUG
			"%s: ENTER: ap->id: %d, port#: %d, hard_port#: %d\n",
			__FUNCTION__, ap->id,
			ap->port_no, ap->hard_port_no);

	if (noacpi)
		return 0;
	if (!ata_id_is_sata(atadev->id)) {
		printk(KERN_DEBUG "%s: skipping non-SATA drive\n",
			__FUNCTION__);
		return 0;
	}

	if (!ata_dev_present(atadev) ||
	    (ap->flags & ATA_FLAG_PORT_DISABLED))
		goto out;
	if (!gtf_count)		/* shouldn't be here */
		goto out;

	if (ata_msg_probe(ap))
		printk(KERN_DEBUG
			"%s: total GTF bytes=%u (0x%x), gtf_count=%d, addr=0x%lx\n",
			__FUNCTION__, gtf_length, gtf_length, gtf_count,
			gtf_address);
	if (gtf_length % REGS_PER_GTF) {
		if (ata_msg_drv(ap))
			printk(KERN_ERR "%s: unexpected GTF length (%d)\n",
				__FUNCTION__, gtf_length);
		goto out;
	}

	for (ix = 0; ix < gtf_count; ix++) {
		gtf = (struct taskfile_array *)
			(gtf_address + ix * REGS_PER_GTF);

		/* send all TaskFile registers (0x1f1-0x1f7) *in*that*order* */
		taskfile_load_raw(ap, atadev, gtf);
	}

	err = 0;
out:
	return err;
}
EXPORT_SYMBOL_GPL(do_drive_set_taskfiles);

/**
 * ata_acpi_exec_tfs - get then write drive taskfile settings
 * @ap: the ata_port for the drive
 *
 * This applies to both PATA and SATA drives.
 */
int ata_acpi_exec_tfs(struct ata_port *ap)
{
	int		ix;
	int		ret;
	unsigned int	gtf_length;
	unsigned long	gtf_address;
	unsigned long	obj_loc;

	if (ata_msg_probe(ap))
		printk(KERN_DEBUG "%s: ENTER:\n", __FUNCTION__);

	if (noacpi)
		return 0;

	for (ix = 0; ix < ATA_MAX_DEVICES; ix++) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "%s: call get_GTF, ix=%d\n",
				__FUNCTION__, ix);
		ret = do_drive_get_GTF(ap, &ap->device[ix],
				&gtf_length, &gtf_address, &obj_loc);
		if (ret < 0) {
			if (ata_msg_probe(ap))
				printk(KERN_DEBUG "%s: get_GTF error (%d)\n",
					__FUNCTION__, ret);
			break;
		}

		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "%s: call set_taskfiles, ix=%d\n",
				__FUNCTION__, ix);
		ret = do_drive_set_taskfiles(ap, &ap->device[ix],
				gtf_length, gtf_address);
		acpi_os_free((void *)obj_loc);
		if (ret < 0) {
			if (ata_msg_probe(ap))
				printk(KERN_DEBUG
					"%s: set_taskfiles error (%d)\n",
					__FUNCTION__, ret);
			break;
		}
	}

	if (ata_msg_probe(ap))
		printk(KERN_DEBUG "%s: ret=%d\n", __FUNCTION__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(ata_acpi_exec_tfs);

/**
 * ata_acpi_get_timing - get the channel (controller) timings
 * @ap: target ata_port (channel)
 *
 * For PATA ACPI, this function executes the _GTM ACPI method for the
 * target channel.
 *
 * _GTM only applies to ATA controllers in PATA (legacy) mode, not to SATA.
 * In legacy mode, ap->hard_port_no is channel (controller) number.
 */
void ata_acpi_get_timing(struct ata_port *ap)
{
	struct device		*dev = ap->dev;
	int			err;
	acpi_handle		dev_handle;
	acpi_integer		pcidevfn;
	acpi_handle		chan_handle;
	acpi_status		status;
	struct acpi_buffer	output;
	union acpi_object 	*out_obj;
	struct GTM_buffer	*gtm;

	if (noacpi)
		goto out;

	if (!ap->legacy_mode) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG
				"%s: channel/controller not in legacy mode (%s)\n",
				__FUNCTION__, dev->bus_id);
		goto out;
	}

	err = pata_get_dev_handle(dev, &dev_handle, &pcidevfn);
	if (err < 0) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG
				"%s: pata_get_dev_handle failed (%d)\n",
				__FUNCTION__, err);
		goto out;
	}

	/* get child objects of dev_handle == channel objects,
	 * + _their_ children == drive objects */
	/* channel is ap->hard_port_no */
	chan_handle = acpi_get_child(dev_handle, ap->hard_port_no);
	if (ata_msg_probe(ap))
		printk(KERN_DEBUG "%s: chan adr=%d: handle=0x%p\n",
			__FUNCTION__, ap->hard_port_no, chan_handle);
	if (!chan_handle)
		goto out;

#if 0
	/* TBD: also check ACPI object VALID bits */
	drive_handle = acpi_get_child(chan_handle, 0);
	printk(KERN_DEBUG "%s:   drive w/ adr=0: %c: 0x%p\n",
		__FUNCTION__,
		ap->device[0].class == ATA_DEV_NONE ? 'n' : 'v',
		drive_handle);
	drive_handle = acpi_get_child(chan_handle, 1);
	printk(KERN_DEBUG "%s:   drive w/ adr=1: %c: 0x%p\n",
		__FUNCTION__,
		ap->device[0].class == ATA_DEV_NONE ? 'n' : 'v',
		drive_handle);
#endif

	/* Setting up output buffer for _GTM */
	output.length = ACPI_ALLOCATE_BUFFER;
	output.pointer = NULL;	/* ACPI-CA sets this; save/free it later */

	/* _GTM has no input parameters */
	status = acpi_evaluate_object(chan_handle, "_GTM",
					NULL, &output);
	if (ata_msg_probe(ap))
		printk(KERN_DEBUG "%s: _GTM status: %d, outptr: 0x%p, outlen: 0x%llx\n",
			__FUNCTION__, status, output.pointer,
			(unsigned long long)output.length);
	if (ACPI_FAILURE(status)) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG
				"%s: Run _GTM error: status = 0x%x\n",
				__FUNCTION__, status);
		goto out;
	}

	if (!output.length || !output.pointer) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "%s: Run _GTM: "
				"length or ptr is NULL (0x%llx, 0x%p)\n",
				__FUNCTION__,
				(unsigned long long)output.length,
				output.pointer);
		acpi_os_free(output.pointer);
		goto out;
	}

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER) {
		acpi_os_free(output.pointer);
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "%s: Run _GTM: error: "
				"expected object type of ACPI_TYPE_BUFFER, "
				"got 0x%x\n",
				__FUNCTION__, out_obj->type);
		goto out;
	}

	if (!out_obj->buffer.length || !out_obj->buffer.pointer ||
	    out_obj->buffer.length != sizeof(struct GTM_buffer)) {
		acpi_os_free(output.pointer);
		if (ata_msg_drv(ap))
			printk(KERN_ERR
				"%s: unexpected _GTM length (0x%x)[should be 0x%x] or addr (0x%p)\n",
				__FUNCTION__, out_obj->buffer.length,
				sizeof(struct GTM_buffer), out_obj->buffer.pointer);
		goto out;
	}

	gtm = (struct GTM_buffer *)out_obj->buffer.pointer;
	if (ata_msg_probe(ap)) {
		printk(KERN_DEBUG "%s: _GTM info: ptr: 0x%p, len: 0x%x, exp.len: 0x%Zx\n",
			__FUNCTION__, out_obj->buffer.pointer,
			out_obj->buffer.length, sizeof(struct GTM_buffer));
		printk(KERN_DEBUG "%s: _GTM fields: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
			__FUNCTION__, gtm->PIO_speed0, gtm->DMA_speed0,
			gtm->PIO_speed1, gtm->DMA_speed1, gtm->GTM_flags);
	}

	/* TBD: when to free gtm */
	ap->gtm = gtm;
	kfree(ap->gtm_object_area); /* free previous then store new one */
	ap->gtm_object_area = out_obj;
out:;
}
EXPORT_SYMBOL_GPL(ata_acpi_get_timing);

/**
 * platform_set_timing - set the channel (controller) timings
 * @ap: target ata_port (channel)
 *
 * For PATA ACPI, this function executes the _STM ACPI method for the
 * target channel.
 *
 * _STM only applies to ATA controllers in PATA (legacy) mode, not to SATA.
 * In legacy mode, ap->hard_port_no is channel (controller) number.
 *
 * _STM requires Identify Drive data, which must already be present in
 * ata_device->id[] (i.e., it's not fetched here).
 */
void ata_acpi_push_timing(struct ata_port *ap)
{
	struct device		*dev = ap->dev;
	int			err;
	acpi_handle		dev_handle;
	acpi_integer		pcidevfn;
	acpi_handle		chan_handle;
	acpi_status		status;
	struct acpi_object_list	input;
	union acpi_object 	in_params[1];

	if (noacpi)
		goto out;

	if (!ap->legacy_mode) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG
				"%s: channel/controller not in legacy mode (%s)\n",
				__FUNCTION__, dev->bus_id);
		goto out;
	}

	if (ap->device[0].id[49] || ap->device[1].id[49]) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG "%s: drive(s) on channel %d: missing Identify data\n",
				__FUNCTION__, ap->hard_port_no);
		goto out;
	}

	err = pata_get_dev_handle(dev, &dev_handle, &pcidevfn);
	if (err < 0) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG
				"%s: pata_get_dev_handle failed (%d)\n",
				__FUNCTION__, err);
		goto out;
	}

	/* get child objects of dev_handle == channel objects,
	 * + _their_ children == drive objects */
	/* channel is ap->hard_port_no */
	chan_handle = acpi_get_child(dev_handle, ap->hard_port_no);
	if (ata_msg_probe(ap))
		printk(KERN_DEBUG "%s: chan adr=%d: handle=0x%p\n",
			__FUNCTION__, ap->hard_port_no, chan_handle);
	if (!chan_handle)
		goto out;

#if 0
	/* TBD: also check ACPI object VALID bits */
	drive_handle = acpi_get_child(chan_handle, 0);
	printk(KERN_DEBUG "%s:   drive w/ adr=0: %c: 0x%p\n",
		__FUNCTION__,
		ap->device[0].class == ATA_DEV_NONE ? 'n' : 'v',
		drive_handle);
	drive_handle = acpi_get_child(chan_handle, 1);
	printk(KERN_DEBUG "%s:   drive w/ adr=1: %c: 0x%p\n",
		__FUNCTION__,
		ap->device[0].class == ATA_DEV_NONE ? 'n' : 'v',
		drive_handle);
#endif

	/* Give the GTM buffer + drive Identify data to the channel via the
	 * _STM method: */
	/* setup input parameters buffer for _STM */
	input.count = 3;
	input.pointer = in_params;
	in_params[0].type = ACPI_TYPE_BUFFER;
	in_params[0].buffer.length = sizeof(struct GTM_buffer);
	in_params[0].buffer.pointer = (u8 *)ap->gtm;
	in_params[1].type = ACPI_TYPE_BUFFER;
	in_params[1].buffer.length = sizeof(ap->device[0].id);
	in_params[1].buffer.pointer = (u8 *)ap->device[0].id;
	in_params[2].type = ACPI_TYPE_BUFFER;
	in_params[2].buffer.length = sizeof(ap->device[1].id);
	in_params[2].buffer.pointer = (u8 *)ap->device[1].id;
	/* Output buffer: _STM has no output */

	swap_buf_le16(ap->device[0].id, ATA_ID_WORDS);
	swap_buf_le16(ap->device[1].id, ATA_ID_WORDS);
	status = acpi_evaluate_object(chan_handle, "_STM", &input, NULL);
	swap_buf_le16(ap->device[0].id, ATA_ID_WORDS);
	swap_buf_le16(ap->device[1].id, ATA_ID_WORDS);
	if (ata_msg_probe(ap))
		printk(KERN_DEBUG "%s: _STM status: %d\n",
			__FUNCTION__, status);
	if (ACPI_FAILURE(status)) {
		if (ata_msg_probe(ap))
			printk(KERN_DEBUG
				"%s: Run _STM error: status = 0x%x\n",
				__FUNCTION__, status);
		goto out;
	}

out:;
}
EXPORT_SYMBOL_GPL(ata_acpi_push_timing);
