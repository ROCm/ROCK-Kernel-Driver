/*
 * scan.c - support for transforming the ACPI namespace into individual objects
 */

#include <linux/init.h>
#include <linux/acpi.h>

#include "acpi_drivers.h"
#include "include/acinterp.h"	/* for acpi_ex_eisa_id_to_string() */


#define _COMPONENT		ACPI_BUS_COMPONENT
ACPI_MODULE_NAME		("scan")

#define STRUCT_TO_INT(s)	(*((int*)&s))

extern struct acpi_device		*acpi_root;


#define ACPI_BUS_CLASS			"system_bus"
#define ACPI_BUS_HID			"ACPI_BUS"
#define ACPI_BUS_DRIVER_NAME		"ACPI Bus Driver"
#define ACPI_BUS_DEVICE_NAME		"System Bus"

static int
acpi_device_register (
	struct acpi_device	*device,
	struct acpi_device	*parent)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_device_register");

	if (device)
		result = acpi_create_dir(device);
	else 
		result = -EINVAL;

	return_VALUE(result);
}


static int
acpi_device_unregister (
	struct acpi_device	*device)
{
	ACPI_FUNCTION_TRACE("acpi_device_unregister");

	acpi_remove_dir(device);
	return_VALUE(0);
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

	if (!device)
		return -ENODEV;

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

	/*
	 * System Power States
	 * -------------------
	 */
	/* TBD: S1-S4 power state support and resource requirements. */
	/*
	for (i=ACPI_STATE_S1; i<ACPI_STATE_S5; i++) {
		char name[5] = {'_','S',('0'+i),'D','\0'};
		status = acpi_evaluate_integer(device->handle, name, NULL,
			&state);
		if (ACPI_FAILURE(status))
			continue;
	}
	*/

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
	ACPI_FUNCTION_TRACE("acpi_bus_get_perf_flags");

	if (!device)
		return -ENODEV;

	device->performance.state = ACPI_STATE_UNKNOWN;

	return 0;
}

/* --------------------------------------------------------------------------
                               Namespace Management
   -------------------------------------------------------------------------- */

#define WALK_UP			0
#define WALK_DOWN		1

typedef int (*acpi_bus_walk_callback)(struct acpi_device*, int, void*);

#define HAS_CHILDREN(d)		((d)->children.next != &((d)->children))
#define HAS_SIBLINGS(d)		(((d)->parent) && ((d)->node.next != &(d)->parent->children))
#define NODE_TO_DEVICE(n)	(list_entry(n, struct acpi_device, node))


/**
 * acpi_bus_walk
 * -------------
 * Used to walk the ACPI Bus's device namespace.  Can walk down (depth-first)
 * or up.  Able to parse starting at any node in the namespace.  Note that a
 * callback return value of -ELOOP will terminate the walk.
 *
 * @start:	starting point
 * callback:	function to call for every device encountered while parsing
 * direction:	direction to parse (up or down)
 * @data:	context for this search operation
 */
