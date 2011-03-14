/*****************************************************************************
*
* Rhea-specific clock framework
*
* Copyright 2010 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

#include <mach/clock.h>
#include <asm/io.h>
#include <linux/math64.h>
#include <mach/io_map.h>
#include <mach/rdb/brcm_rdb_sysmap_a9.h>
#include <mach/rdb/brcm_rdb_kpm_clk_mgr_reg.h>
#include <mach/rdb/brcm_rdb_kps_clk_mgr_reg.h>
#include <mach/rdb/brcm_rdb_mm_clk_mgr_reg.h>

#define	DECLARE_REF_CLK(clk_name, clk_rate, clk_div, clk_parent)		\
	static struct proc_clock clk_name##_clk = {				\
		.clk	=	{						\
			.name	=	__stringify(clk_name##_clk),		\
			.parent	=	clk_parent,				\
			.rate	=	clk_rate,				\
			.div	=	clk_div,				\
			.id	=	-1,					\
			.ops	=	&ref_clk_ops,				\
		},								\
	}

/* Proc clocks */
static struct proc_clock arm_clk = {
	.clk	=	{
		.name	=	"arm_clk",
		.rate	=	700*CLOCK_1M,
		.div	=	1,
		.id	=	-1,
		.ops	=	&proc_clk_ops,
	},
	.proc_clk_mgr_base = PROC_CLK_BASE_ADDR,
};



/* Ref clocks */
DECLARE_REF_CLK		(crystal, 			26*CLOCK_1M,	1,	0);
DECLARE_REF_CLK		(frac_1m, 			1*CLOCK_1M,		1,	0);
DECLARE_REF_CLK		(ref_96m_varVDD, 		96*CLOCK_1M,	1,	0);
DECLARE_REF_CLK		(var_96m, 			96*CLOCK_1M,	1,	0);
DECLARE_REF_CLK		(ref_96m, 			96*CLOCK_1M,	1,	0);
DECLARE_REF_CLK		(var_500m,	 		500*CLOCK_1M,	1,	0);
DECLARE_REF_CLK		(var_500m_varVDD, 		500*CLOCK_1M,	1,	0);


DECLARE_REF_CLK		(ref_32k, 			32*CLOCK_1K,	1,	0);
DECLARE_REF_CLK		(misc_32k, 			32*CLOCK_1K,	1,	0);

DECLARE_REF_CLK		(ref_312m, 			312*CLOCK_1M,	0,	0);
DECLARE_REF_CLK		(ref_208m, 			208*CLOCK_1M,	0,	name_to_clk(ref_312m));
DECLARE_REF_CLK		(ref_104m, 			104*CLOCK_1M,	3,	name_to_clk(ref_312m));
DECLARE_REF_CLK		(ref_52m, 			52*CLOCK_1M,	2,	name_to_clk(ref_104m));
DECLARE_REF_CLK		(ref_13m, 			13*CLOCK_1M,	4,	name_to_clk(ref_52m));

DECLARE_REF_CLK		(var_312m, 			312*CLOCK_1M,	0,	0);
DECLARE_REF_CLK		(var_208m, 			208*CLOCK_1M,	0,	name_to_clk(var_312m));
DECLARE_REF_CLK		(var_156m, 			156*CLOCK_1M,	2,	name_to_clk(var_312m));
DECLARE_REF_CLK		(var_104m, 			104*CLOCK_1M,	3,	name_to_clk(var_312m));
DECLARE_REF_CLK		(var_52m, 			52*CLOCK_1M,	2,	name_to_clk(var_104m));
DECLARE_REF_CLK		(var_13m, 			13*CLOCK_1M,	4,	name_to_clk(var_52m));

DECLARE_REF_CLK		(usbh_48m, 			48*CLOCK_1M,	1,	0);
DECLARE_REF_CLK		(ref_cx40, 			153600*CLOCK_1K,1,	0);	// FIXME

