#include <linux/dasd.h>
#include "dasd_types.h"
#include "dasd_erp.h"

dasd_operations_t dasd_mdsk_operations =
{
  NULL,
  /*	dasd_mdsk_ck_devinfo,
	dasd_mdsk_build_req,
	dasd_mdsk_rw_label,
	dasd_mdsk_ck_char,
	dasd_mdsk_fill_sizes,
	dasd_mdsk_format, */
};
