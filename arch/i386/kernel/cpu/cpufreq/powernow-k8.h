/*
 *   (c) 2003 Advanced Micro Devices, Inc.
 *  Your use of this code is subject to the terms and conditions of the
 *  GNU general public license version 2. See "../../../COPYING" or
 *  http://www.gnu.org/licenses/gpl.html
 */

/* processor's cpuid instruction support */
#define CPUID_PROCESSOR_SIGNATURE             1	/* function 1               */
#define CPUID_F1_FAM                 0x00000f00	/* family mask              */
#define CPUID_F1_XFAM                0x0ff00000	/* extended family mask     */
#define CPUID_F1_MOD                 0x000000f0	/* model mask               */
#define CPUID_F1_STEP                0x0000000f	/* stepping level mask      */
#define CPUID_XFAM_MOD               0x0ff00ff0	/* xtended fam, fam + model */
#define ATHLON64_XFAM_MOD            0x00000f40	/* xtended fam, fam + model */
#define OPTERON_XFAM_MOD             0x00000f50	/* xtended fam, fam + model */
#define ATHLON64_REV_C0                       8
#define CPUID_GET_MAX_CAPABILITIES   0x80000000
#define CPUID_FREQ_VOLT_CAPABILITIES 0x80000007
#define P_STATE_TRANSITION_CAPABLE            6

/* Model Specific Registers for p-state transitions. MSRs are 64-bit. For     */
/* writes (wrmsr - opcode 0f 30), the register number is placed in ecx, and   */
/* the value to write is placed in edx:eax. For reads (rdmsr - opcode 0f 32), */
/* the register number is placed in ecx, and the data is returned in edx:eax. */

#define MSR_FIDVID_CTL      0xc0010041
#define MSR_FIDVID_STATUS   0xc0010042

/* Field definitions within the FID VID Low Control MSR : */
#define MSR_C_LO_INIT_FID_VID     0x00010000
#define MSR_C_LO_NEW_VID          0x00001f00
#define MSR_C_LO_NEW_FID          0x0000002f
#define MSR_C_LO_VID_SHIFT        8

/* Field definitions within the FID VID High Control MSR : */
#define MSR_C_HI_STP_GNT_TO       0x000fffff

/* Field definitions within the FID VID Low Status MSR : */
#define MSR_S_LO_CHANGE_PENDING   0x80000000	/* cleared when completed */
#define MSR_S_LO_MAX_RAMP_VID     0x1f000000
#define MSR_S_LO_MAX_FID          0x003f0000
#define MSR_S_LO_START_FID        0x00003f00
#define MSR_S_LO_CURRENT_FID      0x0000003f

/* Field definitions within the FID VID High Status MSR : */
#define MSR_S_HI_MAX_WORKING_VID  0x001f0000
#define MSR_S_HI_START_VID        0x00001f00
#define MSR_S_HI_CURRENT_VID      0x0000001f

/* fids (frequency identifiers) are arranged in 2 tables - lo and hi */
#define LO_FID_TABLE_TOP     6
#define HI_FID_TABLE_BOTTOM  8

#define LO_VCOFREQ_TABLE_TOP    1400	/* corresponding vco frequency values */
#define HI_VCOFREQ_TABLE_BOTTOM 1600

#define MIN_FREQ_RESOLUTION  200 /* fids jump by 2 matching freq jumps by 200 */

#define MAX_FID 0x2a	/* Spec only gives FID values as far as 5 GHz */

#define LEAST_VID 0x1e	/* Lowest (numerically highest) useful vid value */

#define MIN_FREQ 800	/* Min and max freqs, per spec */
#define MAX_FREQ 5000

#define INVALID_FID_MASK 0xffffffc1  /* not a valid fid if these bits are set */

#define INVALID_VID_MASK 0xffffffe0  /* not a valid vid if these bits are set */

#define STOP_GRANT_5NS 1 /* min poss memory access latency for voltage change */

#define PLL_LOCK_CONVERSION (1000/5) /* ms to ns, then divide by clock period */

#define MAXIMUM_VID_STEPS 1  /* Current cpus only allow a single step of 25mV */

#define VST_UNITS_20US 20   /* Voltage Stabalization Time is in units of 20us */

/*
Version 1.4 of the PSB table. This table is constructed by BIOS and is
to tell the OS's power management driver which VIDs and FIDs are
supported by this particular processor. This information is obtained from
the data sheets for each processor model by the system vendor and
incorporated into the BIOS.
If the data in the PSB / PST is wrong, then this driver will program the
wrong values into hardware, which is very likely to lead to a crash.
*/

#define PSB_ID_STRING      "AMDK7PNOW!"
#define PSB_ID_STRING_LEN  10

#define PSB_VERSION_1_4  0x14

struct psb_s {
	u8 signature[10];
	u8 tableversion;
	u8 flags1;
	u16 voltagestabilizationtime;
	u8 flags2;
	u8 numpst;
	u32 cpuid;
	u8 plllocktime;
	u8 maxfid;
	u8 maxvid;
	u8 numpstates;
};

/* Pairs of fid/vid values are appended to the version 1.4 PSB table. */
struct pst_s {
	u8 fid;
	u8 vid;
};

#ifdef DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) do { } while(0)
#endif

static inline int core_voltage_pre_transition(u32 reqvid);
static inline int core_voltage_post_transition(u32 reqvid);
static inline int core_frequency_transition(u32 reqfid);
static int powernowk8_verify(struct cpufreq_policy *pol);
static int powernowk8_target(struct cpufreq_policy *pol, unsigned targfreq,
		      unsigned relation);
static int __init powernowk8_cpu_init(struct cpufreq_policy *pol);
