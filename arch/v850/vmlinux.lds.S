#include <linux/config.h>

#ifdef CONFIG_V850E_SIM
# include "sim.ld"
#endif

#ifdef CONFIG_V850E2_SIM85E2C
# include "sim85e2c.ld"
#endif

#ifdef CONFIG_V850E2_FPGA85E2C
# include "fpga85e2c.ld"
#endif

#ifdef CONFIG_V850E2_ANNA
# ifdef CONFIG_ROM_KERNEL
#  include "anna-rom.ld"
# else
#  include "anna.ld"
# endif
#endif

#ifdef CONFIG_RTE_CB_MA1
# ifdef CONFIG_ROM_KERNEL
#  include "rte_ma1_cb-rom.ld"
# elif CONFIG_RTE_CB_MA1_KSRAM
#  include "rte_ma1_cb-ksram.ld"
# else /* !CONFIG_ROM_KERNEL && !CONFIG_RTE_CB_MA1_KSRAM */
#  include "rte_ma1_cb.ld"
# endif /* CONFIG_ROM_KERNEL */
#endif /* CONFIG_RTE_CB_MA1 */
