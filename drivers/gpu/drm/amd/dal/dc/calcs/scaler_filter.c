/* Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "include/fixed31_32.h"

#include "scaler_filter.h"

enum {
	DOWN_DB_SCALES = 8,
	DOWN_DB_POINTS = 11,

	UP_DB_SCALES = 1,
	UP_DB_POINTS = 7,

	MIN_SHARPNESS = -50,
	MAX_SHARPNESS = 50,

	CONST_DIVIDER = 10000000,

	MAX_HOR_DOWNSCALE = 1666000, /* 1:6  */
	MAX_VER_DOWNSCALE = 1666000, /* 1:6  */

	MAX_HOR_UPSCALE = 160000000, /* 16:1 */
	MAX_VER_UPSCALE = 160000000, /* 16:1 */

	THRESHOLDRATIOLOW = 8000000, /* 0.8 */
	THRESHOLDRATIOUP = 10000000, /* 1.0 */

	DOWN_DB_FUZZY = -120411996, /* -12.041200 */
	DOWN_DB_FLAT = -60205998, /* -6.020600 */
	DOWN_DB_SHARP = -10000000, /* -1.000000 */

	UP_DB_FUZZY = -60205998, /* -6.020600 */
	UP_DB_FLAT = 0,
	UP_DB_SHARP = 60205998 /* 6.020600 */
};

static inline struct fixed31_32 max_hor_downscale(void)
{
	return dal_fixed31_32_from_fraction(MAX_HOR_DOWNSCALE, CONST_DIVIDER);
}

static inline struct fixed31_32 max_ver_downscale(void)
{
	return dal_fixed31_32_from_fraction(MAX_VER_DOWNSCALE, CONST_DIVIDER);
}

static inline struct fixed31_32 max_hor_upscale(void)
{
	return dal_fixed31_32_from_fraction(MAX_HOR_UPSCALE, CONST_DIVIDER);
}

static inline struct fixed31_32 max_ver_upscale(void)
{
	return dal_fixed31_32_from_fraction(MAX_VER_UPSCALE, CONST_DIVIDER);
}

static inline struct fixed31_32 threshold_ratio_low(void)
{
	return dal_fixed31_32_from_fraction(THRESHOLDRATIOLOW, CONST_DIVIDER);
}

static inline struct fixed31_32 threshold_ratio_up(void)
{
	return dal_fixed31_32_from_fraction(THRESHOLDRATIOUP, CONST_DIVIDER);
}

static inline struct fixed31_32 down_db_fuzzy(void)
{
	return dal_fixed31_32_from_fraction(DOWN_DB_FUZZY, CONST_DIVIDER);
}

static inline struct fixed31_32 down_db_flat(void)
{
	return dal_fixed31_32_from_fraction(DOWN_DB_FLAT, CONST_DIVIDER);
}

static inline struct fixed31_32 down_db_sharp(void)
{
	return dal_fixed31_32_from_fraction(DOWN_DB_SHARP, CONST_DIVIDER);
}

static inline struct fixed31_32 up_db_fuzzy(void)
{
	return dal_fixed31_32_from_fraction(UP_DB_FUZZY, CONST_DIVIDER);
}

static inline struct fixed31_32 up_db_flat(void)
{
	return dal_fixed31_32_from_fraction(UP_DB_FLAT, CONST_DIVIDER);
}

static inline struct fixed31_32 up_db_sharp(void)
{
	return dal_fixed31_32_from_fraction(UP_DB_SHARP, CONST_DIVIDER);
}

