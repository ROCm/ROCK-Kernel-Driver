#ifndef __ACPI_PROCESSOR_H
#define __ACPI_PROCESSOR_H

#include <linux/kernel.h>

#define ACPI_PROCESSOR_BUSY_METRIC	10

#define ACPI_PROCESSOR_MAX_POWER	ACPI_C_STATE_COUNT
#define ACPI_PROCESSOR_MAX_C2_LATENCY	100
#define ACPI_PROCESSOR_MAX_C3_LATENCY	1000

#define ACPI_PROCESSOR_MAX_THROTTLING	16
#define ACPI_PROCESSOR_MAX_THROTTLE	250	/* 25% */
#define ACPI_PROCESSOR_MAX_DUTY_WIDTH	4

/* Power Management */

struct acpi_processor_cx_policy {
	u32			count;
	u32			state;
	struct {
		u32			time;
		u32			ticks;
		u32			count;
		u32			bm;
	}			threshold;
};

struct acpi_processor_cx {
	u8			valid;
	u32			address;
	u32			latency;
	u32			latency_ticks;
	u32			power;
	u32			usage;
	struct acpi_processor_cx_policy promotion;
	struct acpi_processor_cx_policy demotion;
};

struct acpi_processor_power {
	u32			state;
	u32			default_state;
	u32			bm_activity;
	struct acpi_processor_cx states[ACPI_PROCESSOR_MAX_POWER];
};

/* Performance Management */

struct acpi_pct_register {
	u8			descriptor;
	u16			length;
	u8			space_id;
	u8			bit_width;
	u8			bit_offset;
	u8			reserved;
	u64			address;
} __attribute__ ((packed));

struct acpi_processor_px {
	acpi_integer		core_frequency;		/* megahertz */
	acpi_integer		power;			/* milliWatts */
	acpi_integer		transition_latency;	/* microseconds */
	acpi_integer		bus_master_latency;	/* microseconds */
	acpi_integer		control;		/* control value */
	acpi_integer		status;			/* success indicator */
};

#define ACPI_PDC_REVISION_ID                   0x1

struct acpi_processor_performance {
	unsigned int		 state;
	unsigned int		 platform_limit;
	struct acpi_pct_register control_register;
	struct acpi_pct_register status_register;
	unsigned int		 state_count;
	struct acpi_processor_px *states;

	/* the _PDC objects passed by the driver, if any */
	struct acpi_object_list *pdc;
};



/* Throttling Control */

struct acpi_processor_tx {
	u16			power;
	u16			performance;
};

struct acpi_processor_throttling {
	int			state;
	u32			address;
	u8			duty_offset;
	u8			duty_width;
	int			state_count;
	struct acpi_processor_tx states[ACPI_PROCESSOR_MAX_THROTTLING];
};

/* Limit Interface */

struct acpi_processor_lx {
	int			px;		/* performace state */	
	int			tx;		/* throttle level */
};

struct acpi_processor_limit {
	struct acpi_processor_lx state;		/* current limit */
	struct acpi_processor_lx thermal;	/* thermal limit */
	struct acpi_processor_lx user;		/* user limit */
};


struct acpi_processor_flags {
	u8			power:1;
	u8			performance:1;
	u8			throttling:1;
	u8			limit:1;
	u8			bm_control:1;
	u8			bm_check:1;
	u8			reserved:2;
};

struct acpi_processor {
	acpi_handle		handle;
	u32			acpi_id;
	u32			id;
	int			performance_platform_limit;
	struct acpi_processor_flags flags;
	struct acpi_processor_power power;
	struct acpi_processor_performance *performance;
	struct acpi_processor_throttling throttling;
	struct acpi_processor_limit limit;
};

extern int acpi_processor_register_performance (
	struct acpi_processor_performance * performance,
	unsigned int cpu);
extern void acpi_processor_unregister_performance (
	struct acpi_processor_performance * performance,
	unsigned int cpu);

/* note: this locks both the calling module and the processor module
         if a _PPC object exists, rmmod is disallowed then */
int acpi_processor_notify_smm(struct module *calling_module);

#endif
