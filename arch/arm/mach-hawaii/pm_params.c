/*****************************************************************************
* Copyright 2012 Broadcom Corporation.  All rights reserved.
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

#include <linux/version.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/kernel_stat.h>
#include <asm/mach/arch.h>
#include <linux/io.h>
#include<plat/pi_mgr.h>
#include<mach/pi_mgr.h>
#include<mach/pwr_mgr.h>
#include<plat/pwr_mgr.h>
#include <mach/cpu.h>
#include <mach/clock.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include "pm_params.h"
#include "sequencer_ucode.h"
#include <plat/kona_avs.h>

/*sysfs interface to read PMU vlt table*/
static u32 sr_vlt_table[SR_VLT_LUT_SIZE];
module_param_array_named(sr_vlt_table, sr_vlt_table, uint, NULL, S_IRUGO);

static unsigned long pm_erratum_flg;
module_param_named(pm_erratum_flg, pm_erratum_flg, ulong,
						S_IRUGO|S_IWUSR|S_IWGRP);


#ifdef CONFIG_KONA_POWER_MGR
extern struct i2c_cmd *i2c_cmd_buf;
extern u32 cmd_buf_sz;

/**
 * VO0 : HUB + MM + Modem Domain
 */
static struct v0x_spec_i2c_cmd_ptr v0_ptr = {
	.other_ptr = VO0_HW_SEQ_START_OFF,
	.set2_val = VLT_ID_RETN,	/*Retention voltage inx */
	.set2_ptr = VO0_SET2_OFFSET,
	.set1_val = VLT_ID_WAKEUP,	/*wakeup from retention voltage inx */
	.set1_ptr = VO0_SET1_OFFSET,
	.zerov_ptr = VO0_SET2_OFFSET,	/* NO OFF State for  VO0 */
};

/**
 * VO1 : A9 domain
 */
static struct v0x_spec_i2c_cmd_ptr v1_ptr = {
	.other_ptr = VO1_HW_SEQ_START_OFF,
	.set2_val = VLT_ID_RETN,		/*Retention voltage inx */
	.set2_ptr = VO1_SET2_OFFSET,
	.set1_val = VLT_ID_WAKEUP,	/*wakeup from retention voltage inx */
	.set1_ptr = VO1_SET1_OFFSET,
	.zerov_ptr = VO1_ZERO_PTR_OFFSET,
};

struct pwrmgr_init_param pwrmgr_init_param = {
	.v0xptr = {
		&v0_ptr,
		&v1_ptr,
	},
	.i2c_rd_off = SW_SEQ_RD_START_OFF,
	.i2c_rd_slv_id_off1 = SW_SEQ_RD_SLAVE_ID_1_OFF,
	.i2c_rd_reg_addr_off = SW_SEQ_RD_REG_ADDR_OFF,
	.i2c_rd_slv_id_off2 = SW_SEQ_RD_SLAVE_ID_2_OFF,
	.i2c_wr_off = SW_SEQ_WR_START_OFF,
	.i2c_wr_slv_id_off = SW_SEQ_WR_SLAVE_ID_OFF,
	.i2c_wr_reg_addr_off = SW_SEQ_WR_REG_ADDR_OFF,
	.i2c_wr_val_addr_off = SW_SEQ_WR_VALUE_OFF,
	.i2c_seq_timeout = 100,
#ifdef CONFIG_KONA_PWRMGR_SWSEQ_FAKE_TRG_ERRATUM
	.pc_toggle_off = FAKE_TRG_ERRATUM_PC_PIN_TOGGLE_OFF,
#endif
};

#endif /*CONFIG_KONA_POWER_MGR */

static void __init __pm_init_errata_flg(void)
{
	u32 chip_id = get_chip_id();

#ifdef CONFIG_MM_V3D_TIMEOUT_ERRATUM
	if (chip_id <= HAWAII_CHIP_ID(HAWAII_CHIP_REV_A0))
		pm_erratum_flg |= ERRATUM_MM_V3D_TIMEOUT;
#endif

#ifdef CONFIG_PLL1_8PHASE_OFF_ERRATUM
	if (chip_id <= HAWAII_CHIP_ID(HAWAII_CHIP_REV_A0))
		pm_erratum_flg |= ERRATUM_PLL1_8PHASE_OFF;
#endif

#ifdef CONFIG_MM_POWER_OK_ERRATUM
	if (chip_id <= HAWAII_CHIP_ID(HAWAII_CHIP_REV_A0))
		pm_erratum_flg |= ERRATUM_MM_POWER_OK;
#endif

#ifdef CONFIG_MM_FREEZE_VAR500M_ERRATUM
	if (chip_id <= HAWAII_CHIP_ID(HAWAII_CHIP_REV_A0))
		pm_erratum_flg |= ERRATUM_MM_FREEZE_VAR500M;
#endif

}

#define MHZ(x) ((x)*1000*1000)
#define GHZ(x) (MHZ(x)*1000)

bool is_pm_erratum(u32 erratum)
{
	return !!(pm_erratum_flg & erratum);
}


#define CSR_RETN_VAL_SS			0x4
#define CSR_ECO_VAL_SS			0xF
#define CSR_NM1_VAL_SS			0x13
#define CSR_NM2_VAL_SS			0x1D
#define CSR_TURBO_VAL_SS		0x34

#define MSR_RETN_VAL_SS			0x4
#define MSR_ECO_VAL_SS			0x10
#define MSR_NM1_VAL_SS			0x10
#define MSR_NM2_VAL_SS			0x1A
#define MSR_TURBO_VAL_SS		0x24


const u8 swr_vlt_table[SR_VLT_LUT_SIZE] = {
	INIT_LPM_VLT_IDS(MSR_RETN_VAL_SS, MSR_RETN_VAL_SS, MSR_RETN_VAL_SS),
	INIT_A9_VLT_TABLE(CSR_ECO_VAL_SS, CSR_NM1_VAL_SS, CSR_NM2_VAL_SS,
							CSR_TURBO_VAL_SS),
	INIT_OTHER_VLT_TABLE(MSR_ECO_VAL_SS, MSR_NM1_VAL_SS, MSR_NM2_VAL_SS,
							MSR_TURBO_VAL_SS),
	INIT_UNUSED_VLT_IDS(MSR_RETN_VAL_SS)
	};

__weak const u8 *bcmpmu_get_sr_vlt_table (u32 silicon_type)
{
	return swr_vlt_table;
}

int pm_init_pmu_sr_vlt_map_table(u32 silicon_type)
{
	#if !defined (CONFIG_MACH_HAWAII_FPGA)
	int inx;
	u8 *vlt_table;
	vlt_table = (u8 *) bcmpmu_get_sr_vlt_table(SILICON_TYPE_SLOW);
	for (inx = 0; inx < SR_VLT_LUT_SIZE; inx++)
		sr_vlt_table[inx] = vlt_table[inx];
	return pwr_mgr_pm_i2c_var_data_write(vlt_table, SR_VLT_LUT_SIZE);
	#else
		return 0;
	#endif	
}

#if !defined (CONFIG_MACH_HAWAII_FPGA)
int __init pm_params_init(void)
{
	__pm_init_errata_flg();
	pwrmgr_init_param.cmd_buf = i2c_cmd_buf;
	pwrmgr_init_param.cmd_buf_size = cmd_buf_sz;
	#ifndef CONFIG_KONA_AVS
		pm_init_pmu_sr_vlt_map_table(SILICON_TYPE_SLOW);
	#endif
	return 0;	
}
#endif