/* CCU clocks*/
static struct ccu_clock kpm_ccu_clk = {
	.clk	=	{
		.name	=	"kpm_ccu_clk",
		.ops	=	&ccu_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.policy_freq_offset	=	KPM_CLK_MGR_REG_POLICY_FREQ_OFFSET,
	.policy_ctl_offset	=	KPM_CLK_MGR_REG_POLICY_CTL_OFFSET,
	.policy0_mask_offset	=	KPM_CLK_MGR_REG_POLICY0_MASK_OFFSET,
	.policy1_mask_offset	=	KPM_CLK_MGR_REG_POLICY1_MASK_OFFSET,
	.policy2_mask_offset	=	KPM_CLK_MGR_REG_POLICY2_MASK_OFFSET,
	.policy3_mask_offset	=	KPM_CLK_MGR_REG_POLICY3_MASK_OFFSET,
	.lvm_en_offset		=	KPM_CLK_MGR_REG_LVM_EN_OFFSET,

	.freq_id	=	2,
	.freq_tbl	=	{
		 26*CLOCK_1M,  52*CLOCK_1M, 104*CLOCK_1M, 156*CLOCK_1M,
		156*CLOCK_1M, 208*CLOCK_1M, 312*CLOCK_1M, 312*CLOCK_1M
	},
};

static struct ccu_clock kps_ccu_clk = {
	.clk	=	{
		.name	=	"kps_ccu_clk",
		.ops	=	&ccu_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_SLV_CLK_BASE_ADDR,
	.wr_access_offset	=	KPS_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.policy_freq_offset	=	KPS_CLK_MGR_REG_POLICY_FREQ_OFFSET,
	.policy_ctl_offset	=	KPS_CLK_MGR_REG_POLICY_CTL_OFFSET,
	.policy0_mask_offset	=	KPS_CLK_MGR_REG_POLICY0_MASK_OFFSET,
	.policy1_mask_offset	=	KPS_CLK_MGR_REG_POLICY1_MASK_OFFSET,
	.policy2_mask_offset	=	KPS_CLK_MGR_REG_POLICY2_MASK_OFFSET,
	.policy3_mask_offset	=	KPS_CLK_MGR_REG_POLICY3_MASK_OFFSET,
	.lvm_en_offset		=	KPS_CLK_MGR_REG_LVM_EN_OFFSET,

	.freq_id	=	2,
	.freq_tbl	=	{
		 26*CLOCK_1M,  52*CLOCK_1M,  78*CLOCK_1M, 104*CLOCK_1M,
		156*CLOCK_1M, 156*CLOCK_1M
	},
};

static struct ccu_clock mm_ccu_clk = {
	.clk	=	{
		.name	=	"mm_ccu_clk",
		.ops	=	&ccu_clk_ops,
	},

	.ccu_clk_mgr_base	=	MM_CLK_BASE_ADDR,
	.wr_access_offset	=	MM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.policy_freq_offset	=	MM_CLK_MGR_REG_POLICY_FREQ_OFFSET,
	.freq_bit_shift		=	MM_CLK_MGR_REG_POLICY_FREQ_POLICY1_FREQ_SHIFT,
	.policy_ctl_offset	=	MM_CLK_MGR_REG_POLICY_CTL_OFFSET,
	.policy0_mask_offset	=	MM_CLK_MGR_REG_POLICY0_MASK_OFFSET,
	.policy1_mask_offset	=	MM_CLK_MGR_REG_POLICY1_MASK_OFFSET,
	.policy2_mask_offset	=	MM_CLK_MGR_REG_POLICY2_MASK_OFFSET,
	.policy3_mask_offset	=	MM_CLK_MGR_REG_POLICY3_MASK_OFFSET,
	.lvm_en_offset		=	MM_CLK_MGR_REG_LVM_EN_OFFSET,

