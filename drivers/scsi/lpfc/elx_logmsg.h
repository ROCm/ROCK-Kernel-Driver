/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

#ifndef _H_ELX_LOGMSG
#define _H_ELX_LOGMSG

/*
 * Log Message Structure
 *
 * The following structure supports LOG messages only.
 * Every LOG message is associated to a msgBlkLogDef structure of the 
 * following type.
 */

typedef struct msgLogType {
	int msgNum;		/* Message number */
	char *msgStr;		/* Ptr to log message */
	char *msgPreambleStr;	/* Ptr to log message preamble */
	int msgOutput;		/* Message output target - bitmap */
	/*
	 * This member controls message OUTPUT.
	 *
	 * The phase 'global controls' refers to user configurable parameters
	 * such as LOG_VERBOSE that control message output on a global basis.
	 */

#define ELX_MSG_OPUT_GLOB_CTRL         0x0	/* Use global control */
#define ELX_MSG_OPUT_DISA              0x1	/* Override global control */
#define ELX_MSG_OPUT_FORCE             0x2	/* Override global control */
	int msgType;		/* Message LOG type - bitmap */
#define ELX_LOG_MSG_TYPE_INFO          0x1	/* Maskable */
#define ELX_LOG_MSG_TYPE_WARN          0x2	/* Non-Maskable */
#define ELX_LOG_MSG_TYPE_ERR_CFG       0x4	/* Non-Maskable */
#define ELX_LOG_MSG_TYPE_ERR           0x8	/* Non-Maskable */
#define ELX_LOG_MSG_TYPE_PANIC        0x10	/* Non-Maskable */
	int msgMask;		/* Message LOG mask - bitmap */
	/*
	 * NOTE: Only LOG messages of types MSG_TYPE_WARN & MSG_TYPE_INFO are 
	 * maskable at the GLOBAL level.
	 * 
	 * Any LOG message regardless of message type can be disabled (override verbose) 
	 * at the msgBlkLogDef struct level my setting member msgOutput = ELX_MSG_OPUT_DISA.
	 * The message will never be displayed regardless of verbose mask.
	 * 
	 * Any LOG message regardless of message type can be enable (override verbose) 
	 * at the msgBlkLogDef struct level my setting member msgOutput = ELX_MSG_OPUT_FORCE.
	 * The message will always be displayed regardless of verbose mask.
	 */
#define LOG_ELS                       0x1	/* ELS events */
#define LOG_DISCOVERY                 0x2	/* Link discovery events */
#define LOG_MBOX                      0x4	/* Mailbox events */
#define LOG_INIT                      0x8	/* Initialization events */
#define LOG_LINK_EVENT                0x10	/* Link events */
#define LOG_IP                        0x20	/* IP traffic history */
#define LOG_FCP                       0x40	/* FCP traffic history */
#define LOG_NODE                      0x80	/* Node table events */
#define LOG_MISC                      0x400	/* Miscellaneous events */
#define LOG_SLI                       0x800	/* SLI events */
#define LOG_CHK_COND                  0x1000	/* FCP Check condition flag */
#define LOG_IOC                       0x2000	/* IOCtl events */
#define LOG_ALL_MSG                   0xffff	/* LOG all messages */

	unsigned int msgAuxLogID;	/* Message LOG ID - This auxilliary member describes the failure. */
#define ERRID_LOG_TIMEOUT             0xfdefefa7	/* Fibre Channel timeout */
#define ERRID_LOG_HDW_ERR             0x1ae4fffc	/* Fibre Channel hardware failure */
#define ERRID_LOG_UNEXPECT_EVENT      0xbdb7e728	/* Fibre Channel unexpected event */
#define ERRID_LOG_INIT                0xbe1043b8	/* Fibre Channel init failure */
#define ERRID_LOG_NO_RESOURCE         0x474c1775	/* Fibre Channel no resources */
} msgLogDef;

/*
 * External Declarations for LOG Messages
 */