static const int32_t
	downscaling_db_table[][DOWN_DB_SCALES + 1][DOWN_DB_POINTS] = {
	/* 3 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			14302719,	14302719,	14302719,
			10000000,	99999,		99999,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			14302339,	14302339,	14302339,
			10000000,	4452010,	99999,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			14302760,	14302760,	14302760,
			10000000,	7826979,	5258399,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			14302819,	14302819,	14302819,
			10000000,	8669400,	7414469,
			4422729,	99999,		99999,
			99999,		99999
		},
		{
			14302730,	14302730,	12791190,
			10000000,	9045640,	8180170,
			6477950,	4599249,	2019010,
			99999,		99999
		},
		{
			14302699,	14302699,	12067849,
			10000000,	9236029,	8541280,
			7252740,	6021010,	4820120,
			3511950,	1769340
		},
		{
			14302710,	14302710,	11783510,
			10000000,	9325690,	8704419,
			7595670,	6583020,	5652850,
			4749999,	3847680
		},
		{
			14302920,	14302920,	11709250,
			10000000,	9345560,	8754609,
			7692559,	6738259,	5878239,
			5057529,	4264070
		}
	},
	/* 4 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			14308999,	14308999,	14308999,
			10000000,	99999,		99999,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			14308999,	14308999,	14308999,
			10000000,	6311039,	99999,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			14308999,	14308999,	14308999,
			10000000,	8526669,	6832849,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			14308999,	14308999,	12110630,
			10000000,	9117940,	8230940,
			6320130,	3719770,	99999,
			99999,		99999
		},
		{
			14308999,	14308999,	11474980,
			10000000,	9370139,	8771979,
			7601270,	6440780,	5249999,
			3887520,	2039040
		},
		{
			14308999,	13084859,	11179579,
			10000000,	9495180,	9016919,
			8134520,	7311699,	6560329,
			5845720,	5155519
		},
		{
			14308999,	12576600,	11048669,
			10000000,	9550499,	9132360,
			8368729,	7679399,	7073119,
			6520900,	6015530
		},
		{
			14308999,	12448530,	11007410,
			10000000,	9566799,	9165279,
			8435800,	7785279,	7215780,
			6701470,	6240640
		}
	},
	/* 5 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			8971139,	8971139,	8971139,
			10000000,	99999,		99999,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			9466379,	9466379,	9466379,
			10000000,	5648760,	3834280,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			15000000,	15000000,	14550110,
			10000000,	7121120,	5994579,
			4314630,	2606149,	99999,
			99999,		99999
		},
		{
			15000000,	15000000,	13047469,
			10000000,	8368809,	7343569,
			5970299,	4924620,	4029389,
			3171139,	2276369
		},
		{
			15000000,	14157199,	11897679,
			10000000,	9166659,	8444600,
			7287240,	6374719,	5615460,
			4949580,	4350199
		},
		{
			15000000,	12877819,	11224579,
			10000000,	9488620,	9016109,
			8203780,	7500000,	6883730,
			6326839,	5818459
		},
		{
			14733040,	12233200,	10939040,
			10000000,	9608929,	9250000,
			8623390,	8076940,	7606369,
			7177749,	6785169
		},
		{
			14627330,	12046170,	10862360,
			10000000,	9639260,	9312710,
			8737679,	8242470,	7815709,
			7432209,	7082970
		}
	},
	/* 6 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			8231559,	8231559,	8231559,
			10000000,	99999,		99999,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			8353310,	8353310,	8353310,
			10000000,	5504879,	4310710,
			870359,		99999,		99999,
			99999,		99999
		},
		{
			8643479,	8643479,	8643479,
			10000000,	6483510,	5768150,
			4630779,	3580690,	2501940,
			1015309,	99999
		},
		{
			15000000,	15000000,	13493930,
			10000000,	7516040,	6802409,
			5824409,	5080109,	4454280,
			3896749,	3386510
		},
		{
			15000000,	14055930,	12321079,
			10000000,	8872389,	8090410,
			7035570,	6281229,	5676810,
			5165010,	4717260
		},
		{
			15000000,	12915290,	11311399,
			10000000,	9460610,	8988440,
			8202149,	7548679,	6999999,
			6510639,	6065719
		},
		{
			14310129,	12140829,	10901659,
			10000000,	9635019,	9307180,
			8740929,	8263260,	7858849,
			7499330,	7170130
		},
		{
			13815449,	11911309,	10801299,
			10000000,	9669629,	9380580,
			8878319,	8452050,	8097199,
			7785030,	7504299
		}
	},
	/* 7 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			10616660,	10616660,	10616660,
			10000000,	2646020,	99999,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			10099999,	10099999,	10099999,
			10000000,	4936839,	4112670,
			2729740,	896539,		99999,
			99999,		99999
		},
		{
			8345860,	8345860,	8345860,
			10000000,	6034079,	5371739,
			4466759,	3763799,	3155870,
			2588019,	2026730
		},
		{
			9298499,	9298499,	13768420,
			10000000,	7174239,	6524270,
			5670250,	5052099,	4549089,
			4115279,	3722150
		},
		{
			15000000,	14116940,	12563209,
			10000000,	8542140,	7782419,
			6865050,	6239479,	5758860,
			5351870,	4992800
		},
		{
			15000000,	12913750,	11306079,
			10000000,	9452580,	8969209,
			8168810,	7538409,	7029479,
			6603180,	6227809
		},
		{
			14390859,	11862809,	10757420,
			10000000,	9688709,	9404249,
			8904439,	8472480,	8099079,
			7765330,	7459110
		},
		{
			13752900,	11554559,	10637769,
			10000000,	9736120,	9499999,
			9079759,	8718389,	8408790,
			8134469,	7886120
		}
	},
	/* 8 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			11277090,	11277090,	11277090,
			10000000,	2949059,	99999,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			11196039,	11196039,	11196039,
			10000000,	4627540,	4018869,
			3018769,	2000000,	250770,
			99999,		99999
		},
		{
			10878369,	10878369,	10878369,
			10000000,	5657230,	5118110,
			4372630,	3809120,	3337709,
			2919510,	2535369
		},
		{
			9090089,	9090089,	13961290,
			10000000,	6929969,	6334999,
			5569829,	5019649,	4584150,
			4208610,	3876540
		},
		{
			15000000,	14173229,	12732659,
			10000000,	8267070,	7575380,
			6764540,	6218209,	5803539,
			5454990,	5146239
		},
		{
			15000000,	12928279,	11292259,
			10000000,	9447429,	8954229,
			8141599,	7516989,	7039459,
			6649519,	6316819
		},
		{
			14661350,	11638879,	10665880,
			10000000,	9722669,	9464690,
			9013469,	8613470,	8266339,
			7949870,	7663450
		},
		{
			13861900,	11311980,	10543940,
			10000000,	9772019,	9565100,
			9198870,	8881340,	8609200,
			8365769,	8147500
		}
	},
	/* 9 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{	10099999,	10099999,	10099999,
			10000000,	2939159,	1526470,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			11726609,	11726609,	11726609,
			10000000,	4329420,	3805609,
			3030480,	2363760,	1732099,
			980139,		99999
		},
		{
			10949269,	10949269,	10949269,
			10000000,	5452589,	4946640,
			4277969,	3790729,	3392640,
			3048950,	2750000
		},
		{
			8830279,	8830279,	14084529,
			10000000,	6743149,	6182519,
			5482980,	5000000,	4622060,
			4303340,	4022600
		},
		{
			9709150,	14111399,	12800760,
			10000000,	7989749,	7445629,
			6741260,	6241980,	5857459,
			5534989,	5255370
		},
		{
			15000000,	12830289,	11489900,
			10000000,	9302089,	8767340,
			8025540,	7500000,	7100800,
			6768149,	6481850
		},
		{
			14873609,	11576000,	10650579,
			10000000,	9731360,	9483649,
			9054650,	8680559,	8358049,
			8066400,	7802420
		},
		{
			12981410,	11185950,	10491620,
			10000000,	9795730,	9611030,
			9286710,	9007279,	8768100,
			8553469,	8361340
		}
	},
	/* 10 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			8993279,	8993279,	8993279,
			10000000,	2921360,	1905619,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			9064850,	9064850,	9064850,
			10000000,	4095619,	3655839,
			3021000,	2500000,	2031680,
			1566990,	1055440
		},
		{
			11043460,	11043460,	11043460,
			10000000,	5287479,	4816150,
			4208439,	3769229,	3418970,
			3117449,	2850320
		},
		{
			8651900,	8651900,	14169909,
			10000000,	6596950,	6071490,
			5423219,	4980779,	4644620,
			4362219,	4114899
		},
		{
			9246050,	14055370,	12832759,
			10000000,	7831320,	7369570,
			6731680,	6262450,	5897690,
			5592269,	5328789
		},
		{
			15000000,	12770450,	11642129,
			10000000,	9120929,	8601920,
			7946630,	7490440,	7140589,
			6847490,	6593719
		},
		{
			14062479,	11541219,	10644329,
			10000000,	9736120,	9495139,
			9080520,	8724340,	8419489,
			8146359,	7899820
		},
		{
			12507469,	11102950,	10457479,
			10000000,	9811149,	9641249,
			9344969,	9090980,	8875219,
			8684499,	8513180
		}
	},
	/* 11 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			10099509,	10099509,	10099509,
			10000000,	2788810,	2054850,
			99999,		99999,		99999,
			99999,		99999
		},
		{
			8872069,	8872069,	8872069,
			10000000,	3929649,	3522840,
			2963230,	2527720,	2157579,
			1823610,	1500000
		},
		{
			10099999,	10099999,	10099999,
			10000000,	5155599,	4712319,
			4154500,	3759450,	3448629,
			3183139,	2948490
		},
		{
			10511649,	10511649,	14216580,
			10000000,	6445930,	5988820,
			5401239,	4988409,	4673399,
			4410479,	4181599
		},
		{
			9170889,	14003310,	12949769,
			10000000,	7684900,	7250000,
			6670129,	6255810,	5934680,
			5664110,	5427970
		},
		{
			15000000,	12763030,	11734730,
			10000000,	8958870,	8478559,
			7893459,	7489529,	7179200,
			6917790,	6688359
		},
		{
			14634610,	11491880,	10619130,
			10000000,	9744859,	9509819,
			9102900,	8760340,	8463050,
			8202620,	7968729
		},
		{
			12415319,	10980290,	10405089,
			10000000,	9831910,	9680110,
			9413710,	9184579,	8987190,
			8813819,	8655819
		}
	},
	/* 12 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			10832400,	10832400,	10832400,
			10000000,	2700819,	2115820,
			750000,		99999,		99999,
			99999,		99999
		},
		{
			10747549,	10747549,	10747549,
			10000000,	3781630,	3415020,
			2914879,	2537429,	2221180,
			1943199,	1688420
		},
		{
			11630790,	11630790,	11630790,
			10000000,	5047429,	4631519,
			4113860,	3750000,	3469760,
			3229379,	3016360
		},
		{
			10780229,	10780229,	10780229,
			10000000,	6340010,	5935009,
			5387600,	4995940,	4695929,
			4446829,	4231610
		},
		{
			9055669,	13968739,	13037070,
			10000000,	7556660,	7149490,
			6625509,	6250000,	5958870,
			5713790,	5500869
		},
		{
			14614900,	12760740,	11806739,
			10000000,	8824530,	8388419,
			7857400,	7489010,	7206150,
			6968010,	6759889
		},
		{
			14894100,	11451840,	10598870,
			10000000,	9750000,	9521099,
			9122239,	8784019,	8494700,
			8243309,	8018680
		},
		{
			12298769,	10886880,	10367530,
			10000000,	9846829,	9708030,
			9464049,	9252949,	9072539,
			8910980,	8766649
		}
	},
	/* 13 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			10099999,	10099999,	10099999,
			10000000,	2574490,	2099110,
			1194889,	99999,		99999,
			99999,		99999
		},
		{
			10099999,	10099999,	10099999,
			10000000,	3679780,	3332070,
			2869139,	2530030,	2251899,
			2010450,	1793050
		},
		{
			9306690,	9306690,	9306690,
			10000000,	4964010,	4573009,
			4082309,	3742089,	3481810,
			3262990,	3070969
		},
		{
			10099999,	10099999,	10099999,
			10000000,	6217889,	5843269,
			5353810,	5000000,	4730190,
			4499999,	4301390
		},
		{
			8819990,	13964320,	13098440,
			10000000,	7454770,	7075160,
			6591439,	6250000,	5983970,
			5760229,	5564339
		},
		{
			14432849,	12727780,	11847709,
			10000000,	8695709,	8322049,
			7842620,	7500000,	7234820,
			7010849,	6814730
		},
		{
			15000000,	11440130,	10620100,
			10000000,	9742270,	9508739,
			9110010,	8782560,	8510140,
			8276290,	8069980
		},
		{
			12039999,	10825289,	10341939,
			10000000,	9858080,	9729740,
			9505100,	9310669,	9144560,
			8996559,	8862569
		}
	},
	/* 14 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			9289590,	9289590,	9289590,
			10000000,	2485270,	2084970,
			1362659,	250000,		99999,
			99999,		99999
		},
		{
			9484500,	9484500,	9484500,
			10000000,	3593840,	3263100,
			2833609,	2519409,	2267650,
			2050379,	1856749
		},
		{
			9237130,	9237130,	9237130,
			10000000,	4898909,	4527629,
			4057880,	3734529,	3490320,
			3287230,	3111050
		},
		{
			9543399,	9543399,	9543399,
			10000000,	6110230,	5772359,
			5328080,	5007240,	4757330,
			4545379,	4359109
		},
		{
			9032660,	9032660,	9032660,
			10000000,	7373520,	7016940,
			6565740,	6250000,	6002650,
			5794939,	5610830
		},
		{
			14351329,	12697319,	11875350,
			10000000,	8606730,	8275989,
			7833449,	7510430,	7257339,
			7043970,	6857690
		},
		{
			13286800,	11436090,	10643019,
			10000000,	9729470,	9491149,
			9096930,	8778640,	8519319,
			8299450,	8104829
		},
		{
			11838380,	10778709,	10322740,
			10000000,	9866499,	9746059,
			9535790,	9354810,	9200339,
			9063839,	8940430
		}
	},
	/* 15 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			9193199,	9193199,	9193199,
			10000000,	2400999,	2042409,
			1450179,	789309,		99999,
			99999,		99999
		},
		{
			10755189,	10755189,	10755189,
			10000000,	3532319,	3212479,
			2803660,	2510200,	2278629,
			2078720,	1899970
		},
		{
			8732669,	8732669,	8732669,
			10000000,	4821290,	4483030,
			4045079,	3737959,	3505080,
			3311960,	3143329
		},
		{
			9450280,	9450280,	9450280,
			10000000,	6040880,	5718960,
			5302609,	5004199,	4771710,
			4575310,	4404180
		},
		{
			10520930,	10520930,	10520930,
			10000000,	7298259,	6975160,
			6552690,	6250000,	6018469,
			5822089,	5648869
		},
		{
			14320160,	12683949,	11917040,
			10000000,	8541300,	8228710,
			7812070,	7509459,	7272909,
			7072560,	6895729
		},
		{
			15000000,	11434819,	10650700,
			10000000,	9723110,	9480339,
			9083300,	8771640,	8524850,
			8317480,	8135899
		},
		{
			11750520,	10722860,	10299190,
			10000000,	9875990,	9763770,
			9567070,	9397709,	9252669,
			9124029,	9008929
		}
	},
	/* 16 tap downscaling */
	{
		{
			60209999,	40000000,	20000000,
			0,		-10000000,	-20000000,
			-40000000,	-60209999,	-80000000,
			-100000000,	-120410003
		},
		{
			10612260,	10612260,	10612260,
			10000000,	2308720,	1999289,
			1495770,	1009820,	315460,
			99999,		99999
		},
		{
			9394969,	9394969,	9394969,
			10000000,	3462660,	3162190,
			2780120,	2508420,	2295179,
			2109449,	1943989
		},
		{
			10609409,	10609409,	10609409,
			10000000,	4749999,	4447000,
			4039109,	3746300,	3522360,
			3336620,	3177059
		},
		{
			9435039,	9435039,	9435039,
			10000000,	5978109,	5675160,
			5282300,	5000000,	4782429,
			4598149,	4438050
		},
		{
			10592620,	10592620,	10592620,
			10000000,	7244589,	6940630,
			6537730,	6250000,	6027920,
			5842260,	5680159
		},
		{
			14282959,	12678509,	11963449,
			10000000,	8484349,	8181620,
			7785459,	7500000,	7281309,
			7095699,	6932809
		},
		{
			15000000,	11434919,	10673819,
			10000000,	9708179,	9456859,
			9060000,	8760929,	8529940,
			8338279,	8172209
		},
		{
			11690390,	10668220,	10277210,
			10000000,	9884750,	9780330,
			9597110,	9439319,	9304260,
			9183580,	9075019
		}
	}
};