	.freq_id	=	1,
	.freq_tbl	=	{
		 26*CLOCK_1M,  49920*CLOCK_1K,  83200*CLOCK_1K, 99840*CLOCK_1K,
		166400*CLOCK_1K, 249600*CLOCK_1K
	},
};

/* bus clocks */
/* KPM */
static struct bus_clock usb_otg_clk = {
	.clk	=	{
		.name	=	"usb_otg_clk",
		.parent	=	name_to_clk(kpm_ccu),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_USB_OTG_CLKGATE_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_USB_OTG_CLKGATE_USB_OTG_AHB_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPM_CLK_MGR_REG_USB_OTG_CLKGATE_USB_OTG_AHB_HW_SW_GATING_SEL_SHIFT,
	.clk_en_mask		=	KPM_CLK_MGR_REG_USB_OTG_CLKGATE_USB_OTG_AHB_CLK_EN_MASK,

	.freq_tbl	=	{
		 26*CLOCK_1M,  52*CLOCK_1M,  52*CLOCK_1M,  52*CLOCK_1M,
		 78*CLOCK_1M, 104*CLOCK_1M, 104*CLOCK_1M, 156*CLOCK_1M
	},

};

static struct bus_clock sdio1_ahb_clk = {
	.clk	=	{
		.name	=	"sdio1_ahb_clk",
		.parent	=	name_to_clk(kpm_ccu),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_SDIO1_CLKGATE_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO1_CLKGATE_SDIO1_AHB_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPM_CLK_MGR_REG_SDIO1_CLKGATE_SDIO1_AHB_HW_SW_GATING_SEL_SHIFT,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO1_CLKGATE_SDIO1_AHB_CLK_EN_MASK,

	.freq_tbl	=	{
		 26*CLOCK_1M,  52*CLOCK_1M,  52*CLOCK_1M,  52*CLOCK_1M,
		 78*CLOCK_1M, 104*CLOCK_1M, 104*CLOCK_1M, 156*CLOCK_1M
	},

};

static struct bus_clock sdio2_ahb_clk = {
	.clk	=	{
		.name	=	"sdio2_ahb_clk",
		.parent	=	name_to_clk(kpm_ccu),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_SDIO2_CLKGATE_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO2_CLKGATE_SDIO2_AHB_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPM_CLK_MGR_REG_SDIO2_CLKGATE_SDIO2_AHB_HW_SW_GATING_SEL_SHIFT,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO2_CLKGATE_SDIO2_AHB_CLK_EN_MASK,

	.freq_tbl	=	{
		 26*CLOCK_1M,  52*CLOCK_1M,  52*CLOCK_1M,  52*CLOCK_1M,
		 78*CLOCK_1M, 104*CLOCK_1M, 104*CLOCK_1M, 156*CLOCK_1M
	},

};

static struct bus_clock sdio3_ahb_clk = {
	.clk	=	{
		.name	=	"sdio3_ahb_clk",
		.parent	=	name_to_clk(kpm_ccu),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_SDIO3_CLKGATE_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO3_CLKGATE_SDIO3_AHB_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPM_CLK_MGR_REG_SDIO3_CLKGATE_SDIO3_AHB_HW_SW_GATING_SEL_SHIFT,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO3_CLKGATE_SDIO3_AHB_CLK_EN_MASK,

	.freq_tbl	=	{
		 26*CLOCK_1M,  52*CLOCK_1M,  52*CLOCK_1M,  52*CLOCK_1M,
		 78*CLOCK_1M, 104*CLOCK_1M, 104*CLOCK_1M, 156*CLOCK_1M
	},

};

static struct bus_clock sdio4_ahb_clk = {
	.clk	=	{
		.name	=	"sdio4_ahb_clk",
		.parent	=	name_to_clk(kpm_ccu),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_SDIO4_CLKGATE_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO4_CLKGATE_SDIO4_AHB_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPM_CLK_MGR_REG_SDIO4_CLKGATE_SDIO4_AHB_HW_SW_GATING_SEL_SHIFT,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO4_CLKGATE_SDIO4_AHB_CLK_EN_MASK,

	.freq_tbl	=	{
		 26*CLOCK_1M,  52*CLOCK_1M,  52*CLOCK_1M,  52*CLOCK_1M,
		 78*CLOCK_1M, 104*CLOCK_1M, 104*CLOCK_1M, 156*CLOCK_1M
	},

};

static struct bus_clock sdio1_sleep_clk = {
	.clk	=	{
		.name	=	"sdio1_sleep_clk",
		.rate	=	32 * CLOCK_1K,
		.div	=	1,
		.parent	=	name_to_clk(misc_32k),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_SDIO1_CLKGATE_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO1_CLKGATE_SDIO1_SLEEP_STPRSTS_MASK,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO1_CLKGATE_SDIO1_SLEEP_CLK_EN_MASK,
	.freq_tbl	=	{
		32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K,
		32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K
	},
};

static struct bus_clock sdio2_sleep_clk = {
	.clk	=	{
		.name	=	"sdio2_sleep_clk",
		.rate	=	32 * CLOCK_1K,
		.div	=	1,
		.parent	=	name_to_clk(misc_32k),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_SDIO2_CLKGATE_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO2_CLKGATE_SDIO2_SLEEP_STPRSTS_MASK,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO2_CLKGATE_SDIO2_SLEEP_CLK_EN_MASK,
	.freq_tbl	=	{
		32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K,
		32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K
	},
};

static struct bus_clock sdio3_sleep_clk = {
	.clk	=	{
		.name	=	"sdio3_sleep_clk",
		.rate	=	32 * CLOCK_1K,
		.div	=	1,
		.parent	=	name_to_clk(misc_32k),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_SDIO3_CLKGATE_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO3_CLKGATE_SDIO3_SLEEP_STPRSTS_MASK,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO3_CLKGATE_SDIO3_SLEEP_CLK_EN_MASK,
	.freq_tbl	=	{
		32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K,
		32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K
	},
};

static struct bus_clock sdio4_sleep_clk = {
	.clk	=	{
		.name	=	"sdio4_sleep_clk",
		.rate	=	32 * CLOCK_1K,
		.div	=	1,
		.parent	=	name_to_clk(misc_32k),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_SDIO4_CLKGATE_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO4_CLKGATE_SDIO4_SLEEP_STPRSTS_MASK,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO4_CLKGATE_SDIO4_SLEEP_CLK_EN_MASK,
	.freq_tbl	=	{
		32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K,
		32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K, 32*CLOCK_1K
	},
};

/* KPS */
static struct bus_clock bsc1_apb_clk = {
	.clk	=	{
		.name	=	"bsc1_apb_clk",
		.parent	=	name_to_clk(kps_ccu),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_SLV_CLK_BASE_ADDR,
	.wr_access_offset	=	KPS_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPS_CLK_MGR_REG_BSC1_CLKGATE_OFFSET,

	.stprsts_mask		=	KPS_CLK_MGR_REG_BSC1_CLKGATE_BSC1_APB_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPS_CLK_MGR_REG_BSC1_CLKGATE_BSC1_APB_HW_SW_GATING_SEL_SHIFT,
	.clk_en_mask		=	KPS_CLK_MGR_REG_BSC1_CLKGATE_BSC1_APB_CLK_EN_MASK,

	.freq_tbl	=	{
		 26*CLOCK_1M,  26*CLOCK_1M,  39*CLOCK_1M,  52*CLOCK_1M,
		 52*CLOCK_1M,  78*CLOCK_1M
	},

};

static struct bus_clock bsc2_apb_clk = {
	.clk	=	{
		.name	=	"bsc2_apb_clk",
		.parent	=	name_to_clk(kps_ccu),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_SLV_CLK_BASE_ADDR,
	.wr_access_offset	=	KPS_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPS_CLK_MGR_REG_BSC2_CLKGATE_OFFSET,

	.stprsts_mask		=	KPS_CLK_MGR_REG_BSC2_CLKGATE_BSC2_APB_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPS_CLK_MGR_REG_BSC2_CLKGATE_BSC2_APB_HW_SW_GATING_SEL_SHIFT,
	.clk_en_mask		=	KPS_CLK_MGR_REG_BSC2_CLKGATE_BSC2_APB_CLK_EN_MASK,

	.freq_tbl	=	{
		 26*CLOCK_1M,  26*CLOCK_1M,  39*CLOCK_1M,  52*CLOCK_1M,
		 52*CLOCK_1M,  78*CLOCK_1M
	},

};

/* MM */
static struct bus_clock smi_axi_clk = {
	.clk	=	{
		.name	=	"smi_axi_clk",
		.parent	=	name_to_clk(mm_ccu),
		.ops	=	&bus_clk_ops,
	},

