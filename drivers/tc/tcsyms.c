/*
 *	TURBOchannel Services -- Exported Symbols
 *
 */

#include <linux/module.h>
#include <asm/dec/tc.h>

EXPORT_SYMBOL(search_tc_card);
EXPORT_SYMBOL(claim_tc_card);
EXPORT_SYMBOL(release_tc_card);
EXPORT_SYMBOL(get_tc_base_addr);
EXPORT_SYMBOL(get_tc_irq_nr);
EXPORT_SYMBOL(get_tc_speed);