/* ELS LOG Messages */
extern char elx_mes0100[];
extern char elx_mes0101[];
extern char elx_mes0102[];
extern char elx_mes0103[];
extern char elx_mes0104[];
extern char elx_mes0105[];
extern char elx_mes0106[];
extern char elx_mes0107[];
extern char elx_mes0108[];
extern char elx_mes0109[];
extern char elx_mes0110[];
extern char elx_mes0111[];
extern char elx_mes0112[];
extern char elx_mes0113[];
extern char elx_mes0114[];
extern char elx_mes0115[];
extern char elx_mes0116[];
extern char elx_mes0117[];
extern char elx_mes0118[];
extern char elx_mes0119[];
extern char elx_mes0120[];
extern char elx_mes0121[];
extern char elx_mes0122[];
extern char elx_mes0123[];
extern char elx_mes0124[];
extern char elx_mes0125[];
extern char elx_mes0126[];
extern char elx_mes0127[];

/* DISCOVERY LOG Messages */
extern char elx_mes0200[];
extern char elx_mes0201[];
extern char elx_mes0202[];
extern char elx_mes0203[];
extern char elx_mes0204[];
extern char elx_mes0205[];
extern char elx_mes0206[];
extern char elx_mes0207[];
extern char elx_mes0208[];
extern char elx_mes0209[];
extern char elx_mes0210[];
extern char elx_mes0211[];
extern char elx_mes0212[];
extern char elx_mes0213[];
extern char elx_mes0214[];
extern char elx_mes0215[];
extern char elx_mes0216[];
extern char elx_mes0217[];
extern char elx_mes0218[];
extern char elx_mes0219[];
extern char elx_mes0220[];
extern char elx_mes0221[];
extern char elx_mes0222[];
extern char elx_mes0223[];
extern char elx_mes0224[];
extern char elx_mes0225[];
extern char elx_mes0226[];
extern char elx_mes0227[];
extern char elx_mes0228[];
extern char elx_mes0229[];
extern char elx_mes0230[];
extern char elx_mes0231[];
extern char elx_mes0232[];
extern char elx_mes0234[];
extern char elx_mes0235[];
extern char elx_mes0236[];
extern char elx_mes0237[];
extern char elx_mes0238[];
extern char elx_mes0239[];
extern char elx_mes0240[];
extern char elx_mes0241[];
extern char elx_mes0243[];
extern char elx_mes0244[];
extern char elx_mes0245[];
extern char elx_mes0246[];
extern char elx_mes0247[];
extern char elx_mes0248[];

/* MAILBOX LOG Messages */
extern char elx_mes0300[];
extern char elx_mes0301[];
extern char elx_mes0302[];
extern char elx_mes0304[];
extern char elx_mes0305[];
extern char elx_mes0306[];
extern char elx_mes0307[];
extern char elx_mes0308[];
extern char elx_mes0309[];
extern char elx_mes0310[];
extern char elx_mes0311[];
extern char elx_mes0312[];
extern char elx_mes0313[];
extern char elx_mes0314[];
extern char elx_mes0315[];
extern char elx_mes0316[];
extern char elx_mes0317[];
extern char elx_mes0318[];
extern char elx_mes0319[];
extern char elx_mes0320[];
extern char elx_mes0321[];
extern char elx_mes0322[];
extern char elx_mes0323[];
extern char elx_mes0324[];

/* INIT LOG Messages */
extern char elx_mes0405[];
extern char elx_mes0406[];
extern char elx_mes0407[];
extern char elx_mes0409[];
extern char elx_mes0410[];
extern char elx_mes0411[];
extern char elx_mes0412[];
extern char elx_mes0413[];
extern char elx_mes0430[];
extern char elx_mes0431[];
extern char elx_mes0432[];
extern char elx_mes0433[];
extern char elx_mes0434[];
extern char elx_mes0435[];
extern char elx_mes0436[];
extern char elx_mes0437[];
extern char elx_mes0438[];
extern char elx_mes0439[];
extern char elx_mes0440[];
extern char elx_mes0441[];
extern char elx_mes0442[];
extern char elx_mes0446[];
extern char elx_mes0447[];
extern char elx_mes0448[];
extern char elx_mes0449[];
extern char elx_mes0450[];
extern char elx_mes0451[];
extern char elx_mes0453[];
extern char elx_mes0454[];
extern char elx_mes0455[];
extern char elx_mes0457[];
extern char elx_mes0458[];
extern char elx_mes0460[];
extern char elx_mes0462[];

/* IP LOG Messages */
extern char elx_mes0600[];
extern char elx_mes0601[];
extern char elx_mes0602[];
extern char elx_mes0603[];
extern char elx_mes0604[];
extern char elx_mes0605[];
extern char elx_mes0606[];
extern char elx_mes0607[];
extern char elx_mes0608[];
extern char elx_mes0609[];
extern char elx_mes0610[];

