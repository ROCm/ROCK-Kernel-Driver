/******************************************************************************
 *
 * nicstarmac.h
 *
 * Header file for nicstarmac.c
 *
 ******************************************************************************/


typedef void __iomem *virt_addr_t;

void nicstar_init_eprom( virt_addr_t base );
void nicstar_read_eprom( virt_addr_t, u_int8_t, u_int8_t *, u_int32_t);
