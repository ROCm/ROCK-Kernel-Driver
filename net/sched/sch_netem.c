/*
 * net/sched/sch_netem.c	Network emulator
 *
 * 		This program is free software; you can redistribute it and/or
 * 		modify it under the terms of the GNU General Public License
 * 		as published by the Free Software Foundation; either version
 * 		2 of the License, or (at your option) any later version.
 *
 * Authors:	Stephen Hemminger <shemminger@osdl.org>
 *		Catalin(ux aka Dino) BOIE <catab at umbrella dot ro>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>

#include <net/pkt_sched.h>

/*	Network emulator
 *
 *	This scheduler can alters spacing and order
 *	Similar to NISTnet and BSD Dummynet.
 */

struct netem_sched_data {
	struct Qdisc	*qdisc;
	struct sk_buff_head delayed;
	struct timer_list timer;

	u32 latency;
	u32 loss;
	u32 limit;
	u32 counter;
	u32 gap;
	u32 jitter;
};

/* Time stamp put into socket buffer control block */
struct netem_skb_cb {
	psched_time_t	time_to_send;
};

/* This is the distribution table for the normal distribution produced
 * with NISTnet tools.
 * The entries represent a scaled inverse of the cumulative distribution
 * function.
 */
#define TABLESIZE	2048
#define TABLEFACTOR	8192