static const int32_t upscaling_db_table[][UP_DB_SCALES+1][UP_DB_POINTS] = {
	/* 3 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			14302920,	14302920,	11709250,
			10000000,
			8754609,	7692559,	6738259
		}
	},
	/*	4 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			14308999,	12448530,	11007410,
			10000000,	9165279,	8435800,
			7785279
		}
	},
	/* 5 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			14627330,	12046170,	10862360,
			10000000,
			9312710,	8737679,	8242470
		}
	},
	/* 6 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			13815449,	11911309,	10801299,
			10000000,
			9380580,	8878319,	8452050
		}
	},
	/* 7 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			13752900,	11554559,	10637769,
			10000000,
			9499999,	9079759,	8718389
		}
	},
	/* 8 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			13861900,	11311980,	10543940,
			10000000,
			9565100,	9198870,	8881340
		}
	},
	/* 9 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			12981410,	11185950,	10491620,
			10000000,
			9611030,	9286710,	9007279
		}
	},
	/* 10 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			12507469,	11102950,	10457479,
			10000000,
			9641249,	9344969,	9090980
		}
	},
	/* 11 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			12415319,	10980290,	10405089,
			10000000,
			9680110,	9413710,	9184579
		}
	},
	/* 12 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			12298769,	10886880,	10367530,
			10000000,
			9708030,	9464049,	9252949
		}
	},
	/* 13 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			12039999,	10825289,	10341939,
			10000000,
			9729740,	9505100,	9310669
		}
	},
	/* 14 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			11838380,	10778709,	10322740,
			10000000,
			9746059,	9535790,	9354810
		}
	},
	/* 15 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			11750520,	10722860,	10299190,
			10000000,
			9763770,	9567070,	9397709
		}
	},
	/* 16 tap upscaling */
	{
		{
			60209999,	40000000,	20000000,	0,
			-20000000,	-40000000,	-60209999
		},
		{
			11690390,	10668220,	10277210,
			10000000,
			9780330,	9597110,	9439319
		}
	}
};

