
#include <linux/device.h>
#include <linux/init.h>

extern int devices_init(void);
extern int buses_init(void);
extern int classes_init(void);
extern int firmware_init(void);
extern int sys_bus_init(void);
extern int cpu_dev_init(void);

/**
 *	driver_init - initialize driver model.
 *
 *	Call the driver model init functions to initialize their
 *	subsystems. Called early from init/main.c.
 */

void __init driver_init(void)
{
	/* These are the core pieces */
	devices_init();
	buses_init();
	classes_init();
	firmware_init();

	/* These are also core pieces, but must come after the 
	 * core core pieces.
	 */
	sys_bus_init();
	cpu_dev_init();
}