static const short disttable[TABLESIZE] = {
	-31473,		-26739,		-25226,		-24269,
	-23560,		-22993,		-22518,		-22109,
	-21749,		-21426,		-21133,		-20865,
	-20618,		-20389,		-20174,		-19972,
	-19782,		-19601,		-19430,		-19267,
	-19112,		-18962,		-18819,		-18681,
	-18549,		-18421,		-18298,		-18178,
	-18062,		-17950,		-17841,		-17735,
	-17632,		-17532,		-17434,		-17339,
	-17245,		-17155,		-17066,		-16979,
	-16894,		-16811,		-16729,		-16649,
	-16571,		-16494,		-16419,		-16345,
	-16272,		-16201,		-16130,		-16061,
	-15993,		-15926,		-15861,		-15796,
	-15732,		-15669,		-15607,		-15546,
	-15486,		-15426,		-15368,		-15310,
	-15253,		-15196,		-15140,		-15086,
	-15031,		-14977,		-14925,		-14872,
	-14821,		-14769,		-14719,		-14669,
	-14619,		-14570,		-14522,		-14473,
	-14426,		-14379,		-14332,		-14286,
	-14241,		-14196,		-14150,		-14106,
	-14062,		-14019,		-13976,		-13933,
	-13890,		-13848,		-13807,		-13765,
	-13724,		-13684,		-13643,		-13604,
	-13564,		-13525,		-13486,		-13447,
	-13408,		-13370,		-13332,		-13295,
	-13258,		-13221,		-13184,		-13147,
	-13111,		-13075,		-13040,		-13004,
	-12969,		-12934,		-12899,		-12865,
	-12830,		-12796,		-12762,		-12729,
	-12695,		-12662,		-12629,		-12596,
	-12564,		-12531,		-12499,		-12467,
	-12435,		-12404,		-12372,		-12341,
	-12310,		-12279,		-12248,		-12218,
	-12187,		-12157,		-12127,		-12097,
	-12067,		-12038,		-12008,		-11979,
	-11950,		-11921,		-11892,		-11863,
	-11835,		-11806,		-11778,		-11750,
	-11722,		-11694,		-11666,		-11639,
	-11611,		-11584,		-11557,		-11530,
	-11503,		-11476,		-11450,		-11423,
	-11396,		-11370,		-11344,		-11318,
	-11292,		-11266,		-11240,		-11214,
	-11189,		-11164,		-11138,		-11113,
	-11088,		-11063,		-11038,		-11013,
	-10988,		-10964,		-10939,		-10915,
	-10891,		-10866,		-10843,		-10818,
	-10794,		-10770,		-10747,		-10723,
	-10700,		-10676,		-10652,		-10630,
	-10606,		-10583,		-10560,		-10537,
	-10514,		-10491,		-10469,		-10446,
	-10424,		-10401,		-10378,		-10356,
	-10334,		-10312,		-10290,		-10267,
	-10246,		-10224,		-10202,		-10180,
	-10158,		-10137,		-10115,		-10094,
	-10072,		-10051,		-10030,		-10009,
	-9988,		-9967,		-9945,		-9925,
	-9904,		-9883,		-9862,		-9842,
	-9821,		-9800,		-9780,		-9760,
	-9739,		-9719,		-9699,		-9678,
	-9658,		-9638,		-9618,		-9599,
	-9578,		-9559,		-9539,		-9519,
	-9499,		-9480,		-9461,		-9441,
	-9422,		-9402,		-9383,		-9363,
	-9344,		-9325,		-9306,		-9287,
	-9268,		-9249,		-9230,		-9211,
	-9192,		-9173,		-9155,		-9136,
	-9117,		-9098,		-9080,		-9062,
	-9043,		-9025,		-9006,		-8988,
	-8970,		-8951,		-8933,		-8915,
	-8897,		-8879,		-8861,		-8843,
	-8825,		-8807,		-8789,		-8772,
	-8754,		-8736,		-8718,		-8701,
	-8683,		-8665,		-8648,		-8630,
	-8613,		-8595,		-8578,		-8561,
	-8543,		-8526,		-8509,		-8492,
	-8475,		-8458,		-8441,		-8423,
	-8407,		-8390,		-8373,		-8356,
	-8339,		-8322,		-8305,		-8289,
	-8272,		-8255,		-8239,		-8222,
	-8206,		-8189,		-8172,		-8156,
	-8140,		-8123,		-8107,		-8090,
	-8074,		-8058,		-8042,		-8025,
	-8009,		-7993,		-7977,		-7961,
	-7945,		-7929,		-7913,		-7897,
	-7881,		-7865,		-7849,		-7833,
	-7817,		-7802,		-7786,		-7770,
	-7754,		-7739,		-7723,		-7707,
	-7692,		-7676,		-7661,		-7645,
	-7630,		-7614,		-7599,		-7583,
	-7568,		-7553,		-7537,		-7522,
	-7507,		-7492,		-7476,		-7461,
	-7446,		-7431,		-7416,		-7401,
	-7385,		-7370,		-7356,		-7340,
	-7325,		-7311,		-7296,		-7281,
	-7266,		-7251,		-7236,		-7221,
	-7207,		-7192,		-7177,		-7162,
	-7148,		-7133,		-7118,		-7104,
	-7089,		-7075,		-7060,		-7046,
	-7031,		-7016,		-7002,		-6988,
	-6973,		-6959,		-6944,		-6930,
	-6916,		-6901,		-6887,		-6873,
	-6859,		-6844,		-6830,		-6816,
	-6802,		-6788,		-6774,		-6760,
	-6746,		-6731,		-6717,		-6704,
	-6690,		-6675,		-6661,		-6647,
	-6633,		-6620,		-6606,		-6592,
	-6578,		-6564,		-6550,		-6537,
	-6523,		-6509,		-6495,		-6482,
	-6468,		-6454,		-6441,		-6427,
	-6413,		-6400,		-6386,		-6373,
	-6359,		-6346,		-6332,		-6318,
	-6305,		-6291,		-6278,		-6264,
	-6251,		-6238,		-6224,		-6211,
	-6198,		-6184,		-6171,		-6158,
	-6144,		-6131,		-6118,		-6105,
	-6091,		-6078,		-6065,		-6052,
	-6039,		-6025,		-6012,		-5999,
	-5986,		-5973,		-5960,		-5947,
	-5934,		-5921,		-5908,		-5895,
	-5882,		-5869,		-5856,		-5843,
	-5830,		-5817,		-5804,		-5791,
	-5779,		-5766,		-5753,		-5740,
	-5727,		-5714,		-5702,		-5689,
	-5676,		-5663,		-5650,		-5638,
	-5625,		-5612,		-5600,		-5587,
	-5575,		-5562,		-5549,		-5537,
	-5524,		-5512,		-5499,		-5486,
	-5474,		-5461,		-5449,		-5436,
	-5424,		-5411,		-5399,		-5386,
	-5374,		-5362,		-5349,		-5337,
	-5324,		-5312,		-5299,		-5287,
	-5275,		-5263,		-5250,		-5238,
	-5226,		-5213,		-5201,		-5189,
	-5177,		-5164,		-5152,		-5140,
	-5128,		-5115,		-5103,		-5091,
	-5079,		-5067,		-5055,		-5043,
	-5030,		-5018,		-5006,		-4994,
	-4982,		-4970,		-4958,		-4946,
	-4934,		-4922,		-4910,		-4898,
	-4886,		-4874,		-4862,		-4850,
	-4838,		-4826,		-4814,		-4803,
	-4791,		-4778,		-4767,		-4755,
	-4743,		-4731,		-4719,		-4708,
	-4696,		-4684,		-4672,		-4660,
	-4649,		-4637,		-4625,		-4613,
	-4601,		-4590,		-4578,		-4566,
	-4554,		-4543,		-4531,		-4520,
	-4508,		-4496,		-4484,		-4473,
	-4461,		-4449,		-4438,		-4427,
	-4415,		-4403,		-4392,		-4380,
	-4368,		-4357,		-4345,		-4334,
	-4322,		-4311,		-4299,		-4288,
	-4276,		-4265,		-4253,		-4242,
	-4230,		-4219,		-4207,		-4196,
	-4184,		-4173,		-4162,		-4150,
	-4139,		-4128,		-4116,		-4105,
	-4094,		-4082,		-4071,		-4060,
	-4048,		-4037,		-4026,		-4014,
	-4003,		-3992,		-3980,		-3969,
	-3958,		-3946,		-3935,		-3924,
	-3913,		-3901,		-3890,		-3879,
	-3868,		-3857,		-3845,		-3834,
	-3823,		-3812,		-3801,		-3790,
	-3779,		-3767,		-3756,		-3745,
	-3734,		-3723,		-3712,		-3700,
	-3689,		-3678,		-3667,		-3656,
	-3645,		-3634,		-3623,		-3612,
	-3601,		-3590,		-3579,		-3568,
	-3557,		-3545,		-3535,		-3524,
	-3513,		-3502,		-3491,		-3480,
	-3469,		-3458,		-3447,		-3436,
	-3425,		-3414,		-3403,		-3392,
	-3381,		-3370,		-3360,		-3348,
	-3337,		-3327,		-3316,		-3305,
	-3294,		-3283,		-3272,		-3262,
	-3251,		-3240,		-3229,		-3218,
	-3207,		-3197,		-3185,		-3175,
	-3164,		-3153,		-3142,		-3132,
	-3121,		-3110,		-3099,		-3088,
	-3078,		-3067,		-3056,		-3045,
	-3035,		-3024,		-3013,		-3003,
	-2992,		-2981,		-2970,		-2960,
	-2949,		-2938,		-2928,		-2917,
	-2906,		-2895,		-2885,		-2874,
	-2864,		-2853,		-2842,		-2832,
	-2821,		-2810,		-2800,		-2789,
	-2778,		-2768,		-2757,		-2747,
	-2736,		-2725,		-2715,		-2704,
	-2694,		-2683,		-2673,		-2662,
	-2651,		-2641,		-2630,		-2620,
	-2609,		-2599,		-2588,		-2578,
	-2567,		-2556,		-2546,		-2535,
	-2525,		-2515,		-2504,		-2493,
	-2483,		-2472,		-2462,		-2451,
	-2441,		-2431,		-2420,		-2410,
	-2399,		-2389,		-2378,		-2367,
	-2357,		-2347,		-2336,		-2326,
	-2315,		-2305,		-2295,		-2284,
	-2274,		-2263,		-2253,		-2243,
	-2232,		-2222,		-2211,		-2201,
	-2191,		-2180,		-2170,		-2159,
	-2149,		-2139,		-2128,		-2118,
	-2107,		-2097,		-2087,		-2076,
	-2066,		-2056,		-2046,		-2035,
	-2025,		-2014,		-2004,		-1994,
	-1983,		-1973,		-1963,		-1953,
	-1942,		-1932,		-1921,		-1911,
	-1901,		-1891,		-1880,		-1870,
	-1860,		-1849,		-1839,		-1829,
	-1819,		-1808,		-1798,		-1788,
	-1778,		-1767,		-1757,		-1747,
	-1736,		-1726,		-1716,		-1706,
	-1695,		-1685,		-1675,		-1665,
	-1654,		-1644,		-1634,		-1624,
	-1613,		-1603,		-1593,		-1583,
	-1573,		-1563,		-1552,		-1542,
	-1532,		-1522,		-1511,		-1501,
	-1491,		-1481,		-1471,		-1461,
	-1450,		-1440,		-1430,		-1420,
	-1409,		-1400,		-1389,		-1379,
	-1369,		-1359,		-1348,		-1339,
	-1328,		-1318,		-1308,		-1298,
	-1288,		-1278,		-1267,		-1257,
	-1247,		-1237,		-1227,		-1217,
	-1207,		-1196,		-1186,		-1176,
	-1166,		-1156,		-1146,		-1135,
	-1126,		-1115,		-1105,		-1095,
	-1085,		-1075,		-1065,		-1055,
	-1044,		-1034,		-1024,		-1014,
	-1004,		-994,		-984,		-974,
	-964,		-954,		-944,		-933,
	-923,		-913,		-903,		-893,
	-883,		-873,		-863,		-853,
	-843,		-833,		-822,		-812,
	-802,		-792,		-782,		-772,
	-762,		-752,		-742,		-732,
	-722,		-712,		-702,		-691,
	-682,		-671,		-662,		-651,
	-641,		-631,		-621,		-611,
	-601,		-591,		-581,		-571,
	-561,		-551,		-541,		-531,
	-521,		-511,		-501,		-491,
	-480,		-471,		-460,		-451,
	-440,		-430,		-420,		-410,
	-400,		-390,		-380,		-370,
	-360,		-350,		-340,		-330,
	-320,		-310,		-300,		-290,
	-280,		-270,		-260,		-250,
	-240,		-230,		-220,		-210,
	-199,		-190,		-179,		-170,
	-159,		-150,		-139,		-129,
	-119,		-109,		-99,		-89,
	-79,		-69,		-59,		-49,
	-39,		-29,		-19,		-9,
	1,		11,		21,		31,
	41,		51,		61,		71,
	81,		91,		101,		111,
	121,		131,		141,		152,
	161,		172,		181,		192,
	202,		212,		222,		232,
	242,		252,		262,		272,
	282,		292,		302,		312,
	322,		332,		342,		352,
	362,		372,		382,		392,
	402,		412,		422,		433,
	442,		453,		462,		473,
	483,		493,		503,		513,
	523,		533,		543,		553,
	563,		573,		583,		593,
	603,		613,		623,		633,
	643,		653,		664,		673,
	684,		694,		704,		714,
	724,		734,		744,		754,
	764,		774,		784,		794,
	804,		815,		825,		835,
	845,		855,		865,		875,
	885,		895,		905,		915,
	925,		936,		946,		956,
	966,		976,		986,		996,
	1006,		1016,		1026,		1037,
	1047,		1057,		1067,		1077,
	1087,		1097,		1107,		1117,
	1128,		1138,		1148,		1158,
	1168,		1178,		1188,		1198,
	1209,		1219,		1229,		1239,
	1249,		1259,		1269,		1280,
	1290,		1300,		1310,		1320,
	1330,		1341,		1351,		1361,
	1371,		1381,		1391,		1402,
	1412,		1422,		1432,		1442,
	1452,		1463,		1473,		1483,
	1493,		1503,		1513,		1524,
	1534,		1544,		1554,		1565,
	1575,		1585,		1595,		1606,
	1616,		1626,		1636,		1647,
	1656,		1667,		1677,		1687,
	1697,		1708,		1718,		1729,
	1739,		1749,		1759,		1769,
	1780,		1790,		1800,		1810,
	1821,		1831,		1841,		1851,
	1862,		1872,		1883,		1893,
	1903,		1913,		1923,		1934,
	1944,		1955,		1965,		1975,
	1985,		1996,		2006,		2016,
	2027,		2037,		2048,		2058,
	2068,		2079,		2089,		2099,
	2110,		2120,		2130,		2141,
	2151,		2161,		2172,		2182,
	2193,		2203,		2213,		2224,
	2234,		2245,		2255,		2265,
	2276,		2286,		2297,		2307,
	2318,		2328,		2338,		2349,
	2359,		2370,		2380,		2391,
	2401,		2412,		2422,		2433,
	2443,		2454,		2464,		2475,
	2485,		2496,		2506,		2517,
	2527,		2537,		2548,		2559,
	2569,		2580,		2590,		2601,
	2612,		2622,		2632,		2643,
	2654,		2664,		2675,		2685,
	2696,		2707,		2717,		2728,
	2738,		2749,		2759,		2770,
	2781,		2791,		2802,		2813,
	2823,		2834,		2845,		2855,
	2866,		2877,		2887,		2898,
	2909,		2919,		2930,		2941,
	2951,		2962,		2973,		2984,
	2994,		3005,		3015,		3027,
	3037,		3048,		3058,		3069,
	3080,		3091,		3101,		3113,
	3123,		3134,		3145,		3156,
	3166,		3177,		3188,		3199,
	3210,		3220,		3231,		3242,
	3253,		3264,		3275,		3285,
	3296,		3307,		3318,		3329,
	3340,		3351,		3362,		3373,
	3384,		3394,		3405,		3416,
	3427,		3438,		3449,		3460,
	3471,		3482,		3493,		3504,
	3515,		3526,		3537,		3548,
	3559,		3570,		3581,		3592,
	3603,		3614,		3625,		3636,
	3647,		3659,		3670,		3681,
	3692,		3703,		3714,		3725,
	3736,		3747,		3758,		3770,
	3781,		3792,		3803,		3814,
	3825,		3837,		3848,		3859,
	3870,		3881,		3893,		3904,
	3915,		3926,		3937,		3949,
	3960,		3971,		3983,		3994,
	4005,		4017,		4028,		4039,
	4051,		4062,		4073,		4085,
	4096,		4107,		4119,		4130,
	4141,		4153,		4164,		4175,
	4187,		4198,		4210,		4221,
	4233,		4244,		4256,		4267,
	4279,		4290,		4302,		4313,
	4325,		4336,		4348,		4359,
	4371,		4382,		4394,		4406,
	4417,		4429,		4440,		4452,
	4464,		4475,		4487,		4499,
	4510,		4522,		4533,		4545,
	4557,		4569,		4581,		4592,
	4604,		4616,		4627,		4639,
	4651,		4663,		4674,		4686,
	4698,		4710,		4722,		4734,
	4746,		4758,		4769,		4781,
	4793,		4805,		4817,		4829,
	4841,		4853,		4865,		4877,
	4889,		4900,		4913,		4925,
	4936,		4949,		4961,		4973,
	4985,		4997,		5009,		5021,
	5033,		5045,		5057,		5070,
	5081,		5094,		5106,		5118,
	5130,		5143,		5155,		5167,
	5179,		5191,		5204,		5216,
	5228,		5240,		5253,		5265,
	5278,		5290,		5302,		5315,
	5327,		5340,		5352,		5364,
	5377,		5389,		5401,		5414,
	5426,		5439,		5451,		5464,
	5476,		5489,		5502,		5514,
	5527,		5539,		5552,		5564,
	5577,		5590,		5603,		5615,
	5628,		5641,		5653,		5666,
	5679,		5691,		5704,		5717,
	5730,		5743,		5756,		5768,
	5781,		5794,		5807,		5820,
	5833,		5846,		5859,		5872,
	5885,		5897,		5911,		5924,
	5937,		5950,		5963,		5976,
	5989,		6002,		6015,		6028,
	6042,		6055,		6068,		6081,
	6094,		6108,		6121,		6134,
	6147,		6160,		6174,		6187,
	6201,		6214,		6227,		6241,
	6254,		6267,		6281,		6294,
	6308,		6321,		6335,		6348,
	6362,		6375,		6389,		6403,
	6416,		6430,		6443,		6457,
	6471,		6485,		6498,		6512,
	6526,		6540,		6554,		6567,
	6581,		6595,		6609,		6623,
	6637,		6651,		6665,		6679,
	6692,		6706,		6721,		6735,
	6749,		6763,		6777,		6791,
	6805,		6819,		6833,		6848,
	6862,		6876,		6890,		6905,
	6919,		6933,		6948,		6962,
	6976,		6991,		7005,		7020,
	7034,		7049,		7064,		7078,
	7093,		7107,		7122,		7136,
	7151,		7166,		7180,		7195,
	7210,		7225,		7240,		7254,
	7269,		7284,		7299,		7314,
	7329,		7344,		7359,		7374,
	7389,		7404,		7419,		7434,
	7449,		7465,		7480,		7495,
	7510,		7526,		7541,		7556,
	7571,		7587,		7602,		7618,
	7633,		7648,		7664,		7680,
	7695,		7711,		7726,		7742,
	7758,		7773,		7789,		7805,
	7821,		7836,		7852,		7868,
	7884,		7900,		7916,		7932,
	7948,		7964,		7981,		7997,
	8013,		8029,		8045,		8061,
	8078,		8094,		8110,		8127,
	8143,		8160,		8176,		8193,
	8209,		8226,		8242,		8259,
	8276,		8292,		8309,		8326,
	8343,		8360,		8377,		8394,
	8410,		8428,		8444,		8462,
	8479,		8496,		8513,		8530,
	8548,		8565,		8582,		8600,
	8617,		8634,		8652,		8670,
	8687,		8704,		8722,		8740,
	8758,		8775,		8793,		8811,
	8829,		8847,		8865,		8883,
	8901,		8919,		8937,		8955,
	8974,		8992,		9010,		9029,
	9047,		9066,		9084,		9103,
	9121,		9140,		9159,		9177,
	9196,		9215,		9234,		9253,
	9272,		9291,		9310,		9329,
	9349,		9368,		9387,		9406,
	9426,		9445,		9465,		9484,
	9504,		9524,		9544,		9563,
	9583,		9603,		9623,		9643,
	9663,		9683,		9703,		9723,
	9744,		9764,		9785,		9805,
	9826,		9846,		9867,		9888,
	9909,		9930,		9950,		9971,
	9993,		10013,		10035,		10056,
	10077,		10099,		10120,		10142,
	10163,		10185,		10207,		10229,
	10251,		10273,		10294,		10317,
	10339,		10361,		10384,		10406,
	10428,		10451,		10474,		10496,
	10519,		10542,		10565,		10588,
	10612,		10635,		10658,		10682,
	10705,		10729,		10752,		10776,
	10800,		10824,		10848,		10872,
	10896,		10921,		10945,		10969,
	10994,		11019,		11044,		11069,
	11094,		11119,		11144,		11169,
	11195,		11221,		11246,		11272,
	11298,		11324,		11350,		11376,
	11402,		11429,		11456,		11482,
	11509,		11536,		11563,		11590,
	11618,		11645,		11673,		11701,
	11728,		11756,		11785,		11813,
	11842,		11870,		11899,		11928,
	11957,		11986,		12015,		12045,
	12074,		12104,		12134,		12164,
	12194,		12225,		12255,		12286,
	12317,		12348,		12380,		12411,
	12443,		12475,		12507,		12539,
	12571,		12604,		12637,		12670,
	12703,		12737,		12771,		12804,
	12839,		12873,		12907,		12942,
	12977,		13013,		13048,		13084,
	13120,		13156,		13192,		13229,
	13267,		13304,		13341,		13379,
	13418,		13456,		13495,		13534,
	13573,		13613,		13653,		13693,
	13734,		13775,		13817,		13858,
	13901,		13943,		13986,		14029,
	14073,		14117,		14162,		14206,
	14252,		14297,		14343,		14390,
	14437,		14485,		14533,		14582,
	14631,		14680,		14731,		14782,
	14833,		14885,		14937,		14991,
	15044,		15099,		15154,		15210,
	15266,		15324,		15382,		15441,
	15500,		15561,		15622,		15684,
	15747,		15811,		15877,		15943,
	16010,		16078,		16148,		16218,
	16290,		16363,		16437,		16513,
	16590,		16669,		16749,		16831,
	16915,		17000,		17088,		17177,
	17268,		17362,		17458,		17556,
	17657,		17761,		17868,		17977,
	18090,		18207,		18328,		18452,
	18581,		18715,		18854,		18998,
	19149,		19307,		19472,		19645,
	19828,		20021,		20226,		20444,
	20678,		20930,		21204,		21503,
	21835,		22206,		22630,		23124,
	23721,		24478,		25529,		27316,
};