static bool allocate_3d_storage(
	struct dc_context *ctx,
	struct fixed31_32 ****ptr,
	int32_t numberof_tables,
	int32_t numberof_rows,
	int32_t numberof_columns)
{
	int32_t indexof_table = 0;
	int32_t indexof_row = 0;

	struct fixed31_32 ***tables = dm_alloc(numberof_tables * sizeof(struct fixed31_32 **));

	if (!tables) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	while (indexof_table != numberof_tables) {
		struct fixed31_32 **rows = dm_alloc(numberof_rows * sizeof(struct fixed31_32 *));

		if (!rows) {
			BREAK_TO_DEBUGGER();
			--indexof_table;
			goto failure;
		}

		tables[indexof_table] = rows;

		while (indexof_row != numberof_rows) {
			struct fixed31_32 *columns = dm_alloc(numberof_columns * sizeof(struct fixed31_32));

			if (!columns) {
				BREAK_TO_DEBUGGER();
				--indexof_row;
				goto failure;
			}

			rows[indexof_row] = columns;

			++indexof_row;
		}

		indexof_row = 0;

		++indexof_table;
	}

	*ptr = tables;

	return true;

failure:

	while (indexof_table >= 0) {
		while (indexof_row >= 0) {
			dm_free(tables[indexof_table][indexof_row]);

			--indexof_row;
		}

		indexof_row = numberof_rows - 1;

		dm_free(tables[indexof_table]);

		--indexof_table;
	}

	dm_free(tables);

	return false;
}