/* FCP LOG Messages */
extern char elx_mes0700[];
extern char elx_mes0701[];
extern char elx_mes0702[];
extern char elx_mes0703[];
extern char elx_mes0706[];
extern char elx_mes0710[];
extern char elx_mes0712[];
extern char elx_mes0713[];
extern char elx_mes0714[];
extern char elx_mes0716[];
extern char elx_mes0717[];
extern char elx_mes0729[];
extern char elx_mes0730[];
extern char elx_mes0732[];
extern char elx_mes0734[];
extern char elx_mes0735[];
extern char elx_mes0736[];
extern char elx_mes0737[];
extern char elx_mes0747[];
extern char elx_mes0748[];
extern char elx_mes0749[];
extern char elx_mes0754[];

/* NODE LOG Messages */
extern char elx_mes0900[];
extern char elx_mes0901[];
extern char elx_mes0902[];
extern char elx_mes0903[];
extern char elx_mes0904[];
extern char elx_mes0905[];
extern char elx_mes0906[];
extern char elx_mes0907[];
extern char elx_mes0908[];
extern char elx_mes0910[];
extern char elx_mes0911[];
extern char elx_mes0927[];
extern char elx_mes0928[];
extern char elx_mes0929[];
extern char elx_mes0930[];
extern char elx_mes0931[];
extern char elx_mes0932[];

/* MISC LOG messages */
extern char elx_mes1201[];
extern char elx_mes1202[];
extern char elx_mes1204[];
extern char elx_mes1205[];
extern char elx_mes1206[];
extern char elx_mes1207[];
extern char elx_mes1208[];
extern char elx_mes1210[];
extern char elx_mes1211[];
extern char elx_mes1212[];
extern char elx_mes1213[];

/* LINK LOG Messages */
extern char elx_mes1300[];
extern char elx_mes1301[];
extern char elx_mes1302[];
extern char elx_mes1303[];
extern char elx_mes1304[];
extern char elx_mes1305[];
extern char elx_mes1306[];
extern char elx_mes1307[];

/* CHK CONDITION LOG Messages */

/* IOCtl Log Messages */
extern char elx_mes1600[];
extern char elx_mes1601[];
extern char elx_mes1602[];
extern char elx_mes1603[];
extern char elx_mes1604[];
extern char elx_mes1605[];

/*
 * External Declarations for LOG Message Structure msgBlkLogDef
 */

/* ELS LOG Message Structures */
extern msgLogDef elx_msgBlk0100;
extern msgLogDef elx_msgBlk0101;
extern msgLogDef elx_msgBlk0102;
extern msgLogDef elx_msgBlk0103;
extern msgLogDef elx_msgBlk0104;
extern msgLogDef elx_msgBlk0105;
extern msgLogDef elx_msgBlk0106;
extern msgLogDef elx_msgBlk0107;
extern msgLogDef elx_msgBlk0108;
extern msgLogDef elx_msgBlk0109;
extern msgLogDef elx_msgBlk0110;
extern msgLogDef elx_msgBlk0111;
extern msgLogDef elx_msgBlk0112;
extern msgLogDef elx_msgBlk0113;
extern msgLogDef elx_msgBlk0114;
extern msgLogDef elx_msgBlk0115;
extern msgLogDef elx_msgBlk0116;
extern msgLogDef elx_msgBlk0117;
extern msgLogDef elx_msgBlk0118;
extern msgLogDef elx_msgBlk0119;
extern msgLogDef elx_msgBlk0120;
extern msgLogDef elx_msgBlk0121;
extern msgLogDef elx_msgBlk0122;
extern msgLogDef elx_msgBlk0123;
extern msgLogDef elx_msgBlk0124;
extern msgLogDef elx_msgBlk0125;
extern msgLogDef elx_msgBlk0126;
extern msgLogDef elx_msgBlk0127;