	.ccu_clk_mgr_base	=	MM_CLK_BASE_ADDR,
	.wr_access_offset	=	MM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	MM_CLK_MGR_REG_SMI_AXI_CLKGATE_OFFSET,

	.stprsts_mask		=	MM_CLK_MGR_REG_SMI_AXI_CLKGATE_SMI_AXI_STPRSTS_MASK,
	.hw_sw_gating_mask	=	MM_CLK_MGR_REG_SMI_AXI_CLKGATE_SMI_AXI_HW_SW_GATING_SEL_SHIFT,
	.clk_en_mask		=	MM_CLK_MGR_REG_SMI_AXI_CLKGATE_SMI_AXI_CLK_EN_MASK,

	.freq_tbl	=	{
		 26*CLOCK_1M,  49920*CLOCK_1K,	83200*CLOCK_1K, 99840*CLOCK_1K,
		166400*CLOCK_1K, 249600*CLOCK_1K
	},
};

/* peri clocks */
static struct clk *sdio_clk_src_tbl[] =
{
	name_to_clk(crystal),
	name_to_clk(var_52m),
	name_to_clk(ref_52m),
	name_to_clk(var_96m),
	name_to_clk(ref_96m),
};

static struct clk_src sdio1_clk_src = {
	.total		=	ARRAY_SIZE(sdio_clk_src_tbl),
	.sel		=	2,
	.parents	=	sdio_clk_src_tbl,
};

static struct clk_src sdio2_clk_src = {
	.total		=	ARRAY_SIZE(sdio_clk_src_tbl),
	.sel		=	2,
	.parents	=	sdio_clk_src_tbl,
};

static struct clk_src sdio3_clk_src = {
	.total		=	ARRAY_SIZE(sdio_clk_src_tbl),
	.sel		=	2,
	.parents	=	sdio_clk_src_tbl,
};

static struct clk_src sdio4_clk_src = {
	.total		=	ARRAY_SIZE(sdio_clk_src_tbl),
	.sel		=	2,
	.parents	=	sdio_clk_src_tbl,
};

static struct peri_clock sdio1_clk = {
	.clk	=	{
		.name	=	"sdio1_clk",
		.parent	=	name_to_clk(ref_52m),
		.rate	=	26*CLOCK_1M,
		.div	=	2,
		.id	=	-1,

		.src	= 	&sdio1_clk_src,
		.ops	=	&peri_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_SDIO1_CLKGATE_OFFSET,
	.div_offset		=	KPM_CLK_MGR_REG_SDIO1_DIV_OFFSET,
	.div_trig_offset	=	KPM_CLK_MGR_REG_DIV_TRIG_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO1_CLKGATE_SDIO1_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPM_CLK_MGR_REG_SDIO1_CLKGATE_SDIO1_HW_SW_GATING_SEL_MASK,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO1_CLKGATE_SDIO1_CLK_EN_MASK,
	.div_mask		=	KPM_CLK_MGR_REG_SDIO1_DIV_SDIO1_DIV_MASK,
	.div_shift		=	KPM_CLK_MGR_REG_SDIO1_DIV_SDIO1_DIV_SHIFT,
	.pll_select_mask	=	KPM_CLK_MGR_REG_SDIO1_DIV_SDIO1_PLL_SELECT_MASK,
	.pll_select_shift	=	KPM_CLK_MGR_REG_SDIO1_DIV_SDIO1_PLL_SELECT_SHIFT,
	.trigger_mask		=	KPM_CLK_MGR_REG_DIV_TRIG_SDIO1_TRIGGER_MASK,
};

static struct peri_clock sdio2_clk = {
	.clk	=	{
		.name	=	"sdio2_clk",
		.parent	=	name_to_clk(ref_52m),
		.rate	=	26*CLOCK_1M,
		.div	=	2,
		.id	=	-1,

		.src	= 	&sdio2_clk_src,
		.ops	=	&peri_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_SDIO2_CLKGATE_OFFSET,
	.div_offset		=	KPM_CLK_MGR_REG_SDIO2_DIV_OFFSET,
	.div_trig_offset	=	KPM_CLK_MGR_REG_DIV_TRIG_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO2_CLKGATE_SDIO2_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPM_CLK_MGR_REG_SDIO2_CLKGATE_SDIO2_HW_SW_GATING_SEL_MASK,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO2_CLKGATE_SDIO2_CLK_EN_MASK,
	.div_mask		=	KPM_CLK_MGR_REG_SDIO2_DIV_SDIO2_DIV_MASK,
	.div_shift		=	KPM_CLK_MGR_REG_SDIO2_DIV_SDIO2_DIV_SHIFT,
	.pll_select_mask	=	KPM_CLK_MGR_REG_SDIO2_DIV_SDIO2_PLL_SELECT_MASK,
	.pll_select_shift	=	KPM_CLK_MGR_REG_SDIO2_DIV_SDIO2_PLL_SELECT_SHIFT,
	.trigger_mask		=	KPM_CLK_MGR_REG_DIV_TRIG_SDIO2_TRIGGER_MASK,
};

static struct peri_clock sdio3_clk = {
	.clk	=	{
		.name	=	"sdio3_clk",
		.parent	=	name_to_clk(ref_52m),
		.rate	=	26*CLOCK_1M,
		.div	=	2,
		.id	=	-1,

		.src	= 	&sdio3_clk_src,
		.ops	=	&peri_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPM_CLK_MGR_REG_SDIO3_CLKGATE_OFFSET,
	.div_offset		=	KPM_CLK_MGR_REG_SDIO3_DIV_OFFSET,
	.div_trig_offset	=	KPM_CLK_MGR_REG_DIV_TRIG_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO3_CLKGATE_SDIO3_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPM_CLK_MGR_REG_SDIO3_CLKGATE_SDIO3_HW_SW_GATING_SEL_MASK,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO3_CLKGATE_SDIO3_CLK_EN_MASK,
	.div_mask		=	KPM_CLK_MGR_REG_SDIO3_DIV_SDIO3_DIV_MASK,
	.div_shift		=	KPM_CLK_MGR_REG_SDIO3_DIV_SDIO3_DIV_SHIFT,
	.pll_select_mask	=	KPM_CLK_MGR_REG_SDIO3_DIV_SDIO3_PLL_SELECT_MASK,
	.pll_select_shift	=	KPM_CLK_MGR_REG_SDIO3_DIV_SDIO3_PLL_SELECT_SHIFT,
	.trigger_mask		=	KPM_CLK_MGR_REG_DIV_TRIG_SDIO3_TRIGGER_MASK,
};

static struct peri_clock sdio4_clk = {
	.clk	=	{
		.name	=	"sdio4_clk",
		.parent	=	name_to_clk(ref_52m),
		.rate	=	26*CLOCK_1M,
		.div	=	2,
		.id	=	-1,

		.src	= 	&sdio4_clk_src,
		.ops	=	&peri_clk_ops,
	},
	.ccu_clk_mgr_base	=	KONA_MST_CLK_BASE_ADDR,
	.wr_access_offset	=	KPM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset 	=	KPM_CLK_MGR_REG_SDIO4_CLKGATE_OFFSET,
	.div_offset		=	KPM_CLK_MGR_REG_SDIO4_DIV_OFFSET,
	.div_trig_offset	=	KPM_CLK_MGR_REG_DIV_TRIG_OFFSET,

