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

#include <mach/pwr_mgr.h>
#include <plat/pwr_mgr.h>
#include "pm_params.h"
#include "sequencer_ucode.h"

#ifdef CONFIG_KONA_POWER_MGR

#ifdef CONFIG_KONA_PMU_BSC_HS_MODE
#define START_CMD			0xb
#define START_DELAY			6
#define WRITE_DELAY			6
#define VLT_CHANGE_DELAY		0x25
#else
#define START_CMD			0x3
#define START_DELAY			0x10
#define WRITE_DELAY			0x80
#define VLT_CHANGE_DELAY		0x80
#endif /*CONFIG_KONA_PMU_BSC_HS_MODE */
#define PMU_SLAVE_ID			0x8
#define PMU_CSR_REG_ADDR		0xC0
#define PMU_MSR_REG_ADDR		0xC9
#define READ_DELAY			0x20

/**
 * PMU BSC Registers and masks used
 * by the the sequencer code
 */
#define PMU_BSC_INT_STATUS_REG		(0x48)
#define PMU_BSC_INT_STATUS_MASK		(0xFF)
#define PMU_BSC_PADCTL_REG		(0x5C)
#define BSC_PAD_OUT_PULLUP_EN		(0x1<<3)
#define BSC_PAD_OUT_EN			(0x0)
#define BSC_PAD_OUT_DIS			(0x1<<2)