static void destroy_3d_storage(
	struct dc_context *ctx,
	struct fixed31_32 ****ptr,
	uint32_t numberof_tables,
	uint32_t numberof_rows)
{
	struct fixed31_32 ***tables = *ptr;

	uint32_t indexof_table = 0;

	if (!tables)
		return;

	while (indexof_table != numberof_tables) {
		uint32_t indexof_row = 0;

		while (indexof_row != numberof_rows) {
			dm_free(tables[indexof_table][indexof_row]);

			++indexof_row;
		};

		dm_free(tables[indexof_table]);

		++indexof_table;
	};

	dm_free(tables);

	*ptr = NULL;
}

static bool create_downscaling_table(
	struct scaler_filter *filter)
{
	const int32_t numberof_tables =
		ARRAY_SIZE(downscaling_db_table);
	const int32_t numberof_rows =
		ARRAY_SIZE(downscaling_db_table[0]);
	const int32_t numberof_columns =
		ARRAY_SIZE(downscaling_db_table[0][0]);

	int32_t indexof_table = 0;

	if (!allocate_3d_storage(filter->ctx, &filter->downscaling_table,
		numberof_tables, numberof_rows, numberof_columns)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	while (indexof_table != numberof_tables) {
		struct fixed31_32 **table =
			filter->downscaling_table[indexof_table];

		int32_t indexof_row = 0;

		while (indexof_row != numberof_rows) {
			struct fixed31_32 *row = table[indexof_row];

			int32_t indexof_column = 0;

			while (indexof_column != numberof_columns) {
				row[indexof_column] =
dal_fixed31_32_from_fraction(
	downscaling_db_table[indexof_table][indexof_row][indexof_column],
	CONST_DIVIDER);

				++indexof_column;
			}

			++indexof_row;
		}

		++indexof_table;
	}

	return true;
}

static inline void destroy_downscaling_table(
	struct scaler_filter *filter)
{
	destroy_3d_storage(
		filter->ctx,
		&filter->downscaling_table,
		ARRAY_SIZE(downscaling_db_table),
		ARRAY_SIZE(downscaling_db_table[0]));
}

static bool create_upscaling_table(
	struct scaler_filter *filter)
{
	const int32_t numberof_tables =
		ARRAY_SIZE(upscaling_db_table);
	const int32_t numberof_rows =
		ARRAY_SIZE(upscaling_db_table[0]);
	const int32_t numberof_columns =
		ARRAY_SIZE(upscaling_db_table[0][0]);

	int32_t indexof_table = 0;

