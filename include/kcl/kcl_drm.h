#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

/* helper for handling conditionals in various for_each macros */
#ifndef for_each_if
#define for_each_if(condition) if (!(condition)) {} else
#endif

#endif /* AMDKCL_DRM_H */
