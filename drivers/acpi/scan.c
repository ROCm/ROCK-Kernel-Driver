/*
 * scan.c - support for transforming the ACPI namespace into individual objects
 */

#include <linux/init.h>
#include <linux/acpi.h>

#include <acpi/acpi_drivers.h>
#include <acpi/acinterp.h>	/* for acpi_ex_eisa_id_to_string() */


#define _COMPONENT		ACPI_BUS_COMPONENT
ACPI_MODULE_NAME		("scan")

#define STRUCT_TO_INT(s)	(*((int*)&s))

extern struct acpi_device		*acpi_root;


#define ACPI_BUS_CLASS			"system_bus"
#define ACPI_BUS_HID			"ACPI_BUS"
#define ACPI_BUS_DRIVER_NAME		"ACPI Bus Driver"
#define ACPI_BUS_DEVICE_NAME		"System Bus"

static LIST_HEAD(acpi_device_list);
static spinlock_t acpi_device_lock = SPIN_LOCK_UNLOCKED;

static void acpi_device_release(struct kobject * kobj)
{
	struct acpi_device * dev = container_of(kobj,struct acpi_device,kobj);
	if (dev->pnp.cid_list)
		kfree(dev->pnp.cid_list);
	kfree(dev);
}

static struct kobj_type ktype_acpi_ns = {
	.release	= acpi_device_release,
};

static struct kset acpi_namespace_kset = {
	.kobj		= { 
		.name = "namespace",
	},
	.subsys = &acpi_subsys,
	.ktype	= &ktype_acpi_ns,
};


static void acpi_device_register(struct acpi_device * device, struct acpi_device * parent)
{
	/*
	 * Linkage
	 * -------
	 * Link this device to its parent and siblings.
	 */
	INIT_LIST_HEAD(&device->children);
	INIT_LIST_HEAD(&device->node);
	INIT_LIST_HEAD(&device->g_list);

	spin_lock(&acpi_device_lock);
	if (device->parent) {
		list_add_tail(&device->node, &device->parent->children);
		list_add_tail(&device->g_list,&device->parent->g_list);
	} else
		list_add_tail(&device->g_list,&acpi_device_list);
	spin_unlock(&acpi_device_lock);

	kobject_init(&device->kobj);
	strlcpy(device->kobj.name,device->pnp.bus_id,KOBJ_NAME_LEN);
	if (parent)
		device->kobj.parent = &parent->kobj;
	device->kobj.ktype = &ktype_acpi_ns;
	device->kobj.kset = &acpi_namespace_kset;
	kobject_add(&device->kobj);
}

static int
acpi_device_unregister (
	struct acpi_device	*device, 
	int			type)
{
	kobject_unregister(&device->kobj);
	return 0;
}

void
acpi_bus_data_handler (
	acpi_handle		handle,
	u32			function,
	void			*context)
{
	ACPI_FUNCTION_TRACE("acpi_bus_data_handler");

	/* TBD */

	return_VOID;
}

static int
acpi_bus_get_power_flags (
	struct acpi_device	*device)
{
	acpi_status             status = 0;
	acpi_handle		handle = 0;
	u32                     i = 0;

	ACPI_FUNCTION_TRACE("acpi_bus_get_power_flags");

	/*
	 * Power Management Flags
	 */
	status = acpi_get_handle(device->handle, "_PSC", &handle);
	if (ACPI_SUCCESS(status))
		device->power.flags.explicit_get = 1;
	status = acpi_get_handle(device->handle, "_IRC", &handle);
	if (ACPI_SUCCESS(status))
		device->power.flags.inrush_current = 1;
	status = acpi_get_handle(device->handle, "_PRW", &handle);
	if (ACPI_SUCCESS(status))
		device->power.flags.wake_capable = 1;