	if (!allocate_3d_storage(filter->ctx, &filter->upscaling_table,
		numberof_tables, numberof_rows, numberof_columns)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	while (indexof_table != numberof_tables) {
		struct fixed31_32 **table =
			filter->upscaling_table[indexof_table];

		int32_t indexof_row = 0;

		while (indexof_row != numberof_rows) {
			struct fixed31_32 *row = table[indexof_row];

			int32_t indexof_column = 0;

			while (indexof_column != numberof_columns) {
				row[indexof_column] =
dal_fixed31_32_from_fraction(
	upscaling_db_table[indexof_table][indexof_row][indexof_column],
	CONST_DIVIDER);

				++indexof_column;
			}

			++indexof_row;
		}

		++indexof_table;
	}

	return true;
}

static inline void destroy_upscaling_table(
	struct scaler_filter *filter)
{
	destroy_3d_storage(
		filter->ctx,
		&filter->upscaling_table,
		ARRAY_SIZE(upscaling_db_table),
		ARRAY_SIZE(upscaling_db_table[0]));
}

static bool same_filter_required(
	struct scaler_filter *filter,
	const struct scaler_filter_params *params,
	uint32_t src_size,
	uint32_t dst_size)
{
	if (!filter->src_size)
		return false;
	if (!filter->dst_size)
		return false;
	if (filter->src_size != src_size)
		return false;
	if (filter->dst_size != dst_size)
		return false;
	if (filter->params.taps != params->taps)
		return false;
	if (filter->params.phases != params->phases)
		return false;
	if (filter->params.sharpness != params->sharpness)
		return false;

	return true;
}

/*
 * @brief
 *                                            (scale_max - scale_min)
 * result = scale_min + (value - value_min) * -----------------------
 *                                            (value_max - value_min)
 */

static struct fixed31_32 interpolate(
	struct fixed31_32 value,
	struct fixed31_32 value_min,
	struct fixed31_32 value_max,
	struct fixed31_32 scale_min,
	struct fixed31_32 scale_max)
{
	return dal_fixed31_32_add(
		scale_min,
		dal_fixed31_32_div(
			dal_fixed31_32_mul(
				dal_fixed31_32_sub(
					value,
					value_min),
				dal_fixed31_32_sub(
					scale_max,
					scale_min)),
			dal_fixed31_32_sub(
				value_max,
				value_min)));
}

static bool map_sharpness(
	struct scaler_filter *filter,
	const struct scaler_filter_params *params,
	uint32_t src_size,
	uint32_t dst_size,
	struct fixed31_32 *attenuation,
	struct fixed31_32 *decibels_at_nyquist)
{
	struct fixed31_32 ratio = dal_fixed31_32_from_fraction(
		dst_size,
		src_size);

	const struct fixed31_32 sharp_flat =
		dal_fixed31_32_from_fraction(MIN_SHARPNESS + MAX_SHARPNESS, 2);

	struct fixed31_32 sharp_max =
		dal_fixed31_32_from_int(MAX_SHARPNESS);
	struct fixed31_32 sharp_min =
		dal_fixed31_32_from_int(MIN_SHARPNESS);

	uint32_t index = params->taps - 3;

	struct fixed31_32 ratio_low;
	struct fixed31_32 ratio_up;

	struct fixed31_32 db_min;
	struct fixed31_32 db_flat;
	struct fixed31_32 db_max;
	struct fixed31_32 db_value;

	uint32_t i0;
	uint32_t i1;
	uint32_t row0;
	uint32_t row1;

	int32_t sharp = params->sharpness;

	if (sharp < MIN_SHARPNESS)
		sharp = MIN_SHARPNESS;
	else if (sharp > MAX_SHARPNESS)
		sharp = MAX_SHARPNESS;

	if (params->flags.bits.HORIZONTAL) {
		if (dal_fixed31_32_lt(ratio, max_hor_downscale())) {
			BREAK_TO_DEBUGGER();
			return false;
		} else if (dal_fixed31_32_lt(
			max_hor_upscale(), ratio)) {
			BREAK_TO_DEBUGGER();
			return false;
		}
	} else {
		if (dal_fixed31_32_lt(ratio, max_ver_downscale())) {
			BREAK_TO_DEBUGGER();
			return false;
		} else if (dal_fixed31_32_lt(
			max_ver_upscale(), ratio)) {
			BREAK_TO_DEBUGGER();
			return false;
		}
	}

	if (dst_size >= src_size) {
		if (sharp < 0) {
			db_max = up_db_flat();
			db_min = up_db_fuzzy();

			sharp_max = sharp_flat;
		} else {
			db_max = up_db_sharp();
			db_min = up_db_flat();

			sharp_min = sharp_flat;
		}

		db_value = interpolate(
			dal_fixed31_32_from_int(sharp),
			sharp_min, sharp_max,
			db_min, db_max);

		i0 = 0;

		while (dal_fixed31_32_lt(
			db_value, filter->upscaling_table[index][0][i0]) &&
			(i0 < UP_DB_POINTS - 1))
			++i0;

		i1 = i0 + 1;

		if (i0 == UP_DB_POINTS - 1)
			i1 = i0--;

		sharp_max = filter->upscaling_table[index][1][i0];
		sharp_min = filter->upscaling_table[index][1][i1];

		db_max = filter->upscaling_table[index][0][i0];
		db_min = filter->upscaling_table[index][0][i1];

		*attenuation = interpolate(
			db_value,
			db_max, db_min,
			sharp_max, sharp_min);

		*decibels_at_nyquist = db_value;

		return true;
	} else if ((5 * dst_size) < (src_size << 2)) {
		if (sharp < 0) {
			db_max = down_db_flat();
			db_min = down_db_fuzzy();

			sharp_max = sharp_flat;
		} else {
			db_max = down_db_sharp();
			db_min = down_db_flat();

			sharp_min = sharp_flat;
		}

		db_value = interpolate(
			dal_fixed31_32_from_int(sharp),
			sharp_min, sharp_max,
			db_min, db_max);
	} else {
		struct fixed31_32 db_value_min =
			filter->downscaling_table[index][0][0];

		struct fixed31_32 db_value_max =
			filter->downscaling_table[index][0][DOWN_DB_POINTS - 1];

		db_min = interpolate(
			ratio,
			threshold_ratio_low(), threshold_ratio_up(),
			down_db_fuzzy(), up_db_fuzzy());

		db_flat = interpolate(
			ratio,
			threshold_ratio_low(), threshold_ratio_up(),
			down_db_flat(), up_db_flat());

		db_max = interpolate(
			ratio,
			threshold_ratio_low(), threshold_ratio_up(),
			down_db_sharp(), up_db_sharp());

		if (sharp < 0) {
			db_max = db_flat;

			db_value = interpolate(
				dal_fixed31_32_from_int(sharp),
				sharp_min, dal_fixed31_32_zero,
				db_min, db_max);
		} else {
			db_min = db_flat;

			db_value = interpolate(
				dal_fixed31_32_from_int(sharp),
				dal_fixed31_32_zero, sharp_max,
				db_min, db_max);
		}

		if (dal_fixed31_32_lt(db_value_min, db_value))
			db_value = db_value_min;
		else if (dal_fixed31_32_lt(db_value, db_value_max))
			db_value = db_value_max;
	}