/* tabledist - return a pseudo-randomly distributed value with mean mu and
 * std deviation sigma.  Uses table lookup to approximate the desired
 * distribution, and a uniformly-distributed pseudo-random source.
 */
static inline int tabledist(int mu, int sigma)
{
	int x;
	int index;
	int sigmamod, sigmadiv;

	if (sigma == 0)
		return mu;

	index = (net_random() & (TABLESIZE-1));
	sigmamod = sigma%TABLEFACTOR;
	sigmadiv = sigma/TABLEFACTOR;
	x = sigmamod*disttable[index];

	if (x >= 0)
		x += TABLEFACTOR/2;
	else
		x -= TABLEFACTOR/2;

	x /= TABLEFACTOR;
	x += sigmadiv*disttable[index];
	x += mu;
	return x;
}

/* Enqueue packets with underlying discipline (fifo)
 * but mark them with current time first.
 */
static int netem_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	struct netem_skb_cb *cb = (struct netem_skb_cb *)skb->cb;
	psched_time_t now;
	long delay;

	pr_debug("netem_enqueue skb=%p @%lu\n", skb, jiffies);

	/* Random packet drop 0 => none, ~0 => all */
	if (q->loss && q->loss >= net_random()) {
		sch->stats.drops++;
		return 0;	/* lie about loss so TCP doesn't know */
	}


	/* If doing simple delay then gap == 0 so all packets
	 * go into the delayed holding queue
	 * otherwise if doing out of order only "1 out of gap"
	 * packets will be delayed.
	 */
	if (q->counter < q->gap) {
		int ret;

		++q->counter;
		ret = q->qdisc->enqueue(skb, q->qdisc);
		if (ret)
			sch->stats.drops++;
		return ret;
	}
	
	q->counter = 0;
	
	PSCHED_GET_TIME(now);
	if (q->jitter) 
		delay = tabledist(q->latency, q->jitter);
	else
		delay = q->latency;

	PSCHED_TADD2(now, delay, cb->time_to_send);
	
	/* Always queue at tail to keep packets in order */
	if (likely(q->delayed.qlen < q->limit)) {
		__skb_queue_tail(&q->delayed, skb);
		sch->q.qlen++;
		sch->stats.bytes += skb->len;
		sch->stats.packets++;
		return 0;
	}

	sch->stats.drops++;
	kfree_skb(skb);
	return NET_XMIT_DROP;
}

