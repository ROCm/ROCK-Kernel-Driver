/*******************************************************************************

This software program is available to you under a choice of one of two 
licenses. You may choose to be licensed under either the GNU General Public 
License 2.0, June 1991, available at http://www.fsf.org/copyleft/gpl.html, 
or the Intel BSD + Patent License, the text of which follows:

Recipient has requested a license and Intel Corporation ("Intel") is willing
to grant a license for the software entitled Linux Base Driver for the 
Intel(R) PRO/100 Family of Adapters (e100) (the "Software") being provided 
by Intel Corporation. The following definitions apply to this license:

"Licensed Patents" means patent claims licensable by Intel Corporation which 
are necessarily infringed by the use of sale of the Software alone or when 
combined with the operating system referred to below.

"Recipient" means the party to whom Intel delivers this Software.

"Licensee" means Recipient and those third parties that receive a license to 
any operating system available under the GNU General Public License 2.0 or 
later.

Copyright (c) 1999 - 2002 Intel Corporation.
All rights reserved.

The license is provided to Recipient and Recipient's Licensees under the 
following terms.

Redistribution and use in source and binary forms of the Software, with or 
without modification, are permitted provided that the following conditions 
are met:

Redistributions of source code of the Software may retain the above 
copyright notice, this list of conditions and the following disclaimer.

Redistributions in binary form of the Software may reproduce the above 
copyright notice, this list of conditions and the following disclaimer in 
the documentation and/or materials provided with the distribution.

Neither the name of Intel Corporation nor the names of its contributors 
shall be used to endorse or promote products derived from this Software 
without specific prior written permission.

Intel hereby grants Recipient and Licensees a non-exclusive, worldwide, 
royalty-free patent license under Licensed Patents to make, use, sell, offer 
to sell, import and otherwise transfer the Software, if any, in source code 
and object code form. This license shall include changes to the Software 
that are error corrections or other minor changes to the Software that do 
not add functionality or features when the Software is incorporated in any 
version of an operating system that has been distributed under the GNU 
General Public License 2.0 or later. This patent license shall apply to the 
combination of the Software and any operating system licensed under the GNU 
General Public License 2.0 or later if, at the time Intel provides the 
Software to Recipient, such addition of the Software to the then publicly 
available versions of such operating systems available under the GNU General 
Public License 2.0 or later (whether in gold, beta or alpha form) causes 
such combination to be covered by the Licensed Patents. The patent license 
shall not apply to any other combinations which include the Software. NO 
hardware per se is licensed hereunder.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
IMPLIED WARRANTIES OF MECHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR IT CONTRIBUTORS BE LIABLE FOR ANY 
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
ANY LOSS OF USE; DATA, OR PROFITS; OR BUSINESS INTERUPTION) HOWEVER CAUSED 
AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR 
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#ifndef E100_VENDOR_ID_INFO
#define E100_VENDOR_ID_INFO
/* ====================================================================== */
/*                              vendor_info                               */
/* ====================================================================== */

struct e100_vendor_info {
	unsigned long device_type;
	char *idstr;
};

enum e100_device_type {
	E100_BRD_100TX = 1,
	E100_BRD_100T4,
	E100_BRD_10T,
	E100_BRD_100WFM,
	E100_BRD_82557,
	E100_BRD_82557_WOL,
	E100_BRD_82558,
	E100_BRD_82558_WOL,
	E100_BRD_100,
	E100_BRD_100M,
	E100_BRD_AOL2,
	E100_BRD_AOL,
	E100_PROS_M,
	E100_PROS_AM,
	E100_PROS_AM_AOL,
	E100_PROS_DT,
	E100_PRO_DT,
	E100_PROM_DT,
	E100_PRO_SRV,
	E100_PRO_SRVP,
	E100_PROS_SRV,
	E100_PRO_DUAL,
	E100_PROS_DUAL,
	E100_PROP_DUAL,
	E100_PROP_WOL,
	E100_PROS_MOB,
	E100_PRO_CB,
	E100_PRO_CB_M,
	E100_PROSR_MOB,
	E100_PROS_MC,
	E100_PROSR_MC,
	E100_PROP_MC,
	E100_PROSP_MC,
	E100_PROP_MOB,
	E100_PROSP_MOB,
	E100_PRO_MINI,
	E100_PRO_NET,
	E100_PROS_NET,
	E100_PROVM_NET,
	E100_PROVE_D,
	E100_82559_LOM,
	E100_82559_LOM_AOL,
	E100_82559_LOM_AOL2,
	E100_82559_LOM_DELL,
	E100_IBM_MDS,
	E100_CMPQ_S,
	E100_PROVE_DA,
	E100_PROVM_DA,
	E100_PROVE_LOM,
	E100_PROVE_NET,
	E100_82562,
	E100_82551QM,
	E100_ALL_BOARDS
};

