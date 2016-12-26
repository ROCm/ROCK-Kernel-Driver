#ifndef AMDKCL_DRM_H
#define AMDKCL_DRM_H

#include <drm/drm_dp_helper.h>

#ifndef DP_ADJUST_REQUEST_POST_CURSOR2
#define DP_ADJUST_REQUEST_POST_CURSOR2      0x20c
#endif

#ifndef DP_TEST_MISC0
#define DP_TEST_MISC0                       0x232
#endif

#ifndef DP_TEST_PHY_PATTERN
#define DP_TEST_PHY_PATTERN                 0x248
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_7_0
#define DP_TEST_80BIT_CUSTOM_PATTERN_7_0    0x250
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_15_8
#define DP_TEST_80BIT_CUSTOM_PATTERN_15_8   0x251
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_23_16
#define DP_TEST_80BIT_CUSTOM_PATTERN_23_16  0x252
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_31_24
#define DP_TEST_80BIT_CUSTOM_PATTERN_31_24  0x253
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_39_32
#define DP_TEST_80BIT_CUSTOM_PATTERN_39_32  0x254
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_47_40
#define DP_TEST_80BIT_CUSTOM_PATTERN_47_40  0x255
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_55_48
#define DP_TEST_80BIT_CUSTOM_PATTERN_55_48  0x256
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_63_56
#define DP_TEST_80BIT_CUSTOM_PATTERN_63_56  0x257
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_71_64
#define DP_TEST_80BIT_CUSTOM_PATTERN_71_64  0x258
#endif
#ifndef DP_TEST_80BIT_CUSTOM_PATTERN_79_72
#define DP_TEST_80BIT_CUSTOM_PATTERN_79_72  0x259
#endif

#ifndef DP_BRANCH_REVISION_START
#define DP_BRANCH_REVISION_START            0x509
#endif

#ifndef DP_DP13_DPCD_REV
#define DP_DP13_DPCD_REV                    0x2200
#endif
#ifndef DP_DP13_MAX_LINK_RATE
#define DP_DP13_MAX_LINK_RATE               0x2201
#endif

#ifndef DP_LANE0_1_STATUS_ESI
#define DP_LANE0_1_STATUS_ESI                  0x200c /* status same as 0x202 */
#endif
#ifndef DP_LANE2_3_STATUS_ESI
#define DP_LANE2_3_STATUS_ESI                  0x200d /* status same as 0x203 */
#endif
#ifndef DP_LANE_ALIGN_STATUS_UPDATED_ESI
#define DP_LANE_ALIGN_STATUS_UPDATED_ESI       0x200e /* status same as 0x204 */
#endif
#ifndef DP_SINK_STATUS_ESI
#define DP_SINK_STATUS_ESI                     0x200f /* status same as 0x205 */
#endif

/* helper for handling conditionals in various for_each macros */
#ifndef for_each_if
#define for_each_if(condition) if (!(condition)) {} else
#endif

#endif /* AMDKCL_DRM_H */