/* Requeue packets but don't change time stamp */
static int netem_requeue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	int ret;

	if ((ret = q->qdisc->ops->requeue(skb, q->qdisc)) == 0)
		sch->q.qlen++;

	return ret;
}

static unsigned int netem_drop(struct Qdisc* sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	unsigned int len;

	if ((len = q->qdisc->ops->drop(q->qdisc)) != 0) {
		sch->q.qlen--;
		sch->stats.drops++;
	}
	return len;
}

/* Dequeue packet.
 *  Move all packets that are ready to send from the delay holding
 *  list to the underlying qdisc, then just call dequeue
 */
static struct sk_buff *netem_dequeue(struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;
	psched_time_t now;

	PSCHED_GET_TIME(now);
	while ((skb = skb_peek(&q->delayed)) != NULL) {
		const struct netem_skb_cb *cb
			= (const struct netem_skb_cb *)skb->cb;
		long delay 
			= PSCHED_US2JIFFIE(PSCHED_TDIFF(cb->time_to_send, now));
		pr_debug("netem_dequeue: delay queue %p@%lu %ld\n",
			 skb, jiffies, delay);

		/* if more time remaining? */
		if (delay > 0) {
			mod_timer(&q->timer, jiffies + delay);
			break;
		}
		__skb_unlink(skb, &q->delayed);

		if (q->qdisc->enqueue(skb, q->qdisc))
			sch->stats.drops++;
	}

	skb = q->qdisc->dequeue(q->qdisc);
	if (skb) 
		sch->q.qlen--;
	return skb;
}