	i1 = 0;

	while (dal_fixed31_32_lt(db_value,
		filter->downscaling_table[index][0][i1]) &&
		(i1 < DOWN_DB_POINTS - 1))
		++i1;

	if (i1 == 0)
		i0 = i1++;
	else
		i0 = i1 - 1;

	row0 = dal_fixed31_32_round(
		dal_fixed31_32_mul_int(ratio, DOWN_DB_SCALES));

	if (dal_fixed31_32_lt(
		dal_fixed31_32_from_fraction(row0, DOWN_DB_SCALES), ratio)) {
		row1 = row0 + 1;

		if (row1 > DOWN_DB_SCALES) {
			row1 = DOWN_DB_SCALES;
			row0 = row1 - 1;
		}
	} else {
		row1 = row0--;

		if (row0 < 1) {
			row0 = 1;
			row1 = 2;
		}
	}

	ratio_low = dal_fixed31_32_from_fraction(row0, DOWN_DB_SCALES);
	ratio_up = dal_fixed31_32_from_fraction(row1, DOWN_DB_SCALES);

	sharp_max = interpolate(
		ratio,
		ratio_low, ratio_up,
		filter->downscaling_table[index][row0][i0],
		filter->downscaling_table[index][row1][i0]);

	sharp_min = interpolate(
		ratio,
		ratio_low, ratio_up,
		filter->downscaling_table[index][row0][i1],
		filter->downscaling_table[index][row1][i1]);

	db_max = filter->downscaling_table[index][0][i0];
	db_min = filter->downscaling_table[index][0][i1];

	*attenuation = interpolate(
		db_value,
		db_max, db_min,
		sharp_max, sharp_min);

	*decibels_at_nyquist = db_value;

	return true;
}

static inline struct fixed31_32 lanczos(
	struct fixed31_32 x,
	struct fixed31_32 a2)
{
	return dal_fixed31_32_mul(
		dal_fixed31_32_sinc(x),
		dal_fixed31_32_sinc(
			dal_fixed31_32_mul(x, a2)));
}

static bool generate_filter(
	struct scaler_filter *filter,
	const struct scaler_filter_params *params,
	struct fixed31_32 attenuation,
	struct fixed31_32 *ringing)
{
	uint32_t n = params->phases * params->taps;

	uint32_t coefficients_quantity = n;
	uint32_t coefficients_sum_quantity = params->phases;

	uint32_t i;
	uint32_t i_limit;
	uint32_t j;
	uint32_t m;

	struct fixed31_32 attenby2;

	struct fixed31_32 a_max = dal_fixed31_32_zero;
	struct fixed31_32 a_min = dal_fixed31_32_zero;

	if (filter->coefficients_quantity < coefficients_quantity) {
		if (filter->coefficients) {
			dm_free(filter->coefficients);

			filter->coefficients = NULL;
			filter->coefficients_quantity = 0;
		}

		filter->coefficients = dm_alloc(coefficients_quantity * sizeof(struct fixed31_32));

		if (!filter->coefficients) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		filter->coefficients_quantity = coefficients_quantity;
	}

	i = 0;

	while (i != filter->coefficients_quantity) {
		filter->coefficients[i] = dal_fixed31_32_zero;

		++i;
	}

	if (filter->coefficients_sum_quantity < coefficients_sum_quantity) {
		if (filter->coefficients_sum) {
			dm_free(filter->coefficients_sum);

			filter->coefficients_sum = NULL;
			filter->coefficients_sum_quantity = 0;
		}

		filter->coefficients_sum = dm_alloc(coefficients_sum_quantity * sizeof(struct fixed31_32));

		if (!filter->coefficients_sum) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		filter->coefficients_sum_quantity = coefficients_sum_quantity;
	}

	i = 0;

	while (i != filter->coefficients_sum_quantity) {
		filter->coefficients_sum[i] = dal_fixed31_32_zero;

		++i;
	}

	m = 0;

	attenby2 = dal_fixed31_32_div_int(
		dal_fixed31_32_mul_int(attenuation, params->taps), 2);

	i = 1;

	while (i <= params->taps) {
		j = 0;

		while (j != params->phases) {
			struct fixed31_32 x = dal_fixed31_32_mul(
				dal_fixed31_32_pi,
				dal_fixed31_32_from_fraction(
					(int64_t)(m << 1) - n, n));

			uint32_t index =
				(params->taps - i) * params->phases + j;

			filter->coefficients[index] = lanczos(x, attenby2);

			++m;

			++j;
		}

		++i;
	}

	i = 0;