#define SET_PC_PIN_CMD(pc_pin)			\
	(SET_PC_PIN_CMD_##pc_pin##_PIN_VALUE_MASK|\
	 SET_PC_PIN_CMD_##pc_pin##_PIN_OVERRIDE_MASK)

#define CLEAR_PC_PIN_CMD(pc_pin)			\
	 SET_PC_PIN_CMD_##pc_pin##_PIN_OVERRIDE_MASK

#define PC_PIN_DEFAULT_STATE		SET_PC_PIN_CMD(PC3)

struct i2c_cmd i2c_cmd[] = {
	{REG_ADDR, 0},		/* 0: NOP */
	{JUMP_VOLTAGE, 0},	/* Jump based upon the voltage */
	{REG_ADDR, PMU_BSC_PADCTL_REG},	/* 2: i2c Read - SW_SEQ_RD_START_OFF */
	{REG_DATA, BSC_PAD_OUT_EN},	/* Enable pad output */
	{REG_ADDR, 0x20},	/* Set BSC CS Register */
	{REG_DATA, START_CMD},	/* Send Start command */
	{WAIT_TIMER, START_DELAY},	/* Wait ... */
	{REG_DATA, 1},		/* Clear Start condition */
	{I2C_DATA, 0x10},	/* 8: SW_SEQ_RD_SLAVE_ID_1_OFF */
	{WAIT_TIMER, WRITE_DELAY},	/* Wait ... */
	{I2C_DATA, 0x00},	/* 10: SW_SEQ_RD_REG_ADDR_OFF */
	{WAIT_TIMER, WRITE_DELAY}, /* Wait ... */
	{REG_ADDR, 0x20},	/* Set BSC CS Addr */
	{REG_DATA, START_CMD},	/* Send Restart */
	{WAIT_TIMER, START_DELAY},	/* Wait ... */
	{REG_DATA, 1},		/* Clear Start */
	{I2C_DATA, 0x11},	/* 16: SW_SEQ_RD_SLAVE_ID_2_OFF */
	{WAIT_TIMER, WRITE_DELAY},	/* Wait ... */
	{REG_ADDR, 0x20},	/* Set BSC CS Addr */
	{REG_DATA, 0xF},	/* Read Cmd */
	{WAIT_TIMER, READ_DELAY},	/* Wait ... */
	{REG_DATA, 1},		/* Clear Start condition */
	{READ_FIFO, 0},	/* Read bsc DATA register */
	{REG_ADDR, RDWR_CLR_BSC_ISR_JUMP_OFF},
	{JUMP, RDWR_CLR_BSC_ISR_JUMP_OFF},
	{REG_ADDR, PMU_BSC_PADCTL_REG},	/* 25: i2c Write- SW_SEQ_WR_START_OFF*/
	{REG_DATA, BSC_PAD_OUT_EN},	/* Enable pad output */
	{REG_ADDR, 0x20},	/* Set BSC CS Reg */
	{REG_DATA, START_CMD},	/* Send Start condition */
	{WAIT_TIMER, START_DELAY},	/* Wait ... */
	{REG_DATA, 1},		/* Clear Start condition */
	{I2C_DATA, 0x10},	/* 31: SW_SEQ_WR_SLAVE_ID_OFF */
	{WAIT_TIMER, WRITE_DELAY},	/* Wait... */
	{I2C_DATA, 0x00},	/* 33: SW_SEQ_WR_REG_ADDR_OFF */
	{WAIT_TIMER, WRITE_DELAY},	/* Wait ... */
	{I2C_DATA, 0xC0},	/* 35: SW_SEQ_WR_VALUE_OFF */
	{WAIT_TIMER, WRITE_DELAY},	/* fall through */
	{SET_READ_DATA, PMU_BSC_INT_STATUS_REG},/*37:RDWR_CLR_BSC_ISR_JUMP_OFF*/
	{REG_ADDR, PMU_BSC_INT_STATUS_REG},	/* Set BSC INT Reg */
	{REG_DATA, PMU_BSC_INT_STATUS_MASK},	/* Clear INT Status */
	{REG_ADDR, PMU_BSC_PADCTL_REG},	/* Set BSC PADCTL Reg */
	{REG_DATA, BSC_PAD_OUT_DIS},	/* Disable pad output */
#ifdef CONFIG_KONA_PWRMGR_SWSEQ_FAKE_TRG_ERRATUM
	{SET_PC_PINS, PC_PIN_DEFAULT_STATE},/*42:FAKE_TRG_ERRATUM_PC_PIN_TOGGLE_OFF */
#else
	{REG_ADDR, 0},		/* 42: nop */
#endif
	{END, 0},		/* 43 : End sequence (write/read) */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* NOP */
	{REG_ADDR, 0}, /* 63: NOP */
	{REG_ADDR, PMU_BSC_PADCTL_REG},	/* 64: VO0_HW_SEQ_START_OFF */
	{REG_DATA, BSC_PAD_OUT_EN},	/* Enable pad output */
	{REG_ADDR, 0x20},	/* Set BSC CS address */
	{REG_DATA, START_CMD},	/* Start condition */
	{WAIT_TIMER, START_DELAY},	/* Wait */
	{REG_DATA, 1},		/* Clear Start Condition */
	{I2C_DATA, (PMU_SLAVE_ID << 1)},	/* Send Slave Address */
	{WAIT_TIMER, WRITE_DELAY},	/* Wait ... */
	{I2C_DATA, PMU_MSR_REG_ADDR},	/* Send CSR Reg address */
	{WAIT_TIMER, WRITE_DELAY},	/*  Wait */
	{I2C_VAR, 0},		/* 74: Write Variable voltage */
	{WAIT_TIMER, VLT_CHANGE_DELAY},	/* Wait for voltage change */
	{REG_ADDR, PMU_BSC_PADCTL_REG},	/* Set BSC PADCTL Register */
	{REG_DATA, BSC_PAD_OUT_DIS},	/* Disable pad outpout */
	{END, 0},		/*  78: -- VO0 -- End Sequence */
	{REG_ADDR, PMU_BSC_PADCTL_REG},/* 79:VO1 SEQ: VO1_HW_SEQ_START_OFF*/
	{REG_DATA, BSC_PAD_OUT_EN},	/* Enable pad output */
	{REG_ADDR, 0x20},	/* Set BSC CS address */
	{REG_DATA, START_CMD},	/* Start condition */
	{WAIT_TIMER, START_DELAY},	/* Wait */
	{REG_DATA, 1},		/* Clear Start Condition */
	{I2C_DATA, (PMU_SLAVE_ID << 1)},	/* Send Slave Address */
	{WAIT_TIMER, WRITE_DELAY},	/* Wait ... */
	{I2C_DATA, PMU_CSR_REG_ADDR},	/* Send CSR Reg address: fall through */
	{WAIT_TIMER, WRITE_DELAY},	/* Wait */
	{I2C_VAR, 0},		/* 89 : Write Variable voltage */
	{WAIT_TIMER, VLT_CHANGE_DELAY},	/* Wait for voltage change */
	{REG_ADDR, PMU_BSC_PADCTL_REG},	/* Set BSC PADCTL Register */
	{REG_DATA, BSC_PAD_OUT_DIS},	/* Disable pad outpout */
	{END, 0},		/*  93: -- VO1 -- End Sequence */
	{SET_PC_PINS, CLEAR_PC_PIN_CMD(PC1)},/* 94:set PC low-VO0_SET2_OFFSET*/
	{END, 0},		/* End */
	{SET_PC_PINS, SET_PC_PIN_CMD(PC1)},/* 96: set PC high-VO0_SET1_OFFSET*/
	{END, 0},		/* END */
	{SET_PC_PINS, CLEAR_PC_PIN_CMD(PC3)},/* 98:set PC low-VO1_SET2_OFFSET*/
	{END, 0},		/* End sequence (SET2) */
	{SET_PC_PINS, SET_PC_PIN_CMD(PC3)},/* 100:set PC high-VO1_SET1_OFFSET*/
	{REG_ADDR, VO1_HW_SEQ_START_OFF},
	{JUMP, VO1_HW_SEQ_START_OFF},
	{END, 0},		/* 103: End sequence */
};

struct i2c_cmd *i2c_cmd_buf = i2c_cmd;
int cmd_buf_sz = ARRAY_SIZE(i2c_cmd);
#endif /* CONFIG_KONA_PWRMGR */