static void netem_watchdog(unsigned long arg)
{
	struct Qdisc *sch = (struct Qdisc *)arg;

	pr_debug("netem_watchdog: fired @%lu\n", jiffies);
	netif_schedule(sch->dev);
}

static void netem_reset(struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	qdisc_reset(q->qdisc);
	skb_queue_purge(&q->delayed);

	sch->q.qlen = 0;
	del_timer_sync(&q->timer);
}

static int set_fifo_limit(struct Qdisc *q, int limit)
{
        struct rtattr *rta;
	int ret = -ENOMEM;

	rta = kmalloc(RTA_LENGTH(sizeof(struct tc_fifo_qopt)), GFP_KERNEL);
	if (rta) {
		rta->rta_type = RTM_NEWQDISC;
		rta->rta_len = RTA_LENGTH(sizeof(struct tc_fifo_qopt)); 
		((struct tc_fifo_qopt *)RTA_DATA(rta))->limit = limit;
		
		ret = q->ops->change(q, rta);
		kfree(rta);
	}
	return ret;
}

static int netem_change(struct Qdisc *sch, struct rtattr *opt)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	struct tc_netem_qopt *qopt = RTA_DATA(opt);
	struct Qdisc *child;
	int ret;

	if (opt->rta_len < RTA_LENGTH(sizeof(*qopt)))
		return -EINVAL;

	child = qdisc_create_dflt(sch->dev, &pfifo_qdisc_ops);
	if (!child)
		return -EINVAL;

	ret = set_fifo_limit(child, qopt->limit);
	if (ret) {
		qdisc_destroy(child);
		return ret;
	}

	sch_tree_lock(sch);
	if (child) {
		child = xchg(&q->qdisc, child);
		if (child != &noop_qdisc)
			qdisc_destroy(child);
	
		q->latency = qopt->latency;
		q->jitter = qopt->jitter;
		q->limit = qopt->limit;
		q->gap = qopt->gap;
		q->loss = qopt->loss;
	}
	sch_tree_unlock(sch);

	return 0;
}

