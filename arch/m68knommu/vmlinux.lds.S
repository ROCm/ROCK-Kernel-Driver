
#include <linux/config.h>

#ifdef CONFIG_PILOT3
#ifdef CONFIG_ROMKERNEL
#include "platform/68328/pilot/rom.ld"
#endif
#ifdef CONFIG_RAMKERNEL
#include "platform/68328/pilot/ram.ld"
#endif
#endif

#ifdef CONFIG_UCSIMM
#ifdef CONFIG_RAMKERNEL
#include "platform/68EZ328/ucsimm/ram.ld"
#endif
#ifdef CONFIG_ROMKERNEL
#include "platform/68EZ328/ucsimm/rom.ld"
#endif
#ifdef CONFIG_HIMEMKERNEL
#include "platform/68EZ328/ucsimm/himem.ld"
#endif
#endif

#ifdef CONFIG_UCDIMM
#ifdef CONFIG_RAMKERNEL
#include "platform/68VZ328/ucdimm/ram.ld"
#endif
#ifdef CONFIG_ROMKERNEL
#include "platform/68VZ328/ucdimm/rom.ld"
#endif
#ifdef CONFIG_HIMEMKERNEL
#include "platform/68VZ328/ucdimm/himem.ld"
#endif
#endif
#ifdef CONFIG_DRAGEN2
#ifdef CONFIG_RAMKERNEL
#include "platform/68VZ328/de2/ram.ld"
#endif
#ifdef CONFIG_ROMKERNEL
#include "platform/68VZ328/de2/rom.ld"
#endif
#ifdef CONFIG_HIMEMKERNEL
#include "platform/68VZ328/de2/himem.ld"
#endif
#endif

#ifdef CONFIG_UCQUICC
#ifdef CONFIG_RAMKERNEL
#include "platform/68360/uCquicc/ram.ld"
#endif
#ifdef CONFIG_ROMKERNEL
#include "platform/68360/uCquicc/rom.ld"
#endif
#endif

#ifdef CONFIG_ARN5206
#include "platform/5206/ARNEWSH/ram.ld"
#endif

#ifdef CONFIG_M5206eC3
#include "platform/5206e/MOTOROLA/ram.ld"
#endif
#ifdef CONFIG_ELITE
#include "platform/5206e/eLITE/ram.ld"
#endif

#ifdef CONFIG_M5249C3
#include "platform/5249/MOTOROLA/ram.ld"
#endif

#ifdef CONFIG_M5272C3
#include "platform/5272/MOTOROLA/ram.ld"
#endif

#ifdef CONFIG_ARN5307
#include "platform/5307/ARNEWSH/ram.ld"
#endif
#ifdef CONFIG_M5307C3
#include "platform/5307/MOTOROLA/ram.ld"
#endif
#ifdef CONFIG_eLIA
#include "platform/5307/eLIA/ram.ld"
#endif
#ifdef CONFIG_DISKtel
#include "platform/5307/DISKtel/ram.ld"
#endif
#ifdef CONFIG_SECUREEDGEMP3
#include "platform/5307/MP3/ram.ld"
#endif

#ifdef CONFIG_M5407
#include "platform/5407/MOTOROLA/ram.ld"
#endif

#ifdef CONFIG_CLEOPATRA
#ifdef CONFIG_M5307
#include "platform/5307/CLEOPATRA/ram.ld"
#endif
#ifdef CONFIG_M5407
#include "platform/5407/CLEOPATRA/ram.ld"
#endif
#endif

#ifdef CONFIG_NETtel
#ifdef CONFIG_M5206e
#include "platform/5206e/NETtel/ram.ld"
#endif
#ifdef CONFIG_M5272
#include "platform/5272/NETtel/ram.ld"
#endif
#ifdef CONFIG_M5307
#include "platform/5307/NETtel/ram.ld"
#endif
#endif