static int
acpi_bus_walk (
	struct acpi_device	*start, 
	acpi_bus_walk_callback	callback, 
	int			direction, 
	void			*data)
{
	int			result = 0;
	int			level = 0;
	struct acpi_device	*device = NULL;

	if (!start || !callback)
		return -EINVAL;

	device = start;

	/*
	 * Parse Namespace
	 * ---------------
	 * Parse a given subtree (specified by start) in the given direction.
	 * Walking 'up' simply means that we execute the callback on leaf
	 * devices prior to their parents (useful for things like removing
	 * or powering down a subtree).
	 */

	while (device) {

		if (direction == WALK_DOWN)
			if (-ELOOP == callback(device, level, data))
				break;

		/* Depth First */

		if (HAS_CHILDREN(device)) {
			device = NODE_TO_DEVICE(device->children.next);
			++level;
			continue;
		}

		if (direction == WALK_UP)
			if (-ELOOP == callback(device, level, data))
				break;

		/* Now Breadth */

		if (HAS_SIBLINGS(device)) {
			device = NODE_TO_DEVICE(device->node.next);
			continue;
		}

		/* Scope Exhausted - Find Next */

		while ((device = device->parent)) {
			--level;
			if (HAS_SIBLINGS(device)) {
				device = NODE_TO_DEVICE(device->node.next);
				break;
			}
		}
	}

	if ((direction == WALK_UP) && (result == 0))
		callback(start, level, data);

	return result;
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

	if (!device || !driver)
		return -EINVAL;

	if (device->flags.hardware_id) {
		if (strstr(driver->ids, device->pnp.hardware_id))
			return 0;
	}

	if (device->flags.compatible_ids) {
		acpi_status	status = AE_OK;
		acpi_buffer	buffer = {ACPI_ALLOCATE_BUFFER, NULL};
		acpi_object	*object = NULL;
		char		cid[256];

		memset(cid, 0, sizeof(cid));

		status = acpi_evaluate_object(device->handle, "_CID", NULL, 
			&buffer);
		if (ACPI_FAILURE(status) || !buffer.pointer)
			return -ENOENT;

		object = (acpi_object *) buffer.pointer;

		switch (object->type) {
		case ACPI_TYPE_INTEGER:
			acpi_ex_eisa_id_to_string((u32) object->integer.value, 
				cid);
			break;
		case ACPI_TYPE_STRING:
			strncpy(cid, object->string.pointer, sizeof(cid) - 1);
			break;
		case ACPI_TYPE_PACKAGE:
			/* TBD: Support CID packages */
			break;
		}

		if (!cid[0]) {
			acpi_os_free(buffer.pointer);
			return -ENOENT;
		}

		if (strstr(driver->ids, cid)) {
			acpi_os_free(buffer.pointer);
			return 0;
		}

		acpi_os_free(buffer.pointer);
	}

	return -ENOENT;
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


/**
 * acpi_bus_attach 
 * -------------
 * Callback for acpi_bus_walk() used to find devices that match a specific 
 * driver's criteria and then attach the driver.
 */
static int
acpi_bus_attach (
	struct acpi_device	*device, 
	int			level, 
	void			*data)
{
	int			result = 0;
	struct acpi_driver	*driver = NULL;

	ACPI_FUNCTION_TRACE("acpi_bus_attach");

	if (!device || !data)
		return_VALUE(-EINVAL);

	driver = (struct acpi_driver *) data;

	if (device->driver)
		return_VALUE(-EEXIST);

	if (!device->status.present)
		return_VALUE(-ENODEV);

	result = acpi_bus_match(device, driver);
	if (result)
		return_VALUE(result);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found driver [%s] for device [%s]\n",
		driver->name, device->pnp.bus_id));
	
	result = acpi_bus_driver_init(device, driver);
	if (result)
		return_VALUE(result);

	down(&acpi_bus_drivers_lock);
	++driver->references;
	up(&acpi_bus_drivers_lock);

	return_VALUE(0);
}


/**
 * acpi_bus_unattach 
 * -----------------
 * Callback for acpi_bus_walk() used to find devices that match a specific 
 * driver's criteria and unattach the driver.
 */
static int
acpi_bus_unattach (
	struct acpi_device	*device, 
	int			level, 
	void			*data)
{
	int			result = 0;
	struct acpi_driver	*driver = (struct acpi_driver *) data;

	ACPI_FUNCTION_TRACE("acpi_bus_unattach");

	if (!device || !driver)
		return_VALUE(-EINVAL);

	if (device->driver != driver)
		return_VALUE(-ENOENT);

	if (!driver->ops.remove)
		return_VALUE(-ENOSYS);

	result = driver->ops.remove(device, ACPI_BUS_REMOVAL_NORMAL);
	if (result)
		return_VALUE(result);

	device->driver = NULL;
	acpi_driver_data(device) = NULL;

	down(&acpi_bus_drivers_lock);
	driver->references--;
	up(&acpi_bus_drivers_lock);

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
	ACPI_FUNCTION_TRACE("acpi_bus_register_driver");

	if (!driver)
		return_VALUE(-EINVAL);

	down(&acpi_bus_drivers_lock);
	list_add_tail(&driver->node, &acpi_bus_drivers);
	up(&acpi_bus_drivers_lock);

	acpi_bus_walk(acpi_root, acpi_bus_attach, 
		WALK_DOWN, driver);

	return_VALUE(driver->references);
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
	ACPI_FUNCTION_TRACE("acpi_bus_unregister_driver");

	if (!driver)
		return_VALUE(-EINVAL);

	acpi_bus_walk(acpi_root, acpi_bus_unattach, WALK_UP, driver);

	if (driver->references)
		return_VALUE(driver->references);

	down(&acpi_bus_drivers_lock);
	list_del(&driver->node);
	up(&acpi_bus_drivers_lock);

	return_VALUE(0);
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
	int			result = -ENODEV;
	struct list_head	*entry = NULL;
	struct acpi_driver	*driver = NULL;

	ACPI_FUNCTION_TRACE("acpi_bus_find_driver");

	if (!device || device->driver)
		return_VALUE(-EINVAL);

	down(&acpi_bus_drivers_lock);

	list_for_each(entry, &acpi_bus_drivers) {

		driver = list_entry(entry, struct acpi_driver, node);

		if (acpi_bus_match(device, driver))
			continue;

		result = acpi_bus_driver_init(device, driver);
		if (!result)
			++driver->references;

		break;
	}

	up(&acpi_bus_drivers_lock);

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

static int
acpi_bus_remove (
	struct acpi_device	*device, 
	int			type)
{
	ACPI_FUNCTION_TRACE("acpi_bus_remove");

	if (!device)
		return_VALUE(-ENODEV);

	acpi_device_unregister(device);

	kfree(device);

	return_VALUE(0);
}

static int 
acpi_bus_add (
	struct acpi_device	**child,
	struct acpi_device	*parent,
	acpi_handle		handle,
	int			type)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	struct acpi_device	*device = NULL;
	char			bus_id[5] = {'?',0};
	acpi_buffer		buffer = {sizeof(bus_id), bus_id};
	acpi_device_info	info;
	char			*hid = NULL;
	char			*uid = NULL;
	int			i = 0;

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

	memset(&info, 0, sizeof(acpi_device_info));

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
		if (result)
			goto end;
		break;
	default:
		STRUCT_TO_INT(device->status) = 0x0F;
		break;
	}
	if (!device->status.present) {
		result = -ENOENT;
		goto end;
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
	switch (type) {
	case ACPI_BUS_TYPE_DEVICE:
		status = acpi_get_object_info(handle, &info);
		if (ACPI_FAILURE(status)) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				"Error reading device info\n"));
			result = -ENODEV;
			goto end;
		}
		/* Clean up info strings (not NULL terminated) */
		info.hardware_id[sizeof(info.hardware_id)-1] = '\0';
		info.unique_id[sizeof(info.unique_id)-1] = '\0';
		if (info.valid & ACPI_VALID_HID)
			hid = info.hardware_id;
		if (info.valid & ACPI_VALID_UID)
			uid = info.unique_id;
		if (info.valid & ACPI_VALID_ADR) {
			device->pnp.bus_address = info.address;
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

	/*
	 * Context
	 * -------
	 * Attach this 'struct acpi_device' to the ACPI object.  This makes
	 * resolutions from handle->device very efficient.  Note that we need
	 * to be careful with fixed-feature devices as they all attach to the
	 * root object.
	 */
	switch (type) {
	case ACPI_BUS_TYPE_POWER_BUTTON:
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		break;
	default:
		status = acpi_attach_data(device->handle,
			acpi_bus_data_handler, device);
		break;
	}
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Error attaching device data\n"));
		result = -ENODEV;
		goto end;
	}

	/*
	 * Linkage
	 * -------
	 * Link this device to its parent and siblings.
	 */
	INIT_LIST_HEAD(&device->children);
	if (!device->parent)
		INIT_LIST_HEAD(&device->node);
	else
		list_add_tail(&device->node, &device->parent->children);