/* DISCOVERY LOG Message Structures */
extern msgLogDef elx_msgBlk0200;
extern msgLogDef elx_msgBlk0201;
extern msgLogDef elx_msgBlk0202;
extern msgLogDef elx_msgBlk0203;
extern msgLogDef elx_msgBlk0204;
extern msgLogDef elx_msgBlk0205;
extern msgLogDef elx_msgBlk0206;
extern msgLogDef elx_msgBlk0207;
extern msgLogDef elx_msgBlk0208;
extern msgLogDef elx_msgBlk0209;
extern msgLogDef elx_msgBlk0210;
extern msgLogDef elx_msgBlk0211;
extern msgLogDef elx_msgBlk0212;
extern msgLogDef elx_msgBlk0213;
extern msgLogDef elx_msgBlk0214;
extern msgLogDef elx_msgBlk0215;
extern msgLogDef elx_msgBlk0216;
extern msgLogDef elx_msgBlk0217;
extern msgLogDef elx_msgBlk0218;
extern msgLogDef elx_msgBlk0219;
extern msgLogDef elx_msgBlk0220;
extern msgLogDef elx_msgBlk0221;
extern msgLogDef elx_msgBlk0222;
extern msgLogDef elx_msgBlk0223;
extern msgLogDef elx_msgBlk0224;
extern msgLogDef elx_msgBlk0225;
extern msgLogDef elx_msgBlk0226;
extern msgLogDef elx_msgBlk0227;
extern msgLogDef elx_msgBlk0228;
extern msgLogDef elx_msgBlk0229;
extern msgLogDef elx_msgBlk0230;
extern msgLogDef elx_msgBlk0231;
extern msgLogDef elx_msgBlk0232;
extern msgLogDef elx_msgBlk0234;
extern msgLogDef elx_msgBlk0235;
extern msgLogDef elx_msgBlk0236;
extern msgLogDef elx_msgBlk0237;
extern msgLogDef elx_msgBlk0238;
extern msgLogDef elx_msgBlk0239;
extern msgLogDef elx_msgBlk0240;
extern msgLogDef elx_msgBlk0241;
extern msgLogDef elx_msgBlk0243;
extern msgLogDef elx_msgBlk0244;
extern msgLogDef elx_msgBlk0245;
extern msgLogDef elx_msgBlk0246;
extern msgLogDef elx_msgBlk0247;
extern msgLogDef elx_msgBlk0248;

/* MAILBOX LOG Message Structures */
extern msgLogDef elx_msgBlk0300;
extern msgLogDef elx_msgBlk0301;
extern msgLogDef elx_msgBlk0302;
extern msgLogDef elx_msgBlk0304;
extern msgLogDef elx_msgBlk0305;
extern msgLogDef elx_msgBlk0306;
extern msgLogDef elx_msgBlk0307;
extern msgLogDef elx_msgBlk0308;
extern msgLogDef elx_msgBlk0309;
extern msgLogDef elx_msgBlk0310;
extern msgLogDef elx_msgBlk0311;
extern msgLogDef elx_msgBlk0312;
extern msgLogDef elx_msgBlk0313;
extern msgLogDef elx_msgBlk0314;
extern msgLogDef elx_msgBlk0315;
extern msgLogDef elx_msgBlk0316;
extern msgLogDef elx_msgBlk0317;
extern msgLogDef elx_msgBlk0318;
extern msgLogDef elx_msgBlk0319;
extern msgLogDef elx_msgBlk0320;
extern msgLogDef elx_msgBlk0321;
extern msgLogDef elx_msgBlk0322;
extern msgLogDef elx_msgBlk0323;
extern msgLogDef elx_msgBlk0324;

/* INIT LOG Message Structures */
extern msgLogDef elx_msgBlk0405;
extern msgLogDef elx_msgBlk0406;
extern msgLogDef elx_msgBlk0407;
extern msgLogDef elx_msgBlk0409;
extern msgLogDef elx_msgBlk0410;
extern msgLogDef elx_msgBlk0411;
extern msgLogDef elx_msgBlk0412;
extern msgLogDef elx_msgBlk0413;
extern msgLogDef elx_msgBlk0430;
extern msgLogDef elx_msgBlk0431;
extern msgLogDef elx_msgBlk0432;
extern msgLogDef elx_msgBlk0433;
extern msgLogDef elx_msgBlk0434;
extern msgLogDef elx_msgBlk0435;
extern msgLogDef elx_msgBlk0436;
extern msgLogDef elx_msgBlk0437;
extern msgLogDef elx_msgBlk0438;
extern msgLogDef elx_msgBlk0439;
extern msgLogDef elx_msgBlk0440;
extern msgLogDef elx_msgBlk0441;
extern msgLogDef elx_msgBlk0442;
extern msgLogDef elx_msgBlk0446;
extern msgLogDef elx_msgBlk0447;
extern msgLogDef elx_msgBlk0448;
extern msgLogDef elx_msgBlk0449;
extern msgLogDef elx_msgBlk0450;
extern msgLogDef elx_msgBlk0451;
extern msgLogDef elx_msgBlk0453;
extern msgLogDef elx_msgBlk0454;
extern msgLogDef elx_msgBlk0455;
extern msgLogDef elx_msgBlk0457;
extern msgLogDef elx_msgBlk0458;
extern msgLogDef elx_msgBlk0460;
extern msgLogDef elx_msgBlk0462;