struct e100_vendor_info e100_vendor_info_array[] = {
	{ E100_BRD_100TX, "Intel(R) PRO/100B PCI Adapter (TX)"},
	{ E100_BRD_100T4, "Intel(R) PRO/100B PCI Adapter (T4)"},
	{ E100_BRD_10T, "Intel(R) PRO/10+ PCI Adapter"},
	{ E100_BRD_100WFM, "Intel(R) PRO/100 WfM PCI Adapter"},
	{ E100_BRD_82557, "Intel(R) 82557-based Integrated Ethernet PCI (10/100)"},
	{ E100_BRD_82557_WOL, "Intel(R) 82557-based Integrated Ethernet with Wake on LAN*"},
	{ E100_BRD_82558, "Intel(R) 82558-based Integrated Ethernet"},
	{ E100_BRD_82558_WOL, "Intel(R) 82558-based Integrated Ethernet with Wake on LAN*"},
	{ E100_BRD_100, "Intel(R) PRO/100+ PCI Adapter"},
	{ E100_BRD_100M, "Intel(R) PRO/100+ Management Adapter"},
	{ E100_BRD_AOL2, "Intel(R) PRO/100+ Alert on LAN* 2 Management Adapter"},
	{ E100_82559_LOM_DELL, "Intel(R) 8255x Based Network Connection"},
	{ E100_BRD_AOL, "Intel(R) PRO/100+ Alert on LAN* Management Adapter"},
	{ E100_PROS_M, "Intel(R) PRO/100 S Management Adapter"},
	{ E100_PROS_AM, "Intel(R) PRO/100 S Advanced Management Adapter"},
	{ E100_PROS_AM_AOL, "Intel(R) PRO/100+ Management Adapter with Alert On LAN* GC"},
	{ E100_PROS_DT, "Intel(R) PRO/100 S Desktop Adapter"},
	{ E100_PRO_DT, "Intel(R) PRO/100 Desktop Adapter"},
	{ E100_PROM_DT, "Intel(R) PRO/100 M Desktop Adapter"},
	{ E100_PRO_SRV, "Intel(R) PRO/100+ Server Adapter"},
	{ E100_PRO_SRVP, "Intel(R) PRO/100+ Server Adapter (PILA8470B)"},
	{ E100_PROS_SRV, "Intel(R) PRO/100 S Server Adapter"},
	{ E100_PRO_DUAL, "Intel(R) PRO/100 Dual Port Server Adapter"},
	{ E100_PROS_DUAL, "Intel(R) PRO/100 S Dual Port Server Adapter"},
	{ E100_PROP_DUAL, "Intel(R) PRO/100+ Dual Port Server Adapter"},
	{ E100_PROP_WOL, "Intel(R) PRO/100+ Management Adapter with Alert On LAN* G Server"},
	{ E100_PROS_MOB, "Intel(R) PRO/100 S Mobile Adapter"},
	{ E100_PRO_CB, "Intel(R) PRO/100 CardBus II"},
	{ E100_PRO_CB_M, "Intel(R) PRO/100 LAN+Modem56 CardBus II"},
	{ E100_PROSR_MOB, "Intel(R) PRO/100 SR Mobile Adapter"},
	{ E100_PROS_MC, "Intel(R) PRO/100 S Mobile Combo Adapter"},
	{ E100_PROSR_MC, "Intel(R) PRO/100 SR Mobile Combo Adapter"},
	{ E100_PROP_MC, "Intel(R) PRO/100 P Mobile Combo Adapter"},
	{ E100_PROSP_MC, "Intel(R) PRO/100 SP Mobile Combo Adapter"},
	{ E100_PROP_MOB, "Intel(R) PRO/100 P Mobile Adapter"},
	{ E100_PROSP_MOB, "Intel(R) PRO/100 SP Mobile Adapter"},
	{ E100_PRO_MINI, "Intel(R) PRO/100+ Mini PCI"},
	{ E100_PRO_NET, "Intel(R) PRO/100 Network Connection" },
	{ E100_PROS_NET, "Intel(R) PRO/100 S Network Connection" },
	{ E100_PROVM_NET, "Intel(R) PRO/100 VM Network Connection"},
	{ E100_PROVE_D, "Intel(R) PRO/100 VE Desktop Connection"},
	{ E100_82559_LOM, "Intel(R) 82559 Fast Ethernet LAN on Motherboard"},
	{ E100_82559_LOM_AOL, "Intel(R) 82559 Fast Ethernet LOM with Alert on LAN*" },
	{ E100_82559_LOM_AOL2, "Intel(R) 82559 Fast Ethernet LOM with Alert on LAN* 2" },
	{ E100_IBM_MDS, "IBM Mobile, Desktop & Server Adapters"},
	{ E100_CMPQ_S, "Compaq Fast Ethernet Server Adapter" },
	{ E100_PROVE_DA, "Intel(R) PRO/100 VE Desktop Adapter"},
	{ E100_PROVM_DA, "Intel(R) PRO/100 VM Desktop Adapter"},
	{ E100_PROVE_LOM, "Intel(R) PRO/100 VE Network ConnectionPLC LOM" },
	{ E100_PROVE_NET, "Intel(R) PRO/100 VE Network Connection"},
	{ E100_82562, "Intel(R)82562 based Fast Ethernet Connection"},
	{ E100_82551QM, "Intel(R) PRO/100 M Mobile Connection"},
	{ E100_ALL_BOARDS, "Intel(R) 8255x-based Ethernet Adapter"},
	{0,NULL}
};