#ifdef CONFIG_ACPI_DEBUG
	{
		char		*type_string = NULL;
		char		name[80] = {'?','\0'};
		acpi_buffer	buffer = {sizeof(name), name};

		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);

		switch (type) {
		case ACPI_BUS_TYPE_DEVICE:
			type_string = "Device";
			break;
		case ACPI_BUS_TYPE_POWER:
			type_string = "Power Resource";
			break;
		case ACPI_BUS_TYPE_PROCESSOR:
			type_string = "Processor";
			break;
		case ACPI_BUS_TYPE_SYSTEM:
			type_string = "System";
			break;
		case ACPI_BUS_TYPE_THERMAL:
			type_string = "Thermal Zone";
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

		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found %s %s [%p]\n", 
			type_string, name, handle));
	}
#endif /*CONFIG_ACPI_DEBUG*/

	/*
	 * Global Device Hierarchy:
	 * ------------------------
	 * Register this device with the global device hierarchy.
	 */
	acpi_device_register(device, parent);

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
	 * that drivers can install before or after a device in enumerated.
	 *
	 * TBD: Assumes LDM provides driver hot-plug capability.
	 */
	if (device->flags.hardware_id || device->flags.compatible_ids)
		acpi_bus_find_driver(device);

end:
	if (result) {
		kfree(device);
		return_VALUE(result);
	}

	*child = device;

	return_VALUE(0);
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
		if (type == ACPI_TYPE_ANY) {
			/* Hack to get around scope identity problem */
			status = acpi_get_next_object(ACPI_TYPE_ANY, chandle, 0, NULL);
			if (ACPI_SUCCESS(status)) {
				level++;
				phandle = chandle;
				chandle = 0;
			}
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
			ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_POWER_BUTTON);

	if (acpi_fadt.sleep_button == 0)
		result = acpi_bus_add(&device, acpi_root, 
			ACPI_ROOT_OBJECT, ACPI_BUS_TYPE_SLEEP_BUTTON);

	return_VALUE(result);
}


static int __init acpi_scan_init(void)
{
	int result;

	ACPI_FUNCTION_TRACE("acpi_scan_init");

	if (acpi_disabled)
		return_VALUE(0);

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
		acpi_bus_remove(acpi_root, ACPI_BUS_REMOVAL_NORMAL);

 Done:
	return_VALUE(result);
}

subsys_initcall(acpi_scan_init);