	/*
	 * Enumerate supported power management states
	 */
	for (i = ACPI_STATE_D0; i <= ACPI_STATE_D3; i++) {
		struct acpi_device_power_state *ps = &device->power.states[i];
		char		object_name[5] = {'_','P','R','0'+i,'\0'};

		/* Evaluate "_PRx" to se if power resources are referenced */
		acpi_evaluate_reference(device->handle, object_name, NULL,
			&ps->resources);
		if (ps->resources.count) {
			device->power.flags.power_resources = 1;
			ps->flags.valid = 1;
		}

		/* Evaluate "_PSx" to see if we can do explicit sets */
		object_name[2] = 'S';
		status = acpi_get_handle(device->handle, object_name, &handle);
		if (ACPI_SUCCESS(status)) {
			ps->flags.explicit_set = 1;
			ps->flags.valid = 1;
		}

		/* State is valid if we have some power control */
		if (ps->resources.count || ps->flags.explicit_set)
			ps->flags.valid = 1;

		ps->power = -1;		/* Unknown - driver assigned */
		ps->latency = -1;	/* Unknown - driver assigned */
	}

	/* Set defaults for D0 and D3 states (always valid) */
	device->power.states[ACPI_STATE_D0].flags.valid = 1;
	device->power.states[ACPI_STATE_D0].power = 100;
	device->power.states[ACPI_STATE_D3].flags.valid = 1;
	device->power.states[ACPI_STATE_D3].power = 0;

	/* TBD: System wake support and resource requirements. */

	device->power.state = ACPI_STATE_UNKNOWN;

	return 0;
}


/* --------------------------------------------------------------------------
                              Performance Management
   -------------------------------------------------------------------------- */

static int
acpi_bus_get_perf_flags (
	struct acpi_device	*device)
{
	device->performance.state = ACPI_STATE_UNKNOWN;
	return 0;
}

/* --------------------------------------------------------------------------
                                 Driver Management
   -------------------------------------------------------------------------- */

static LIST_HEAD(acpi_bus_drivers);
static DECLARE_MUTEX(acpi_bus_drivers_lock);


/**
 * acpi_bus_match 
 * --------------
 * Checks the device's hardware (_HID) or compatible (_CID) ids to see if it
 * matches the specified driver's criteria.
 */
static int
acpi_bus_match (
	struct acpi_device	*device,
	struct acpi_driver	*driver)
{
	int error = 0;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};

	if (device->flags.hardware_id)
		if (strstr(driver->ids, device->pnp.hardware_id))
			goto Done;

	if (device->flags.compatible_ids) {
		struct acpi_compatible_id_list *cid_list = device->pnp.cid_list;
		int i;

		/* compare multiple _CID entries against driver ids */
		for (i = 0; i < cid_list->count; i++)
		{
			if (strstr(driver->ids, cid_list->id[i].value))
				goto Done;
		}
	}
	error = -ENOENT;

 Done:
	if (buffer.pointer)
		acpi_os_free(buffer.pointer);
	return error;
}


/**
 * acpi_bus_driver_init 
 * --------------------
 * Used to initialize a device via its device driver.  Called whenever a 
 * driver is bound to a device.  Invokes the driver's add() and start() ops.
 */
static int
acpi_bus_driver_init (
	struct acpi_device	*device, 
	struct acpi_driver	*driver)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_bus_driver_init");

	if (!device || !driver)
		return_VALUE(-EINVAL);

	if (!driver->ops.add)
		return_VALUE(-ENOSYS);

	result = driver->ops.add(device);
	if (result) {
		device->driver = NULL;
		acpi_driver_data(device) = NULL;
		return_VALUE(result);
	}

	device->driver = driver;

	/*
	 * TBD - Configuration Management: Assign resources to device based
	 * upon possible configuration and currently allocated resources.
	 */

	if (driver->ops.start) {
		result = driver->ops.start(device);
		if (result && driver->ops.remove)
			driver->ops.remove(device, ACPI_BUS_REMOVAL_NORMAL);
		return_VALUE(result);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Driver successfully bound to device\n"));

	if (driver->ops.scan) {
		driver->ops.scan(device);
	}

	return_VALUE(0);
}