	.stprsts_mask		=	KPM_CLK_MGR_REG_SDIO4_CLKGATE_SDIO4_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPM_CLK_MGR_REG_SDIO4_CLKGATE_SDIO4_HW_SW_GATING_SEL_MASK,
	.clk_en_mask		=	KPM_CLK_MGR_REG_SDIO4_CLKGATE_SDIO4_CLK_EN_MASK,
	.div_mask		=	KPM_CLK_MGR_REG_SDIO4_DIV_SDIO4_DIV_MASK,
	.div_shift		=	KPM_CLK_MGR_REG_SDIO4_DIV_SDIO4_DIV_SHIFT,
	.pll_select_mask	=	KPM_CLK_MGR_REG_SDIO4_DIV_SDIO4_PLL_SELECT_MASK,
	.pll_select_shift	=	KPM_CLK_MGR_REG_SDIO4_DIV_SDIO4_PLL_SELECT_SHIFT,
	.trigger_mask		=	KPM_CLK_MGR_REG_DIV_TRIG_SDIO4_TRIGGER_MASK,
};

static struct clk *bsc_clk_src_tbl[] =
{
	name_to_clk(crystal),
	name_to_clk(var_104m),
	name_to_clk(ref_104m),
	name_to_clk(var_13m),
	name_to_clk(ref_13m),
};

static struct clk_src bsc1_clk_src = {
	.total		=	ARRAY_SIZE(bsc_clk_src_tbl),
	.sel		=	3,
	.parents	=	bsc_clk_src_tbl,
};

static struct clk_src bsc2_clk_src = {
	.total		=	ARRAY_SIZE(bsc_clk_src_tbl),
	.sel		=	3,
	.parents	=	bsc_clk_src_tbl,
};

static struct peri_clock bsc1_clk = {
	.clk	=	{
		.name	=	"bsc1_clk",
		.parent	=	name_to_clk(ref_13m),
		.rate	=	13*CLOCK_1M,
		.div	=	1,
		.id	=	-1,

		.src	= 	&bsc1_clk_src,
		.ops	=	&peri_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_SLV_CLK_BASE_ADDR,
	.wr_access_offset	=	KPS_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPS_CLK_MGR_REG_BSC1_CLKGATE_OFFSET,
	.div_offset		=	KPS_CLK_MGR_REG_BSC1_DIV_OFFSET,
	.div_trig_offset	=	KPS_CLK_MGR_REG_DIV_TRIG_OFFSET,

	.stprsts_mask		=	KPS_CLK_MGR_REG_BSC1_CLKGATE_BSC1_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPS_CLK_MGR_REG_BSC1_CLKGATE_BSC1_HW_SW_GATING_SEL_MASK,
	.clk_en_mask		=	KPS_CLK_MGR_REG_BSC1_CLKGATE_BSC1_CLK_EN_MASK,
	.pll_select_mask	=	KPS_CLK_MGR_REG_BSC1_DIV_BSC1_PLL_SELECT_MASK,
	.pll_select_shift	=	KPS_CLK_MGR_REG_BSC1_DIV_BSC1_PLL_SELECT_SHIFT,
	.trigger_mask		=	KPS_CLK_MGR_REG_DIV_TRIG_BSC1_TRIGGER_MASK,
};

static struct peri_clock bsc2_clk = {
	.clk	=	{
		.name	=	"bsc2_clk",
		.parent	=	name_to_clk(ref_13m),
		.rate	=	13*CLOCK_1M,
		.div	=	1,
		.id	=	-1,

		.src	= 	&bsc2_clk_src,
		.ops	=	&peri_clk_ops,
	},