/* IP LOG Message Structures */
extern msgLogDef elx_msgBlk0600;
extern msgLogDef elx_msgBlk0601;
extern msgLogDef elx_msgBlk0602;
extern msgLogDef elx_msgBlk0603;
extern msgLogDef elx_msgBlk0604;
extern msgLogDef elx_msgBlk0605;
extern msgLogDef elx_msgBlk0606;
extern msgLogDef elx_msgBlk0607;
extern msgLogDef elx_msgBlk0608;
extern msgLogDef elx_msgBlk0609;
extern msgLogDef elx_msgBlk0610;

/* FCP LOG Message Structures */
extern msgLogDef elx_msgBlk0700;
extern msgLogDef elx_msgBlk0701;
extern msgLogDef elx_msgBlk0702;
extern msgLogDef elx_msgBlk0703;
extern msgLogDef elx_msgBlk0706;
extern msgLogDef elx_msgBlk0710;
extern msgLogDef elx_msgBlk0712;
extern msgLogDef elx_msgBlk0713;
extern msgLogDef elx_msgBlk0714;
extern msgLogDef elx_msgBlk0716;
extern msgLogDef elx_msgBlk0717;
extern msgLogDef elx_msgBlk0729;
extern msgLogDef elx_msgBlk0730;
extern msgLogDef elx_msgBlk0732;
extern msgLogDef elx_msgBlk0734;
extern msgLogDef elx_msgBlk0735;
extern msgLogDef elx_msgBlk0736;
extern msgLogDef elx_msgBlk0737;
extern msgLogDef elx_msgBlk0747;
extern msgLogDef elx_msgBlk0748;
extern msgLogDef elx_msgBlk0749;
extern msgLogDef elx_msgBlk0754;

/* NODE LOG Message Structures */
extern msgLogDef elx_msgBlk0900;
extern msgLogDef elx_msgBlk0901;
extern msgLogDef elx_msgBlk0902;
extern msgLogDef elx_msgBlk0903;
extern msgLogDef elx_msgBlk0904;
extern msgLogDef elx_msgBlk0905;
extern msgLogDef elx_msgBlk0906;
extern msgLogDef elx_msgBlk0907;
extern msgLogDef elx_msgBlk0908;
extern msgLogDef elx_msgBlk0910;
extern msgLogDef elx_msgBlk0911;
extern msgLogDef elx_msgBlk0927;
extern msgLogDef elx_msgBlk0928;
extern msgLogDef elx_msgBlk0929;
extern msgLogDef elx_msgBlk0930;
extern msgLogDef elx_msgBlk0931;
extern msgLogDef elx_msgBlk0932;

/* MISC LOG Message Structures */
extern msgLogDef elx_msgBlk1201;
extern msgLogDef elx_msgBlk1202;
extern msgLogDef elx_msgBlk1204;
extern msgLogDef elx_msgBlk1205;
extern msgLogDef elx_msgBlk1206;
extern msgLogDef elx_msgBlk1207;
extern msgLogDef elx_msgBlk1208;
extern msgLogDef elx_msgBlk1210;
extern msgLogDef elx_msgBlk1211;
extern msgLogDef elx_msgBlk1212;
extern msgLogDef elx_msgBlk1213;

/* LINK LOG Message Structures */
extern msgLogDef elx_msgBlk1300;
extern msgLogDef elx_msgBlk1301;
extern msgLogDef elx_msgBlk1302;
extern msgLogDef elx_msgBlk1303;
extern msgLogDef elx_msgBlk1304;
extern msgLogDef elx_msgBlk1305;
extern msgLogDef elx_msgBlk1306;
extern msgLogDef elx_msgBlk1307;