static struct pci_device_id e100_id_table[] __devinitdata = {
	{0x8086, 0x1229, 0x8086, 0x0001, 0, 0, E100_BRD_100TX},
	{0x8086, 0x1229, 0x8086, 0x0002, 0, 0, E100_BRD_100T4},
	{0x8086, 0x1229, 0x8086, 0x0003, 0, 0, E100_BRD_10T},
	{0x8086, 0x1229, 0x8086, 0x0004, 0, 0, E100_BRD_100WFM},
	{0x8086, 0x1229, 0x8086, 0x0005, 0, 0, E100_BRD_82557},
	{0x8086, 0x1229, 0x8086, 0x0006, 0, 0, E100_BRD_82557_WOL},
	{0x8086, 0x1229, 0x8086, 0x0002, 0, 0, E100_BRD_100T4},
	{0x8086, 0x1229, 0x8086, 0x0003, 0, 0, E100_BRD_10T},
	{0x8086, 0x1229, 0x8086, 0x0004, 0, 0, E100_BRD_100WFM},
	{0x8086, 0x1229, 0x8086, 0x0005, 0, 0, E100_BRD_82557},
	{0x8086, 0x1229, 0x8086, 0x0006, 0, 0, E100_BRD_82557_WOL},
	{0x8086, 0x1229, 0x8086, 0x0007, 0, 0, E100_BRD_82558},
	{0x8086, 0x1229, 0x8086, 0x0008, 0, 0, E100_BRD_82558_WOL},
	{0x8086, 0x1229, 0x8086, 0x0009, 0, 0, E100_BRD_100},
	{0x8086, 0x1229, 0x8086, 0x000A, 0, 0, E100_BRD_100M},
	{0x8086, 0x1229, 0x8086, 0x000B, 0, 0, E100_BRD_100},
	{0x8086, 0x1229, 0x8086, 0x000C, 0, 0, E100_BRD_100M},
	{0x8086, 0x1229, 0x8086, 0x000D, 0, 0, E100_BRD_AOL2},
	{0x8086, 0x1229, 0x8086, 0x000E, 0, 0, E100_BRD_AOL},
	{0x8086, 0x1229, 0x8086, 0x0010, 0, 0, E100_PROS_M},
	{0x8086, 0x1229, 0x8086, 0x0011, 0, 0, E100_PROS_M},
	{0x8086, 0x1229, 0x8086, 0x0012, 0, 0, E100_PROS_AM},
	{0x8086, 0x1229, 0x8086, 0x0013, 0, 0, E100_PROS_AM},
	{0x8086, 0x1229, 0x8086, 0x0030, 0, 0, E100_PROS_AM_AOL},
	{0x8086, 0x1229, 0x8086, 0x0040, 0, 0, E100_PROS_DT},
	{0x8086, 0x1229, 0x8086, 0x0041, 0, 0, E100_PROS_DT},
	{0x8086, 0x1229, 0x8086, 0x0042, 0, 0, E100_PRO_DT},
	{0x8086, 0x1229, 0x8086, 0x0050, 0, 0, E100_PROS_DT},
	{0x8086, 0x1229, 0x8086, 0x0070, 0, 0, E100_PROM_DT},
	{0x8086, 0x1229, 0x8086, 0x1009, 0, 0, E100_PRO_SRV},
	{0x8086, 0x1229, 0x8086, 0x100C, 0, 0, E100_PRO_SRVP},
	{0x8086, 0x1229, 0x8086, 0x1012, 0, 0, E100_PROS_SRV},
	{0x8086, 0x1229, 0x8086, 0x1013, 0, 0, E100_PROS_SRV},
	{0x8086, 0x1229, 0x8086, 0x1014, 0, 0, E100_PRO_DUAL},
	{0x8086, 0x1229, 0x8086, 0x1015, 0, 0, E100_PROS_DUAL},
	{0x8086, 0x1229, 0x8086, 0x1016, 0, 0, E100_PROS_DUAL},
	{0x8086, 0x1229, 0x8086, 0x1017, 0, 0, E100_PROP_DUAL},
	{0x8086, 0x1229, 0x8086, 0x1030, 0, 0, E100_PROP_WOL},
	{0x8086, 0x1229, 0x8086, 0x1040, 0, 0, E100_PROS_SRV},
	{0x8086, 0x1229, 0x8086, 0x1041, 0, 0, E100_PROS_SRV},
	{0x8086, 0x1229, 0x8086, 0x1042, 0, 0, E100_PRO_SRV},
	{0x8086, 0x1229, 0x8086, 0x1050, 0, 0, E100_PROS_SRV},
	{0x8086, 0x1229, 0x8086, 0x10F0, 0, 0, E100_PROP_DUAL}, 
	{0x8086, 0x1229, 0x8086, 0x10F0, 0, 0, E100_PROP_DUAL}, 
	{0x8086, 0x1229, 0x8086, 0x2009, 0, 0, E100_PROS_MOB},
	{0x8086, 0x1229, 0x8086, 0x200D, 0, 0, E100_PRO_CB},
	{0x8086, 0x1229, 0x8086, 0x200E, 0, 0, E100_PRO_CB_M},
	{0x8086, 0x1229, 0x8086, 0x200F, 0, 0, E100_PROSR_MOB},
	{0x8086, 0x1229, 0x8086, 0x2010, 0, 0, E100_PROS_MC},
	{0x8086, 0x1229, 0x8086, 0x2013, 0, 0, E100_PROSR_MC},
	{0x8086, 0x1229, 0x8086, 0x2016, 0, 0, E100_PROS_MOB},
	{0x8086, 0x1229, 0x8086, 0x2017, 0, 0, E100_PROS_MC},
	{0x8086, 0x1229, 0x8086, 0x2018, 0, 0, E100_PROSR_MOB},
	{0x8086, 0x1229, 0x8086, 0x2019, 0, 0, E100_PROSR_MC},
	{0x8086, 0x1229, 0x8086, 0x2101, 0, 0, E100_PROP_MOB},
	{0x8086, 0x1229, 0x8086, 0x2102, 0, 0, E100_PROSP_MOB},
	{0x8086, 0x1229, 0x8086, 0x2103, 0, 0, E100_PROSP_MOB},
	{0x8086, 0x1229, 0x8086, 0x2104, 0, 0, E100_PROSP_MOB},
	{0x8086, 0x1229, 0x8086, 0x2105, 0, 0, E100_PROSP_MOB},
	{0x8086, 0x1229, 0x8086, 0x2106, 0, 0, E100_PROP_MOB},
	{0x8086, 0x1229, 0x8086, 0x2107, 0, 0, E100_PRO_NET},
	{0x8086, 0x1229, 0x8086, 0x2108, 0, 0, E100_PRO_NET},
	{0x8086, 0x1229, 0x8086, 0x2200, 0, 0, E100_PROP_MC},
	{0x8086, 0x1229, 0x8086, 0x2201, 0, 0, E100_PROP_MC},
	{0x8086, 0x1229, 0x8086, 0x2202, 0, 0, E100_PROSP_MC},
	{0x8086, 0x1229, 0x8086, 0x2203, 0, 0, E100_PRO_MINI},
	{0x8086, 0x1229, 0x8086, 0x2204, 0, 0, E100_PRO_MINI},
	{0x8086, 0x1229, 0x8086, 0x2205, 0, 0, E100_PROSP_MC},
	{0x8086, 0x1229, 0x8086, 0x2206, 0, 0, E100_PROSP_MC},
	{0x8086, 0x1229, 0x8086, 0x2207, 0, 0, E100_PROSP_MC},
	{0x8086, 0x1229, 0x8086, 0x2208, 0, 0, E100_PROP_MC},
	{0x8086, 0x1229, 0x8086, 0x2408, 0, 0, E100_PRO_MINI},
	{0x8086, 0x1229, 0x8086, 0x240F, 0, 0, E100_PRO_MINI},
	{0x8086, 0x1229, 0x8086, 0x2411, 0, 0, E100_PRO_MINI},
	{0x8086, 0x1229, 0x8086, 0x3400, 0, 0, E100_82559_LOM},
	{0x8086, 0x1229, 0x8086, 0x3000, 0, 0, E100_82559_LOM},
	{0x8086, 0x1229, 0x8086, 0x3001, 0, 0, E100_82559_LOM_AOL},
	{0x8086, 0x1229, 0x8086, 0x3002, 0, 0, E100_82559_LOM_AOL2},
	{0x8086, 0x1229, 0x8086, 0x3006, 0, 0, E100_PROS_NET},
	{0x8086, 0x1229, 0x8086, 0x3007, 0, 0, E100_PROS_NET},
	{0x8086, 0x1229, 0x8086, 0x3008, 0, 0, E100_PRO_NET},
	{0x8086, 0x1229, 0x8086, 0x3010, 0, 0, E100_PROS_NET},
	{0x8086, 0x1229, 0x8086, 0x3011, 0, 0, E100_PROS_NET},
	{0x8086, 0x1229, 0x8086, 0x3012, 0, 0, E100_PRO_NET},
	{0x8086, 0x1229, 0x1014, 0x005C, 0, 0, E100_IBM_MDS},   
	{0x8086, 0x1229, 0x1014, 0x305C, 0, 0, E100_IBM_MDS},   
	{0x8086, 0x1229, 0x1014, 0x405C, 0, 0, E100_IBM_MDS},   
	{0x8086, 0x1229, 0x1014, 0x605C, 0, 0, E100_IBM_MDS},   
	{0x8086, 0x1229, 0x1014, 0x505C, 0, 0, E100_IBM_MDS},   
	{0x8086, 0x1229, 0x1014, 0x105C, 0, 0, E100_IBM_MDS},   
	{0x8086, 0x1229, 0x1014, 0x805C, 0, 0, E100_IBM_MDS},   
	{0x8086, 0x1229, 0x1014, 0x705C, 0, 0, E100_IBM_MDS},   
	{0x8086, 0x1229, 0x1014, 0x01F1, 0, 0, E100_IBM_MDS},   
	{0x8086, 0x1229, 0x1014, 0x0232, 0, 0, E100_IBM_MDS},   
	{0x8086, 0x1229, 0x1014, 0x0207, 0, 0, E100_PRO_NET},     
	{0x8086, 0x1229, 0x1014, 0x023F, 0, 0, E100_PRO_NET},   
	{0x8086, 0x1229, 0x1014, 0x01BC, 0, 0, E100_PRO_NET},     
	{0x8086, 0x1229, 0x1014, 0x01CE, 0, 0, E100_PRO_NET},     
	{0x8086, 0x1229, 0x1014, 0x01DC, 0, 0, E100_PRO_NET},     
	{0x8086, 0x1229, 0x1014, 0x01EB, 0, 0, E100_PRO_NET},     
	{0x8086, 0x1229, 0x1014, 0x01EC, 0, 0, E100_PRO_NET},     
	{0x8086, 0x1229, 0x1014, 0x0202, 0, 0, E100_PRO_NET},     
	{0x8086, 0x1229, 0x1014, 0x0205, 0, 0, E100_PRO_NET},     
	{0x8086, 0x1229, 0x1014, 0x0217, 0, 0, E100_PRO_NET},     
	{0x8086, 0x1229, 0x0E11, 0xB01E, 0, 0, E100_CMPQ_S},         
	{0x8086, 0x1229, 0x0E11, 0xB02F, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB04A, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB0C6, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB0C7, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB0D7, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB0DD, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB0DE, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB0E1, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB134, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB13C, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB144, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB163, 0, 0, E100_CMPQ_S},     
	{0x8086, 0x1229, 0x0E11, 0xB164, 0, 0, E100_CMPQ_S},
	{0x8086, 0x1229, 0x1028, PCI_ANY_ID, 0, 0, E100_82559_LOM_DELL},
	{0x8086, 0x1229, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_ALL_BOARDS},