	while (i != params->phases) {
		filter->coefficients_sum[i] = dal_fixed31_32_zero;

		m = i;

		j = 0;

		while (j != params->taps) {
			filter->coefficients_sum[i] =
				dal_fixed31_32_add(
					filter->coefficients_sum[i],
					filter->coefficients[m]);

			m += params->phases;

			++j;
		}

		++i;
	}

	i = 0;

	while (i != params->phases) {
		m = i;

		j = 0;

		while (j != params->taps) {
			filter->coefficients[m] =
				dal_fixed31_32_div(
					filter->coefficients[m],
					filter->coefficients_sum[i]);

			m += params->phases;

			++j;
		}

		++i;
	}

	i = 0;
	i_limit = (params->phases >> 1) + 1;

	while (i != i_limit) {
		m = i;

		j = 0;

		while (j != params->taps) {
			struct fixed31_32 tmp = filter->coefficients[m];

			filter->filter[i * params->taps + j] = tmp;

			if (dal_fixed31_32_lt(
				tmp, dal_fixed31_32_zero) &&
				dal_fixed31_32_lt(tmp, a_min))
				a_min = tmp;
			else if (dal_fixed31_32_lt(
				dal_fixed31_32_zero, tmp) &&
				dal_fixed31_32_lt(a_max, tmp))
				a_max = tmp;

			m += params->phases;

			++j;
		}

		++i;
	}

	if (dal_fixed31_32_eq(a_min, dal_fixed31_32_zero))
		*ringing = dal_fixed31_32_from_int(100);
	else
		*ringing = dal_fixed31_32_min(
			dal_fixed31_32_abs(
				dal_fixed31_32_div(a_max, a_min)),
			dal_fixed31_32_from_int(100));

	return true;
}

static bool construct_scaler_filter(
	struct dc_context *ctx,
	struct scaler_filter *filter)
{
	filter->src_size = 0;
	filter->dst_size = 0;
	filter->filter = NULL;
	filter->integer_filter = NULL;
	filter->filter_size_allocated = 0;
	filter->filter_size_effective = 0;
	filter->coefficients = NULL;
	filter->coefficients_quantity = 0;
	filter->coefficients_sum = NULL;
	filter->coefficients_sum_quantity = 0;
	filter->downscaling_table = NULL;
	filter->upscaling_table = NULL;
	filter->ctx = ctx;

	if (!create_downscaling_table(filter)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!create_upscaling_table(filter)) {
		BREAK_TO_DEBUGGER();
		destroy_downscaling_table(filter);
		return false;
	}

	return true;
}

static void destruct_scaler_filter(
	struct scaler_filter *filter)
{
	if (filter->coefficients_sum)
		dm_free(filter->coefficients_sum);

	if (filter->coefficients)
		dm_free(filter->coefficients);

	if (filter->integer_filter)
		dm_free(filter->integer_filter);

	if (filter->filter)
		dm_free(filter->filter);

	destroy_upscaling_table(filter);

	destroy_downscaling_table(filter);
}

struct scaler_filter *dal_scaler_filter_create(struct dc_context *ctx)
{
	struct scaler_filter *filter =
		dm_alloc(sizeof(struct scaler_filter));

	if (!filter) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (construct_scaler_filter(ctx, filter))
		return filter;

	BREAK_TO_DEBUGGER();

	dm_free(filter);

	return NULL;
}

bool dal_scaler_filter_generate(
	struct scaler_filter *filter,
	const struct scaler_filter_params *params,
	uint32_t src_size,
	uint32_t dst_size)
{
	uint32_t filter_size_required;

	struct fixed31_32 attenuation;
	struct fixed31_32 decibels_at_nyquist;
	struct fixed31_32 ringing;

	if (!params) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if ((params->taps < 3) || (params->taps > 16)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!src_size || !dst_size) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (same_filter_required(filter, params, src_size, dst_size))
		return true;

	filter_size_required =
		params->taps * ((params->phases >> 1) + 1);

	if (filter_size_required > filter->filter_size_allocated) {
		if (filter->filter) {
			dm_free(filter->filter);

			filter->filter = 0;
			filter->filter_size_allocated = 0;
		}

		filter->filter = dm_alloc(filter_size_required * sizeof(struct fixed31_32));

		if (!filter->filter) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		if (filter->integer_filter) {
			dm_free(filter->integer_filter);

			filter->integer_filter = 0;
		}

		filter->integer_filter = dm_alloc(filter_size_required * sizeof(uint32_t));

		if (!filter->integer_filter) {
			BREAK_TO_DEBUGGER();
			return false;
		}

		filter->filter_size_allocated = filter_size_required;
	}

	filter->filter_size_effective = filter_size_required;

	if (!map_sharpness(filter, params, src_size, dst_size,
		&attenuation, &decibels_at_nyquist)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!generate_filter(filter, params, attenuation, &ringing)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	filter->params = *params;
	filter->src_size = src_size;
	filter->dst_size = dst_size;

	return true;
}

const struct fixed31_32 *dal_scaler_filter_get(
	const struct scaler_filter *filter,
	uint32_t **data,
	uint32_t *number)
{
	if (!number) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (!data) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	*number = filter->filter_size_effective;
	*data = filter->integer_filter;

	return filter->filter;
}

void dal_scaler_filter_destroy(
	struct scaler_filter **filter)
{
	if (!filter || !*filter) {
		BREAK_TO_DEBUGGER();
		return;
	}

	destruct_scaler_filter(*filter);

	dm_free(*filter);

	*filter = NULL;
}