/* CHK CONDITION LOG Message Structures */

/* IOCtl LOG Message Structures */
extern msgLogDef elx_msgBlk1600;
extern msgLogDef elx_msgBlk1601;
extern msgLogDef elx_msgBlk1602;
extern msgLogDef elx_msgBlk1603;
extern msgLogDef elx_msgBlk1604;
extern msgLogDef elx_msgBlk1605;

/* 
 * LOG Messages Numbers
 */

/* ELS LOG Message Numbers */
#define ELX_LOG_MSG_EL_0100    100
#define ELX_LOG_MSG_EL_0101    101
#define ELX_LOG_MSG_EL_0102    102
#define ELX_LOG_MSG_EL_0103    103
#define ELX_LOG_MSG_EL_0104    104
#define ELX_LOG_MSG_EL_0105    105
#define ELX_LOG_MSG_EL_0106    106
#define ELX_LOG_MSG_EL_0107    107
#define ELX_LOG_MSG_EL_0108    108
#define ELX_LOG_MSG_EL_0109    109
#define ELX_LOG_MSG_EL_0110    110
#define ELX_LOG_MSG_EL_0111    111
#define ELX_LOG_MSG_EL_0112    112
#define ELX_LOG_MSG_EL_0113    113
#define ELX_LOG_MSG_EL_0114    114
#define ELX_LOG_MSG_EL_0115    115
#define ELX_LOG_MSG_EL_0116    116
#define ELX_LOG_MSG_EL_0117    117
#define ELX_LOG_MSG_EL_0118    118
#define ELX_LOG_MSG_EL_0119    119
#define ELX_LOG_MSG_EL_0120    120
#define ELX_LOG_MSG_EL_0121    121
#define ELX_LOG_MSG_EL_0122    122
#define ELX_LOG_MSG_EL_0123    123
#define ELX_LOG_MSG_EL_0124    124
#define ELX_LOG_MSG_EL_0125    125
#define ELX_LOG_MSG_EL_0126    126
#define ELX_LOG_MSG_EL_0127    127

/* DISCOVERY LOG Message Numbers */
#define ELX_LOG_MSG_DI_0200    200
#define ELX_LOG_MSG_DI_0201    201
#define ELX_LOG_MSG_DI_0202    202
#define ELX_LOG_MSG_DI_0203    203
#define ELX_LOG_MSG_DI_0204    204
#define ELX_LOG_MSG_DI_0205    205
#define ELX_LOG_MSG_DI_0206    206
#define ELX_LOG_MSG_DI_0207    207
#define ELX_LOG_MSG_DI_0208    208
#define ELX_LOG_MSG_DI_0209    209
#define ELX_LOG_MSG_DI_0210    210
#define ELX_LOG_MSG_DI_0211    211
#define ELX_LOG_MSG_DI_0212    212
#define ELX_LOG_MSG_DI_0213    213
#define ELX_LOG_MSG_DI_0214    214
#define ELX_LOG_MSG_DI_0215    215
#define ELX_LOG_MSG_DI_0216    216
#define ELX_LOG_MSG_DI_0217    217
#define ELX_LOG_MSG_DI_0218    218
#define ELX_LOG_MSG_DI_0219    219
#define ELX_LOG_MSG_DI_0220    220
#define ELX_LOG_MSG_DI_0221    221
#define ELX_LOG_MSG_DI_0222    222
#define ELX_LOG_MSG_DI_0223    223
#define ELX_LOG_MSG_DI_0224    224
#define ELX_LOG_MSG_DI_0225    225
#define ELX_LOG_MSG_DI_0226    226
#define ELX_LOG_MSG_DI_0227    227
#define ELX_LOG_MSG_DI_0228    228
#define ELX_LOG_MSG_DI_0229    229
#define ELX_LOG_MSG_DI_0230    230
#define ELX_LOG_MSG_DI_0231    231
#define ELX_LOG_MSG_DI_0232    232
#define ELX_LOG_MSG_DI_0234    234
#define ELX_LOG_MSG_DI_0235    235
#define ELX_LOG_MSG_DI_0236    236
#define ELX_LOG_MSG_DI_0237    237
#define ELX_LOG_MSG_DI_0238    238
#define ELX_LOG_MSG_DI_0239    239
#define ELX_LOG_MSG_DI_0240    240
#define ELX_LOG_MSG_DI_0241    241
#define ELX_LOG_MSG_DI_0243    243
#define ELX_LOG_MSG_DI_0244    244
#define ELX_LOG_MSG_DI_0245    245
#define ELX_LOG_MSG_DI_0246    246
#define ELX_LOG_MSG_DI_0247    247
#define ELX_LOG_MSG_DI_0248    248