	.ccu_clk_mgr_base	=	KONA_SLV_CLK_BASE_ADDR,
	.wr_access_offset	=	KPS_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	KPS_CLK_MGR_REG_BSC2_CLKGATE_OFFSET,
	.div_offset		=	KPS_CLK_MGR_REG_BSC2_DIV_OFFSET,
	.div_trig_offset	=	KPS_CLK_MGR_REG_DIV_TRIG_OFFSET,

	.stprsts_mask		=	KPS_CLK_MGR_REG_BSC2_CLKGATE_BSC2_STPRSTS_MASK,
	.hw_sw_gating_mask	=	KPS_CLK_MGR_REG_BSC2_CLKGATE_BSC2_HW_SW_GATING_SEL_MASK,
	.clk_en_mask		=	KPS_CLK_MGR_REG_BSC2_CLKGATE_BSC2_CLK_EN_MASK,
	.pll_select_mask	=	KPS_CLK_MGR_REG_BSC2_DIV_BSC2_PLL_SELECT_MASK,
	.pll_select_shift	=	KPS_CLK_MGR_REG_BSC2_DIV_BSC2_PLL_SELECT_SHIFT,
	.trigger_mask		=	KPS_CLK_MGR_REG_DIV_TRIG_BSC2_TRIGGER_MASK,
};

static struct clk *smi_clk_src_tbl[] =
{
	name_to_clk(var_500m),
	name_to_clk(var_312m),
};

static struct clk_src smi_clk_src = {
	.total		=	ARRAY_SIZE(smi_clk_src_tbl),
	.sel		=	0,
	.parents	=	smi_clk_src_tbl,
};


static struct peri_clock smi_clk = {
	.clk	=	{
		.name	=	"smi_clk",
		.parent	=	name_to_clk(var_500m),
		.rate	=	500*CLOCK_1M,
		.div	=	1,
		.id	=	-1,
		.src	= 	&smi_clk_src,
		.ops	=	&peri_clk_ops,
	},