static int acpi_driver_attach(struct acpi_driver * drv)
{
	struct list_head * node, * next;

	ACPI_FUNCTION_TRACE("acpi_driver_attach");

	spin_lock(&acpi_device_lock);
	list_for_each_safe(node, next, &acpi_device_list) {
		struct acpi_device * dev = container_of(node, struct acpi_device, g_list);

		if (dev->driver || !dev->status.present)
			continue;
		spin_unlock(&acpi_device_lock);

		if (!acpi_bus_match(dev, drv)) {
			if (!acpi_bus_driver_init(dev, drv)) {
				atomic_inc(&drv->references);
				ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found driver [%s] for device [%s]\n",
						  drv->name, dev->pnp.bus_id));
			}
		}
		spin_lock(&acpi_device_lock);
	}
	spin_unlock(&acpi_device_lock);
	return_VALUE(0);
}

static int acpi_driver_detach(struct acpi_driver * drv)
{
	struct list_head * node, * next;

	ACPI_FUNCTION_TRACE("acpi_driver_detach");

	spin_lock(&acpi_device_lock);
	list_for_each_safe(node,next,&acpi_device_list) {
		struct acpi_device * dev = container_of(node,struct acpi_device,g_list);

		if (dev->driver == drv) {
			if (drv->ops.remove)
				drv->ops.remove(dev,ACPI_BUS_REMOVAL_NORMAL);
			dev->driver = NULL;
			dev->driver_data = NULL;
			atomic_dec(&drv->references);
		}
	}
	spin_unlock(&acpi_device_lock);
	return_VALUE(0);
}

/**
 * acpi_bus_register_driver 
 * ------------------------ 
 * Registers a driver with the ACPI bus.  Searches the namespace for all
 * devices that match the driver's criteria and binds.
 */
int
acpi_bus_register_driver (
	struct acpi_driver	*driver)
{
	int error = 0;

	ACPI_FUNCTION_TRACE("acpi_bus_register_driver");

	if (driver) {
		spin_lock(&acpi_device_lock);
		list_add_tail(&driver->node, &acpi_bus_drivers);
		spin_unlock(&acpi_device_lock);
		acpi_driver_attach(driver);
	} else
		error = -EINVAL;

	return_VALUE(error);
}


/**
 * acpi_bus_unregister_driver 
 * --------------------------
 * Unregisters a driver with the ACPI bus.  Searches the namespace for all
 * devices that match the driver's criteria and unbinds.
 */
int
acpi_bus_unregister_driver (
	struct acpi_driver	*driver)
{
	int error = 0;

	ACPI_FUNCTION_TRACE("acpi_bus_unregister_driver");

	if (driver) {
		acpi_driver_detach(driver);

		if (!atomic_read(&driver->references)) {
			spin_lock(&acpi_device_lock);
			list_del_init(&driver->node);
			spin_unlock(&acpi_device_lock);
		} 
	} else 
		error = -EINVAL;
	return_VALUE(error);
}

/**
 * acpi_bus_find_driver 
 * --------------------
 * Parses the list of registered drivers looking for a driver applicable for
 * the specified device.
 */
static int
acpi_bus_find_driver (
	struct acpi_device	*device)
{
	int			result = 0;
	struct list_head	* node, *next;

	ACPI_FUNCTION_TRACE("acpi_bus_find_driver");

	if (!device->flags.hardware_id && !device->flags.compatible_ids)
		goto Done;

	spin_lock(&acpi_device_lock);
	list_for_each_safe(node,next,&acpi_bus_drivers) {
		struct acpi_driver * driver = container_of(node,struct acpi_driver,node);

		atomic_inc(&driver->references);
		spin_unlock(&acpi_device_lock);
		if (!acpi_bus_match(device, driver)) {
			result = acpi_bus_driver_init(device, driver);
			if (!result)
				goto Done;
		}
		atomic_dec(&driver->references);
		spin_lock(&acpi_device_lock);
	}
	spin_unlock(&acpi_device_lock);