static int netem_init(struct Qdisc *sch, struct rtattr *opt)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	if (!opt)
		return -EINVAL;

	skb_queue_head_init(&q->delayed);
	q->qdisc = &noop_qdisc;

	init_timer(&q->timer);
	q->timer.function = netem_watchdog;
	q->timer.data = (unsigned long) sch;
	q->counter = 0;

	return netem_change(sch, opt);
}

static void netem_destroy(struct Qdisc *sch)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	del_timer_sync(&q->timer);
	qdisc_destroy(q->qdisc);
}

static int netem_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	unsigned char	 *b = skb->tail;
	struct tc_netem_qopt qopt;

	qopt.latency = q->latency;
	qopt.jitter = q->jitter;
	qopt.limit = q->limit;
	qopt.loss = q->loss;
	qopt.gap = q->gap;

	RTA_PUT(skb, TCA_OPTIONS, sizeof(qopt), &qopt);

	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int netem_dump_class(struct Qdisc *sch, unsigned long cl,
			  struct sk_buff *skb, struct tcmsg *tcm)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	if (cl != 1) 	/* only one class */
		return -ENOENT;

	tcm->tcm_handle |= TC_H_MIN(1);
	tcm->tcm_info = q->qdisc->handle;

	return 0;
}

static int netem_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
		     struct Qdisc **old)
{
	struct netem_sched_data *q = qdisc_priv(sch);