	{0x8086, 0x2449, 0x1014, 0x0265, 0, 0, E100_PROVE_D},
	{0x8086, 0x2449, 0x1014, 0x0267, 0, 0, E100_PROVE_D},
	{0x8086, 0x2449, 0x1014, 0x026A, 0, 0, E100_PROVE_D},
	{0x8086, 0x2449, 0x8086, 0x3010, 0, 0, E100_PROVE_DA},
	{0x8086, 0x2449, 0x8086, 0x3011, 0, 0, E100_PROVM_DA},
	{0x8086, 0x2449, 0x8086, 0x3013, 0, 0, E100_PROVE_NET},
	{0x8086, 0x2449, 0x8086, 0x3014, 0, 0, E100_PROVM_NET},
	{0x8086, 0x2449, 0x8086, 0x3016, 0, 0, E100_PROP_MC},
	{0x8086, 0x2449, 0x8086, 0x3017, 0, 0, E100_PROP_MOB},
	{0x8086, 0x2449, 0x8086, 0x3018, 0, 0, E100_PRO_NET},
	{0x8086, 0x2449, 0x0E11, PCI_ANY_ID, 0, 0, E100_PROVM_NET},
	{0x8086, 0x2449, 0x1014, PCI_ANY_ID, 0, 0, E100_PROVE_D},	
	{0x8086, 0x2449, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_ALL_BOARDS},
	