/* MAILBOX LOG Message Numbers */
#define ELX_LOG_MSG_MB_0300    300
#define ELX_LOG_MSG_MB_0301    301
#define ELX_LOG_MSG_MB_0302    302
#define ELX_LOG_MSG_MB_0304    304
#define ELX_LOG_MSG_MB_0305    305
#define ELX_LOG_MSG_MB_0306    306
#define ELX_LOG_MSG_MB_0307    307
#define ELX_LOG_MSG_MB_0308    308
#define ELX_LOG_MSG_MB_0309    309
#define ELX_LOG_MSG_MB_0310    310
#define ELX_LOG_MSG_MB_0311    311
#define ELX_LOG_MSG_MB_0312    312
#define ELX_LOG_MSG_MB_0313    313
#define ELX_LOG_MSG_MB_0314    314
#define ELX_LOG_MSG_MB_0315    315
#define ELX_LOG_MSG_MB_0316    316
#define ELX_LOG_MSG_MB_0317    317
#define ELX_LOG_MSG_MB_0318    318
#define ELX_LOG_MSG_MB_0319    319
#define ELX_LOG_MSG_MB_0320    320
#define ELX_LOG_MSG_MB_0321    321
#define ELX_LOG_MSG_MB_0322    322
#define ELX_LOG_MSG_MB_0323    323
#define ELX_LOG_MSG_MB_0324    324

/* INIT LOG Message Numbers */
#define ELX_LOG_MSG_IN_0405    405
#define ELX_LOG_MSG_IN_0406    406
#define ELX_LOG_MSG_IN_0407    407
#define ELX_LOG_MSG_IN_0409    409
#define ELX_LOG_MSG_IN_0410    410
#define ELX_LOG_MSG_IN_0411    411
#define ELX_LOG_MSG_IN_0412    412
#define ELX_LOG_MSG_IN_0413    413
#define ELX_LOG_MSG_IN_0430    430
#define ELX_LOG_MSG_IN_0431    431
#define ELX_LOG_MSG_IN_0432    432
#define ELX_LOG_MSG_IN_0433    433
#define ELX_LOG_MSG_IN_0434    434
#define ELX_LOG_MSG_IN_0435    435
#define ELX_LOG_MSG_IN_0436    436
#define ELX_LOG_MSG_IN_0437    437
#define ELX_LOG_MSG_IN_0438    438
#define ELX_LOG_MSG_IN_0439    439
#define ELX_LOG_MSG_IN_0440    440
#define ELX_LOG_MSG_IN_0441    441
#define ELX_LOG_MSG_IN_0442    442
#define ELX_LOG_MSG_IN_0446    446
#define ELX_LOG_MSG_IN_0447    447
#define ELX_LOG_MSG_IN_0448    448
#define ELX_LOG_MSG_IN_0449    449
#define ELX_LOG_MSG_IN_0450    450
#define ELX_LOG_MSG_IN_0451    451
#define ELX_LOG_MSG_IN_0453    453
#define ELX_LOG_MSG_IN_0454    454
#define ELX_LOG_MSG_IN_0455    455
#define ELX_LOG_MSG_IN_0457    457
#define ELX_LOG_MSG_IN_0458    458
#define ELX_LOG_MSG_IN_0460    460
#define ELX_LOG_MSG_IN_0462    462

/*
 * Available.ELX_LOG_MSG_IN_0500    500
 */

/* IP LOG Message Numbers */
#define ELX_LOG_MSG_IP_0600    600
#define ELX_LOG_MSG_IP_0601    601
#define ELX_LOG_MSG_IP_0602    602
#define ELX_LOG_MSG_IP_0603    603
#define ELX_LOG_MSG_IP_0604    604
#define ELX_LOG_MSG_IP_0605    605
#define ELX_LOG_MSG_IP_0606    606
#define ELX_LOG_MSG_IP_0607    607
#define ELX_LOG_MSG_IP_0608    608
#define ELX_LOG_MSG_IP_0609    609
#define ELX_LOG_MSG_IP_0610    610