	.ccu_clk_mgr_base	=	MM_CLK_BASE_ADDR,
	.wr_access_offset	=	MM_CLK_MGR_REG_WR_ACCESS_OFFSET,
	.clkgate_offset		=	MM_CLK_MGR_REG_SMI_CLKGATE_OFFSET,
	.div_offset		=	MM_CLK_MGR_REG_SMI_DIV_OFFSET,
	.div_trig_offset	=	MM_CLK_MGR_REG_DIV_TRIG_OFFSET,

	.stprsts_mask		=	MM_CLK_MGR_REG_SMI_CLKGATE_SMI_STPRSTS_MASK,
	.hw_sw_gating_mask	=	MM_CLK_MGR_REG_SMI_CLKGATE_SMI_HW_SW_GATING_SEL_MASK,
	.clk_en_mask		=	MM_CLK_MGR_REG_SMI_CLKGATE_SMI_CLK_EN_MASK,
	.div_mask		=	MM_CLK_MGR_REG_SMI_DIV_SMI_DIV_MASK,
	.div_shift		=	MM_CLK_MGR_REG_SMI_DIV_SMI_DIV_SHIFT,
	.div_dithering		=	1,
	.pll_select_mask	=	MM_CLK_MGR_REG_SMI_DIV_SMI_PLL_SELECT_MASK,
	.pll_select_shift	=	MM_CLK_MGR_REG_SMI_DIV_SMI_PLL_SELECT_SHIFT,
	.trigger_mask		=	MM_CLK_MGR_REG_DIV_TRIG_SMI_TRIGGER_MASK,
};

/* table for registering clock */
static struct __init clk_lookup rhea_clk_tbl[] =
{
	CLK_LK(arm),