	if (new == NULL)
		new = &noop_qdisc;

	sch_tree_lock(sch);
	*old = xchg(&q->qdisc, new);
	qdisc_reset(*old);
	sch->q.qlen = 0;
	sch_tree_unlock(sch);

	return 0;
}

static struct Qdisc *netem_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct netem_sched_data *q = qdisc_priv(sch);
	return q->qdisc;
}

static unsigned long netem_get(struct Qdisc *sch, u32 classid)
{
	return 1;
}

static void netem_put(struct Qdisc *sch, unsigned long arg)
{
}

static int netem_change_class(struct Qdisc *sch, u32 classid, u32 parentid, 
			    struct rtattr **tca, unsigned long *arg)
{
	return -ENOSYS;
}

static int netem_delete(struct Qdisc *sch, unsigned long arg)
{
	return -ENOSYS;
}

static void netem_walk(struct Qdisc *sch, struct qdisc_walker *walker)
{
	if (!walker->stop) {
		if (walker->count >= walker->skip)
			if (walker->fn(sch, 1, walker) < 0) {
				walker->stop = 1;
				return;
			}
		walker->count++;
	}
}

static struct tcf_proto **netem_find_tcf(struct Qdisc *sch, unsigned long cl)
{
	return NULL;
}

static struct Qdisc_class_ops netem_class_ops = {
	.graft		=	netem_graft,
	.leaf		=	netem_leaf,
	.get		=	netem_get,
	.put		=	netem_put,
	.change		=	netem_change_class,
	.delete		=	netem_delete,
	.walk		=	netem_walk,
	.tcf_chain	=	netem_find_tcf,
	.dump		=	netem_dump_class,
};

static struct Qdisc_ops netem_qdisc_ops = {
	.id		=	"netem",
	.cl_ops		=	&netem_class_ops,
	.priv_size	=	sizeof(struct netem_sched_data),
	.enqueue	=	netem_enqueue,
	.dequeue	=	netem_dequeue,
	.requeue	=	netem_requeue,
	.drop		=	netem_drop,
	.init		=	netem_init,
	.reset		=	netem_reset,
	.destroy	=	netem_destroy,
	.change		=	netem_change,
	.dump		=	netem_dump,
	.owner		=	THIS_MODULE,
};


static int __init netem_module_init(void)
{
	return register_qdisc(&netem_qdisc_ops);
}
static void __exit netem_module_exit(void)
{
	unregister_qdisc(&netem_qdisc_ops);
}
module_init(netem_module_init)
module_exit(netem_module_exit)
MODULE_LICENSE("GPL");