 Done:
	return_VALUE(result);
}


/* --------------------------------------------------------------------------
                                 Device Enumeration
   -------------------------------------------------------------------------- */

static int 
acpi_bus_get_flags (
	struct acpi_device	*device)
{
	acpi_status		status = AE_OK;
	acpi_handle		temp = NULL;

	ACPI_FUNCTION_TRACE("acpi_bus_get_flags");

	/* Presence of _STA indicates 'dynamic_status' */
	status = acpi_get_handle(device->handle, "_STA", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.dynamic_status = 1;

	/* Presence of _CID indicates 'compatible_ids' */
	status = acpi_get_handle(device->handle, "_CID", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.compatible_ids = 1;

	/* Presence of _RMV indicates 'removable' */
	status = acpi_get_handle(device->handle, "_RMV", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.removable = 1;

	/* Presence of _EJD|_EJ0 indicates 'ejectable' */
	status = acpi_get_handle(device->handle, "_EJD", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.ejectable = 1;
	else {
		status = acpi_get_handle(device->handle, "_EJ0", &temp);
		if (ACPI_SUCCESS(status))
			device->flags.ejectable = 1;
	}

	/* Presence of _LCK indicates 'lockable' */
	status = acpi_get_handle(device->handle, "_LCK", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.lockable = 1;

	/* Presence of _PS0|_PR0 indicates 'power manageable' */
	status = acpi_get_handle(device->handle, "_PS0", &temp);
	if (ACPI_FAILURE(status))
		status = acpi_get_handle(device->handle, "_PR0", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.power_manageable = 1;

	/* TBD: Peformance management */

	return_VALUE(0);
}

static void acpi_device_get_busid(struct acpi_device * device, acpi_handle handle, int type)
{
	char			bus_id[5] = {'?',0};
	struct acpi_buffer	buffer = {sizeof(bus_id), bus_id};
	int			i = 0;

	/*
	 * Bus ID
	 * ------
	 * The device's Bus ID is simply the object name.
	 * TBD: Shouldn't this value be unique (within the ACPI namespace)?
	 */
	switch (type) {
	case ACPI_BUS_TYPE_SYSTEM:
		sprintf(device->pnp.bus_id, "%s", "ACPI");
		break;
	case ACPI_BUS_TYPE_POWER_BUTTON:
		sprintf(device->pnp.bus_id, "%s", "PWRF");
		break;
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		sprintf(device->pnp.bus_id, "%s", "SLPF");
		break;
	default:
		acpi_get_name(handle, ACPI_SINGLE_NAME, &buffer);
		/* Clean up trailing underscores (if any) */
		for (i = 3; i > 1; i--) {
			if (bus_id[i] == '_')
				bus_id[i] = '\0';
			else
				break;
		}
		sprintf(device->pnp.bus_id, "%s", bus_id);
		break;
	}
}

static void acpi_device_set_id(struct acpi_device * device, struct acpi_device * parent,
			       acpi_handle handle, int type)
{
	struct acpi_device_info	*info;
	struct acpi_buffer	buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	char			*hid = NULL;
	char			*uid = NULL;
	struct acpi_compatible_id_list *cid_list = NULL;
	acpi_status		status;

	switch (type) {
	case ACPI_BUS_TYPE_DEVICE:
		status = acpi_get_object_info(handle, &buffer);
		if (ACPI_FAILURE(status)) {
			printk("%s: Error reading device info\n",__FUNCTION__);
			return;
		}

		info = buffer.pointer;
		if (info->valid & ACPI_VALID_HID)
			hid = info->hardware_id.value;
		if (info->valid & ACPI_VALID_UID)
			uid = info->unique_id.value;
		if (info->valid & ACPI_VALID_CID)
			cid_list = &info->compatibility_id;
		if (info->valid & ACPI_VALID_ADR) {
			device->pnp.bus_address = info->address;
			device->flags.bus_address = 1;
		}
		break;
	case ACPI_BUS_TYPE_POWER:
		hid = ACPI_POWER_HID;
		break;
	case ACPI_BUS_TYPE_PROCESSOR:
		hid = ACPI_PROCESSOR_HID;
		break;
	case ACPI_BUS_TYPE_SYSTEM:
		hid = ACPI_SYSTEM_HID;
		break;
	case ACPI_BUS_TYPE_THERMAL:
		hid = ACPI_THERMAL_HID;
		break;
	case ACPI_BUS_TYPE_POWER_BUTTON:
		hid = ACPI_BUTTON_HID_POWERF;
		break;
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		hid = ACPI_BUTTON_HID_SLEEPF;
		break;
	}

	/* 
	 * \_SB
	 * ----
	 * Fix for the system root bus device -- the only root-level device.
	 */
	if ((parent == ACPI_ROOT_OBJECT) && (type == ACPI_BUS_TYPE_DEVICE)) {
		hid = ACPI_BUS_HID;
		sprintf(device->pnp.device_name, "%s", ACPI_BUS_DEVICE_NAME);
		sprintf(device->pnp.device_class, "%s", ACPI_BUS_CLASS);
	}

	if (hid) {
		sprintf(device->pnp.hardware_id, "%s", hid);
		device->flags.hardware_id = 1;
	}
	if (uid) {
		sprintf(device->pnp.unique_id, "%s", uid);
		device->flags.unique_id = 1;
	}
	if (cid_list) {
		device->pnp.cid_list = kmalloc(cid_list->size, GFP_KERNEL);
		if (device->pnp.cid_list)
			memcpy(device->pnp.cid_list, cid_list, cid_list->size);
		else
			printk(KERN_ERR "Memory allocation error\n");
	}

	acpi_os_free(buffer.pointer);
}

int acpi_device_set_context(struct acpi_device * device, int type)
{
	acpi_status status = AE_OK;
	int result = 0;
	/*
	 * Context
	 * -------
	 * Attach this 'struct acpi_device' to the ACPI object.  This makes
	 * resolutions from handle->device very efficient.  Note that we need
	 * to be careful with fixed-feature devices as they all attach to the
	 * root object.
	 */
	if (type != ACPI_BUS_TYPE_POWER_BUTTON && 
	    type != ACPI_BUS_TYPE_SLEEP_BUTTON) {
		status = acpi_attach_data(device->handle,
			acpi_bus_data_handler, device);

		if (ACPI_FAILURE(status)) {
			printk("Error attaching device data\n");
			result = -ENODEV;
		}
	}
	return result;
}

void acpi_device_get_debug_info(struct acpi_device * device, acpi_handle handle, int type)
{
#ifdef CONFIG_ACPI_DEBUG_OUTPUT
	char		*type_string = NULL;
	char		name[80] = {'?','\0'};
	acpi_buffer	buffer = {sizeof(name), name};

	switch (type) {
	case ACPI_BUS_TYPE_DEVICE:
		type_string = "Device";
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		break;
	case ACPI_BUS_TYPE_POWER:
		type_string = "Power Resource";
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		break;
	case ACPI_BUS_TYPE_PROCESSOR:
		type_string = "Processor";
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		break;
	case ACPI_BUS_TYPE_SYSTEM:
		type_string = "System";
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		break;
	case ACPI_BUS_TYPE_THERMAL:
		type_string = "Thermal Zone";
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		break;
	case ACPI_BUS_TYPE_POWER_BUTTON:
		type_string = "Power Button";
		sprintf(name, "PWRB");
		break;
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		type_string = "Sleep Button";
		sprintf(name, "SLPB");
		break;
	}

	printk(KERN_DEBUG "Found %s %s [%p]\n", type_string, name, handle);
#endif /*CONFIG_ACPI_DEBUG_OUTPUT*/
}

static int 
acpi_bus_add (
	struct acpi_device	**child,
	struct acpi_device	*parent,
	acpi_handle		handle,
	int			type)
{
	int			result = 0;
	struct acpi_device	*device = NULL;

	ACPI_FUNCTION_TRACE("acpi_bus_add");

	if (!child)
		return_VALUE(-EINVAL);

	device = kmalloc(sizeof(struct acpi_device), GFP_KERNEL);
	if (!device) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Memory allocation error\n"));
		return_VALUE(-ENOMEM);
	}
	memset(device, 0, sizeof(struct acpi_device));

	device->handle = handle;
	device->parent = parent;

	acpi_device_get_busid(device,handle,type);

	/*
	 * Flags
	 * -----
	 * Get prior to calling acpi_bus_get_status() so we know whether
	 * or not _STA is present.  Note that we only look for object
	 * handles -- cannot evaluate objects until we know the device is
	 * present and properly initialized.
	 */
	result = acpi_bus_get_flags(device);
	if (result)
		goto end;

	/*
	 * Status
	 * ------
	 * See if the device is present.  We always assume that non-Device()
	 * objects (e.g. thermal zones, power resources, processors, etc.) are
	 * present, functioning, etc. (at least when parent object is present).
	 * Note that _STA has a different meaning for some objects (e.g.
	 * power resources) so we need to be careful how we use it.
	 */
	switch (type) {
	case ACPI_BUS_TYPE_DEVICE:
		result = acpi_bus_get_status(device);
		if (!result)
			break;
		if (!device->status.present) 
			result = -ENOENT;
		goto end;
	default:
		STRUCT_TO_INT(device->status) = 0x0F;
		break;
	}

	/*
	 * Initialize Device
	 * -----------------
	 * TBD: Synch with Core's enumeration/initialization process.
	 */

	/*
	 * Hardware ID, Unique ID, & Bus Address
	 * -------------------------------------
	 */
	acpi_device_set_id(device,parent,handle,type);

	/*
	 * Power Management
	 * ----------------
	 */
	if (device->flags.power_manageable) {
		result = acpi_bus_get_power_flags(device);
		if (result)
			goto end;
	}

	/*
	 * Performance Management
	 * ----------------------
	 */
	if (device->flags.performance_manageable) {
		result = acpi_bus_get_perf_flags(device);
		if (result)
			goto end;
	}

	if ((result = acpi_device_set_context(device,type)))
		goto end;

	acpi_device_get_debug_info(device,handle,type);

	acpi_device_register(device,parent);

	/*
	 * Bind _ADR-Based Devices
	 * -----------------------
	 * If there's a a bus address (_ADR) then we utilize the parent's 
	 * 'bind' function (if exists) to bind the ACPI- and natively-
	 * enumerated device representations.
	 */
	if (device->flags.bus_address) {
		if (device->parent && device->parent->ops.bind)
			device->parent->ops.bind(device);
	}

	/*
	 * Locate & Attach Driver
	 * ----------------------
	 * If there's a hardware id (_HID) or compatible ids (_CID) we check
	 * to see if there's a driver installed for this kind of device.  Note
	 * that drivers can install before or after a device is enumerated.
	 *
	 * TBD: Assumes LDM provides driver hot-plug capability.
	 */
	acpi_bus_find_driver(device);

end:
	if (!result)
		*child = device;
	else {
		if (device->pnp.cid_list)
			kfree(device->pnp.cid_list);
		kfree(device);
	}

	return_VALUE(result);
}



static int acpi_bus_scan (struct acpi_device	*start)
{
	acpi_status		status = AE_OK;
	struct acpi_device	*parent = NULL;
	struct acpi_device	*child = NULL;
	acpi_handle		phandle = 0;
	acpi_handle		chandle = 0;
	acpi_object_type	type = 0;
	u32			level = 1;

	ACPI_FUNCTION_TRACE("acpi_bus_scan");

	if (!start)
		return_VALUE(-EINVAL);

	parent = start;
	phandle = start->handle;
	
	/*
	 * Parse through the ACPI namespace, identify all 'devices', and
	 * create a new 'struct acpi_device' for each.
	 */
	while ((level > 0) && parent) {

		status = acpi_get_next_object(ACPI_TYPE_ANY, phandle,
			chandle, &chandle);

		/*
		 * If this scope is exhausted then move our way back up.
		 */
		if (ACPI_FAILURE(status)) {
			level--;
			chandle = phandle;
			acpi_get_parent(phandle, &phandle);
			if (parent->parent)
				parent = parent->parent;
			continue;
		}

		status = acpi_get_type(chandle, &type);
		if (ACPI_FAILURE(status))
			continue;

		/*
		 * If this is a scope object then parse it (depth-first).
		 */
		if (type == ACPI_TYPE_LOCAL_SCOPE) {
			level++;
			phandle = chandle;
			chandle = 0;
			continue;
		}

		/*
		 * We're only interested in objects that we consider 'devices'.
		 */
		switch (type) {
		case ACPI_TYPE_DEVICE:
			type = ACPI_BUS_TYPE_DEVICE;
			break;
		case ACPI_TYPE_PROCESSOR:
			type = ACPI_BUS_TYPE_PROCESSOR;
			break;
		case ACPI_TYPE_THERMAL:
			type = ACPI_BUS_TYPE_THERMAL;
			break;
		case ACPI_TYPE_POWER:
			type = ACPI_BUS_TYPE_POWER;
			break;
		default:
			continue;
		}

		status = acpi_bus_add(&child, parent, chandle, type);
		if (ACPI_FAILURE(status))
			continue;

		/*
		 * If the device is present, enabled, and functioning then
		 * parse its scope (depth-first).  Note that we need to
		 * represent absent devices to facilitate PnP notifications
		 * -- but only the subtree head (not all of its children,
		 * which will be enumerated when the parent is inserted).
		 *
		 * TBD: Need notifications and other detection mechanisms
		 *	in place before we can fully implement this.
		 */
		if (child->status.present) {
			status = acpi_get_next_object(ACPI_TYPE_ANY, chandle,
				0, NULL);
			if (ACPI_SUCCESS(status)) {
				level++;
				phandle = chandle;
				chandle = 0;
				parent = child;
			}
		}
	}

	return_VALUE(0);
}


static int
acpi_bus_scan_fixed (
	struct acpi_device	*root)
{
	int			result = 0;
	struct acpi_device	*device = NULL;

	ACPI_FUNCTION_TRACE("acpi_bus_scan_fixed");

	if (!root)
		return_VALUE(-ENODEV);

	/*
	 * Enumerate all fixed-feature devices.
	 */
	if (acpi_fadt.pwr_button == 0)
		result = acpi_bus_add(&device, acpi_root, 
			NULL, ACPI_BUS_TYPE_POWER_BUTTON);

	if (acpi_fadt.sleep_button == 0)
		result = acpi_bus_add(&device, acpi_root, 
			NULL, ACPI_BUS_TYPE_SLEEP_BUTTON);

	return_VALUE(result);
}


static int __init acpi_scan_init(void)
{
	int result;

	ACPI_FUNCTION_TRACE("acpi_scan_init");

	if (acpi_disabled)
		return_VALUE(0);

	kset_register(&acpi_namespace_kset);

	/*
	 * Create the root device in the bus's device tree
	 */
	result = acpi_bus_add(&acpi_root, NULL, ACPI_ROOT_OBJECT, 
		ACPI_BUS_TYPE_SYSTEM);
	if (result)
		goto Done;

	/*
	 * Enumerate devices in the ACPI namespace.
	 */
	result = acpi_bus_scan_fixed(acpi_root);
	if (!result) 
		result = acpi_bus_scan(acpi_root);

	if (result)
		acpi_device_unregister(acpi_root, ACPI_BUS_REMOVAL_NORMAL);

 Done:
	return_VALUE(result);
}

subsys_initcall(acpi_scan_init);
