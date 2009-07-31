#ifndef __XEN_FIRMWARE_H__
#define __XEN_FIRMWARE_H__

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
void copy_edd(void);
#endif

void copy_edid(void);

#endif /* __XEN_FIRMWARE_H__ */
