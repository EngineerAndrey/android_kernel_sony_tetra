/*******************************************************************************
* Copyright 2010,2011 Broadcom Corporation.  All rights reserved.
*
*	@file	avs.h
*
* Unless you and Broadcom execute a separate written software license agreement
* governing use of this software, this software is licensed to you under the
* terms of the GNU General Public License version 2, available at
* http://www.gnu.org/copyleft/gpl.html (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a license
* other than the GPL, without Broadcom's express prior written consent.
*******************************************************************************/

#ifndef ___AVS___
#define ___AVS___

#define CSR_NUM_OPP	4
#define MSR_NUM_OPP	4

#define AVS_HANDSHAKE_VERSION 2
#define AVS_SW_VERSION	2

enum {
	SILICON_TYPE_SLOW,
	SILICON_TYPE_TYP_SLOW,
	SILICON_TYPE_TYPICAL,
	SILICON_TYPE_TYP_FAST,
	SILICON_TYPE_FAST,
	SILICON_TYPE_MAX,
};

enum {
	ARM_FREQ_1000_MHZ,
	ARM_FREQ_1200_MHZ,
	ARM_FREQ_1400_MHZ,
	ARM_FREQ_MAX,
};

enum {
	AVS_DOMAIN_VDDVAR,
	AVS_DOMAIN_VDDVAR_A7,
	AVS_DOMAIN_VDDFIX,
	AVS_DOMAIN_MAX,
};

struct avs_handshake {
	u32 version;
	u32 csr_opp;
	u32 csr_opp_ext;
	u32 msr_opp;
	u32 msr_opp_ext;	/* 5 */
	u8 vddfix;
	u8 vddfix_ret;
	u8 vddvar_ret;
	u8 silicon_type;	/* 6 */
	u32 irdrop_1v2;
	u32 arm_freq;
	s8 temperature;
	u8 np_ratio_1;
	u8 np_ratio_2;
	u8 rsvd_1;		/* 9 */
	u32 row3;
	u32 row3_ext;
	u32 row4;
	u32 row4_ext;
	u32 row5;
	u32 row5_ext;
	u32 row8;
	u32 row8_ext;		/* 17 */
	u8 varspm0;
	u8 varspm1;
	u8 varspm2;
	u8 varspm3;
	u8 varspm4;
	u8 varspm5;
	u8 spm0;
	u8 spm1;
	u8 spm2;
	u8 spm3;
	u8 spm4;
	u8 spm5;		/* 20 */
	u32 error_status;	/* 21 */
	u32 abi_version;
	u32 rsvd_2[3];		/* 25 */
};

struct avs_pdata {
	u32 flags;
	u32 avs_info_base_addr;
	u32 irdrop_vreq;
	char *a9_regl_id;
	u32 pwrwdog_base;
};

#endif	  /*__KONA_AVS___*/