	{0x8086, 0x1059, 0x1179, 0x0005, 0, 0, E100_82551QM},
	{0x8086, 0x1059, 0x1033, 0x8191, 0, 0, E100_82551QM},
	{0x8086, 0x1059, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_82551QM},
	
	{0x8086, 0x1209, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_ALL_BOARDS},
  	{0x8086, 0x1029, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_ALL_BOARDS},
	{0x8086, 0x1030, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_ALL_BOARDS},	
	{0x8086, 0x1031, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_PROVE_NET}, 
	{0x8086, 0x1032, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_PROVE_NET},
	{0x8086, 0x1033, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_PROVM_NET}, 
	{0x8086, 0x1034, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_PROVM_NET}, 
	{0x8086, 0x1038, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_PROVM_NET},
	{0x8086, 0x1039, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_PROVE_NET},
	{0x8086, 0x103A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_PROVE_NET},
	{0x8086, 0x103B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_PROVM_NET},
	{0x8086, 0x103C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_PROVM_NET},
	{0x8086, 0x103D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_PROVE_NET},
	{0x8086, 0x103E, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_PROVM_NET},
	{0x8086, 0x2459, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_82562},
	{0x8086, 0x245D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, E100_82562},
	{0,} /* This has to be the last entry*/
};

#endif /* E100_VENDOR_ID_INFO */