	CLK_LK(crystal),
	CLK_LK(frac_1m),
	CLK_LK(ref_96m_varVDD),
	CLK_LK(var_96m),
	CLK_LK(ref_96m),
	CLK_LK(var_500m),
	CLK_LK(var_500m_varVDD),

	CLK_LK(ref_32k),
	CLK_LK(misc_32k),

	CLK_LK(ref_312m),
	CLK_LK(ref_208m),
	CLK_LK(ref_104m),
	CLK_LK(ref_52m),
	CLK_LK(ref_13m),

	CLK_LK(var_312m),
	CLK_LK(var_208m),
	CLK_LK(var_156m),
	CLK_LK(var_52m),
	CLK_LK(var_13m),

	CLK_LK(usbh_48m),
	CLK_LK(ref_cx40),

	CLK_LK(sdio1),
	CLK_LK(sdio2),
	CLK_LK(sdio3),
	CLK_LK(sdio4),

	CLK_LK(bsc1),
	CLK_LK(bsc2),

	CLK_LK(smi),

	CLK_LK(kpm_ccu),
	CLK_LK(kps_ccu),
	CLK_LK(mm_ccu),

	CLK_LK(usb_otg),
	CLK_LK(sdio1_ahb),
	CLK_LK(sdio2_ahb),
	CLK_LK(sdio3_ahb),
	CLK_LK(sdio4_ahb),
	CLK_LK(sdio1_sleep),
	CLK_LK(sdio2_sleep),
	CLK_LK(sdio3_sleep),
	CLK_LK(sdio4_sleep),
	CLK_LK(bsc1_apb),
	CLK_LK(bsc2_apb),

	CLK_LK(smi_axi),
};

int __init clock_init(void)
{
	int i;
	for (i=0; i<ARRAY_SIZE(rhea_clk_tbl); i++)
		clkdev_add (&rhea_clk_tbl[i]);

	return 0;
}

int __init clock_late_init(void)
{
#ifdef CONFIG_DEBUG_FS
	int i;
	clock_debug_init();
	for (i=0; i<ARRAY_SIZE(rhea_clk_tbl); i++)
		clock_debug_add_clock (rhea_clk_tbl[i].clk);
#endif
	return 0;
}

late_initcall(clock_late_init);

unsigned long clock_get_xtal(void)
{
	return 26*CLOCK_1M;
}