/* FCP LOG Message Numbers */
#define ELX_LOG_MSG_FP_0700    700
#define ELX_LOG_MSG_FP_0701    701
#define ELX_LOG_MSG_FP_0702    702
#define ELX_LOG_MSG_FP_0703    703
#define ELX_LOG_MSG_FP_0706    706
#define ELX_LOG_MSG_FP_0710    710
#define ELX_LOG_MSG_FP_0712    712
#define ELX_LOG_MSG_FP_0713    713
#define ELX_LOG_MSG_FP_0714    714
#define ELX_LOG_MSG_FP_0716    716
#define ELX_LOG_MSG_FP_0717    717
#define ELX_LOG_MSG_FP_0729    729
#define ELX_LOG_MSG_FP_0730    730
#define ELX_LOG_MSG_FP_0732    732
#define ELX_LOG_MSG_FP_0734    734
#define ELX_LOG_MSG_FP_0735    735
#define ELX_LOG_MSG_FP_0736    736
#define ELX_LOG_MSG_FP_0737    737
#define ELX_LOG_MSG_FP_0747    747
#define ELX_LOG_MSG_FP_0748    748
#define ELX_LOG_MSG_FP_0749    749
#define ELX_LOG_MSG_FP_0754    754

/*
 * Available:  ELX_LOG_MSG_FP_0800    800
 */

/* NODE LOG Message Numbers */
#define ELX_LOG_MSG_ND_0900    900
#define ELX_LOG_MSG_ND_0901    901
#define ELX_LOG_MSG_ND_0902    902
#define ELX_LOG_MSG_ND_0903    903
#define ELX_LOG_MSG_ND_0904    904
#define ELX_LOG_MSG_ND_0905    905
#define ELX_LOG_MSG_ND_0906    906
#define ELX_LOG_MSG_ND_0907    907
#define ELX_LOG_MSG_ND_0908    908
#define ELX_LOG_MSG_ND_0910    910
#define ELX_LOG_MSG_ND_0911    911
#define ELX_LOG_MSG_ND_0927    927
#define ELX_LOG_MSG_ND_0928    928
#define ELX_LOG_MSG_ND_0929    929
#define ELX_LOG_MSG_ND_0930    930
#define ELX_LOG_MSG_ND_0931    931
#define ELX_LOG_MSG_ND_0932    932

/* MISC LOG Message Numbers */
#define ELX_LOG_MSG_MI_1201   1201
#define ELX_LOG_MSG_MI_1202   1202
#define ELX_LOG_MSG_MI_1204   1204
#define ELX_LOG_MSG_MI_1205   1205
#define ELX_LOG_MSG_MI_1206   1206
#define ELX_LOG_MSG_MI_1207   1207
#define ELX_LOG_MSG_MI_1208   1208
#define ELX_LOG_MSG_MI_1210   1210
#define ELX_LOG_MSG_MI_1211   1211
#define ELX_LOG_MSG_MI_1212   1212
#define ELX_LOG_MSG_MI_1213   1213

/* LINK LOG Message Numbers */
#define ELX_LOG_MSG_LK_1300   1300
#define ELX_LOG_MSG_LK_1301   1301
#define ELX_LOG_MSG_LK_1302   1302
#define ELX_LOG_MSG_LK_1303   1303
#define ELX_LOG_MSG_LK_1304   1304
#define ELX_LOG_MSG_LK_1305   1305
#define ELX_LOG_MSG_LK_1306   1306
#define ELX_LOG_MSG_LK_1307   1307

/* CHK COMDITION LOG Message Numbers */
/*
 * Available ELX_LOG_MSG_LK_1500   1500
 */

/* IOCtl LOG Message Numbers */
#define ELX_LOG_MSG_IO_1600   1600
#define ELX_LOG_MSG_IO_1601   1601
#define ELX_LOG_MSG_IO_1602   1602
#define ELX_LOG_MSG_IO_1603   1603
#define ELX_LOG_MSG_IO_1604   1604
#define ELX_LOG_MSG_IO_1605   1605

#endif				/* _H_ELX_LOGMSG */
