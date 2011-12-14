/*****************************************************************************
*  Copyright 2001 - 2008 Broadcom Corporation.  All rights reserved.
*
*  Unless you and Broadcom execute a separate written software license
*  agreement governing use of this software, this software is licensed to you
*  under the terms of the GNU General Public License version 2, available at
*  http://www.gnu.org/licenses/old-license/gpl-2.0.html (the "GPL").
*
*  Notwithstanding the above, under no circumstances may you combine this
*  software in any way with any other Broadcom software provided under a
*  license other than the GPL, without Broadcom's express prior written
*  consent.
*
*****************************************************************************/

#ifndef __LINUX_MFD_BCMPMU_H_
#define __LINUX_MFD_BCMPMU_H_

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/i2c-kona.h>

#define	PMU_BITMASK_ALL		0xFFFFFFFF

struct bcmpmu;
struct regulator_init_data;

/* LDO or Switcher def */
#define BCMPMU_LDO    0x10
#define BCMPMU_SR     0x11
/* HOSTCTRL1 def*/
#define BCMPMU_SW_SHDWN 0x04


int bcmpmu_register_regulator(struct bcmpmu *bcmpmu, int reg,
			      struct regulator_init_data *initdata);

struct bcmpmu_reg_info 
{
	u8  reg_addr;		/* address of regulator control register for mode control */
	u8  reg_addr_volt;	/* address of control register to change voltage */
	u8  reg_addr_volt_l;
	u8  reg_addr_volt_t;
	u32 en_dis_mask;	/* Mask for enable/disable bits */
	u32 en_dis_shift;	/* Shift for enable/disalbe bits */
	u32 vout_mask;		/* Mask of bits in register */
	u32 vout_shift;		/* Bit shift in register */
	u32 vout_mask_l;	/* Mask of bits in register */
	u32 vout_shift_l;	/* Bit shift in register */
	u32 vout_mask_t;	/* Mask of bits in register */
	u32 vout_shift_t;	/* Bit shift in register */
	u32 *v_table;		/* Map for converting register voltage to register value */
	u32 num_voltages;	/* Size of register map */
	u32 mode_mask;
	u8 ldo_or_sr;
};

struct bcmpmu_regulator_init_data
{
	int regulator ; /* Regulator ID */
	struct regulator_init_data   *initdata;
};

enum bcmpmu_rgr_id {
	BCMPMU_REGULATOR_RFLDO,
	BCMPMU_REGULATOR_CAMLDO,
	BCMPMU_REGULATOR_HV1LDO,
	BCMPMU_REGULATOR_HV2LDO,
	BCMPMU_REGULATOR_HV3LDO,
	BCMPMU_REGULATOR_HV4LDO,
	BCMPMU_REGULATOR_HV5LDO,
	BCMPMU_REGULATOR_HV6LDO,
	BCMPMU_REGULATOR_HV7LDO,
	BCMPMU_REGULATOR_HV8LDO,
	BCMPMU_REGULATOR_HV9LDO,
	BCMPMU_REGULATOR_HV10LDO,
	BCMPMU_REGULATOR_SIMLDO,
	BCMPMU_REGULATOR_USBLDO,
	BCMPMU_REGULATOR_BCDLDO,
	BCMPMU_REGULATOR_DVS1LDO,
	BCMPMU_REGULATOR_DVS2LDO,
	BCMPMU_REGULATOR_SIM2LDO,
	BCMPMU_REGULATOR_CSR,
	BCMPMU_REGULATOR_IOSR,
	BCMPMU_REGULATOR_SDSR,
	BCMPMU_REGULATOR_MAX,
};

extern struct regulator_ops bcmpmuldo_ops;

/* struct bcmpmu; */
enum bcmpmu_reg {
	PMU_REG_SMPLCTRL,
	PMU_REG_WRLOCKKEY,
	PMU_REG_WRPROEN,
	PMU_REG_PMUGID,
	PMU_REG_PONKEYCTRL1,
	PMU_REG_PONKEYCTRL2,
	PMU_REG_PONKEYCTRL3,
	PMU_REG_AUXCTRL,
	PMU_REG_RTCSC,
	PMU_REG_RTCMN,
	PMU_REG_RTCHR,
	PMU_REG_RTCDT,
	PMU_REG_RTCMT,
	PMU_REG_RTCYR,
	PMU_REG_RTCSC_ALM,
	PMU_REG_RTCMN_ALM,
	PMU_REG_RTCHR_ALM,
	PMU_REG_RTCWD_ALM,
	PMU_REG_RTCDT_ALM,
	PMU_REG_RTCMT_ALM,
	PMU_REG_RTCYR_ALM,
	PMU_REG_RTC_CORE,
	PMU_REG_RTC_C2C1_XOTRIM,
	PMU_REG_RFOPMODCTRL,
	PMU_REG_CAMOPMODCTRL,
	PMU_REG_HV1OPMODCTRL,
	PMU_REG_HV2OPMODCTRL,
	PMU_REG_HV3OPMODCTRL,
	PMU_REG_HV4OPMODCTRL,
	PMU_REG_HV5OPMODCTRL,
	PMU_REG_HV6OPMODCTRL,
	PMU_REG_HV7OPMODCTRL,
	PMU_REG_HV8OPMODCTRL,
	PMU_REG_HV9OPMODCTRL,
	PMU_REG_HV10OPMODCTRL,
	PMU_REG_SIMOPMODCTRL,
	PMU_REG_CSROPMODCTRL,
	PMU_REG_IOSROPMODCTRL,
	PMU_REG_SDSROPMODCTRL,
	PMU_REG_ASROPMODCTRL,
	PMU_REG_RFLDOCTRL,
	PMU_REG_CAMLDOCTRL,
	PMU_REG_HVLDO1CTRL,
	PMU_REG_HVLDO2CTRL,
	PMU_REG_HVLDO3CTRL,
	PMU_REG_HVLDO4CTRL,
	PMU_REG_HVLDO5CTRL,
	PMU_REG_HVLDO6CTRL,
	PMU_REG_HVLDO7CTRL,
	PMU_REG_HVLDO8CTRL,
	PMU_REG_HVLDO9CTRL,
	PMU_REG_HVLDO10CTRL,
	PMU_REG_SIMLDOCTRL,
	PMU_REG_SIMLDO2PMCTRL,
	PMU_REG_USBLDOPMCTRL,
	PMU_REG_DVSLDO1PMCTRL,
	PMU_REG_DVSLDO2PMCTRL,
	PMU_REG_DVSLDO1VSEL1,
	PMU_REG_DVSLDO1VSEL2,
	PMU_REG_DVSLDO1VSEL3,
	PMU_REG_DVSLDO2VSEL1,
	PMU_REG_DVSLDO2VSEL2,
	PMU_REG_DVSLDO2VSEL3,
	PMU_REG_SIM2OPMODCTRL,
	PMU_REG_SIMLDO2CTRL,
	PMU_REG_USBOPMODCTRL,
	PMU_REG_USBLDOCTRL,
	PMU_REG_BCDLDOCTRL,
	PMU_REG_DVS1OPMODCTRL,
	PMU_REG_DVS2OPMODCTRL,
	PMU_REG_SIMLDOEN,
	PMU_REG_SIMLDO2EN,
	PMU_REG_PWR_GRP_DLY,
	PMU_REG_CSRCTRL1,
	PMU_REG_CSRCTRL2,
	PMU_REG_CSRCTRL3,
	PMU_REG_CSRCTRL4,
	PMU_REG_CSRCTRL5,
	PMU_REG_CSRCTRL6,
	PMU_REG_CSRCTRL7,
	PMU_REG_CSRCTRL8,
	PMU_REG_IOSRCTRL1,
	PMU_REG_IOSRCTRL2,
	PMU_REG_IOSRCTRL3,
	PMU_REG_IOSRCTRL4,
	PMU_REG_IOSRCTRL5,
	PMU_REG_IOSRCTRL6,
	PMU_REG_IOSRCTRL7,
	PMU_REG_IOSRCTRL8,
	PMU_REG_SDSRCTRL1,
	PMU_REG_SDSRCTRL2,
	PMU_REG_SDSRCTRL3,
	PMU_REG_SDSRCTRL4,
	PMU_REG_SDSRCTRL5,
	PMU_REG_SDSRCTRL6,
	PMU_REG_SDSRCTRL7,
	PMU_REG_SDSRCTRL8,
	PMU_REG_ASRCTRL1,
	PMU_REG_ASRCTRL2,
	PMU_REG_ASRCTRL3,
	PMU_REG_ASRCTRL4,
	PMU_REG_ASRCTRL5,
	PMU_REG_ASRCTRL6,
	PMU_REG_ASRCTRL7,
	PMU_REG_ASRCTRL8,
	PMU_REG_ENV1,
	PMU_REG_ENV2,
	PMU_REG_ENV3,
	PMU_REG_ENV4,
	PMU_REG_ENV5,
	PMU_REG_ENV6,
	PMU_REG_ENV7,
	PMU_REG_ENV8,
	PMU_REG_ENV9,
	PMU_REG_IHFTOP_IHF_IDDQ,
	PMU_REG_IHFLDO_PUP,
	PMU_REG_IHFPOP_PUP,
	PMU_REG_IHFPGA2_GAIN,
	PMU_REG_HSPUP1_IDDQ_PWRDWN,
	PMU_REG_HSPUP2_HS_PWRUP,
	PMU_REG_HSPGA1_GAIN,
	PMU_REG_HSPGA2_GAIN,
	/* Charge */
	PMU_REG_CHRGR_USB_EN,
	PMU_REG_CHRGR_WAC_EN,
	PMU_REG_CHRGR_ICC_FC,
	PMU_REG_CHRGR_ICC_QC,
	PMU_REG_CHRGR_VFLOAT,
	PMU_REG_CHRGR_EOC,
	PMU_REG_CHRGR_BCDLDO,
	PMU_REG_CHRGR_BCDLDO_AON,
	/* fuel gauge */
	PMU_REG_FG_ACCM0,
	PMU_REG_FG_ACCM1,
	PMU_REG_FG_ACCM2,
	PMU_REG_FG_ACCM3,
	PMU_REG_FG_CNT0,
	PMU_REG_FG_CNT1,
	PMU_REG_FG_SLEEPCNT0,
	PMU_REG_FG_SLEEPCNT1,
	PMU_REG_FG_HOSTEN,
	PMU_REG_FG_RESET,
	PMU_REG_FG_FRZREAD,
	PMU_REG_FG_FRZSMPL,
	/* usb control */
	PMU_REG_OTG_VBUS_PULSE,
	PMU_REG_OTG_VBUS_BOOST,
	PMU_REG_OTG_VBUS_DISCHRG,
	PMU_REG_OTG_ENABLE,
	PMU_REG_ADP_SENSE,
	PMU_REG_ADP_COMP_DB_TM,
	PMU_REG_ADP_PRB,
	PMU_REG_ADP_CAL_PRB,
	PMU_REG_ADP_PRB_MOD,
	PMU_REG_ADP_PRB_CYC_TIME,
	PMU_REG_ADP_COMP_METHOD,
	PMU_REG_ADP_ENABLE,
	PMU_REG_ADP_PRB_COMP,
	PMU_REG_ADP_PRB_REG_RST,
	PMU_REG_ADP_SNS_COMP,
	PMU_REG_ADP_SNS_AON,
	/* usb status */
	PMU_REG_USB_STATUS_ID_CODE,
	PMU_REG_OTG_STATUS_VBUS,
	PMU_REG_OTG_STATUS_SESS,
	PMU_REG_OTG_STATUS_SESS_END,
	PMU_REG_ADP_STATUS_ATTACH_DET,
	PMU_REG_ADP_STATUS_SNS_DET,
	PMU_REG_ADP_STATUS_RISE_TIMES_LSB,
	PMU_REG_ADP_STATUS_RISE_TIMES_MSB,
	/* BC ctrl n status */
	PMU_REG_BC_DET_EN,
	PMU_REG_BC_SW_RST,
	PMU_REG_BC_OVWR_KEY,            /* BC CTRL register Overwrite permission reg */

	/* interrupt */
	PMU_REG_INT_START,
	PMU_REG_INT_MSK_START,
	/* bc */
	PMU_REG_BC_STATUS_CODE,
	PMU_REG_BC_STATUS_DONE,
	PMU_REG_BC_STATUS_TO,
	PMU_REG_BC_CTRL_DET_RST,
	PMU_REG_BC_CTRL_DET_EN,
	/* generic */
	PMU_REG_SWUP,
	PMU_REG_PMUID,
	PMU_REG_PMUREV,
	PMU_REG_PLLCTRL,
	PMU_REG_HOSTCTRL1,
	PMU_REG_SYS_WDT_CLR,
	PMU_REG_MAX,
};
enum bcmpmu_irq_reg {
	PMU_REG_INT1,
	PMU_REG_INT2,
	PMU_REG_INT3,
	PMU_REG_INT4,
	PMU_REG_INT5,
	PMU_REG_INT6,
	PMU_REG_INT7,
	PMU_REG_INT8,
	PMU_REG_INT9,
	PMU_REG_INT10,
	PMU_REG_INT11,
	PMU_REG_INT12,
	PMU_REG_INT13,
	PMU_REG_INT14,
};

enum bcmpmu_irq {
	PMU_IRQ_RTC_ALARM,
	PMU_IRQ_RTC_SEC,
	PMU_IRQ_RTC_MIN,
	PMU_IRQ_RTCADJ,
	PMU_IRQ_BATINS,
	PMU_IRQ_BATRM,
	PMU_IRQ_GBAT_PLUG_IN,
	PMU_IRQ_SMPL_INT,
	PMU_IRQ_USBINS,
	PMU_IRQ_USBRM,
	PMU_IRQ_USBOV,
	PMU_IRQ_EOC,
	PMU_IRQ_RESUME_VBUS,
	PMU_IRQ_CHG_HW_TTR_EXP,
	PMU_IRQ_CHG_HW_TCH_EXP,
	PMU_IRQ_CHG_SW_TMR_EXP,
	PMU_IRQ_CHGDET_LATCH,
	PMU_IRQ_CHGDET_TO,
	PMU_IRQ_MBTEMPLOW,
	PMU_IRQ_MBTEMPHIGH,
	PMU_IRQ_MBOV,
	PMU_IRQ_MBOV_DIS,
	PMU_IRQ_USBOV_DIS,
	PMU_IRQ_CHGERRDIS,
	PMU_IRQ_VBUS_1V5_R,
	PMU_IRQ_VBUS_4V5_R,
	PMU_IRQ_VBUS_1V5_F,
	PMU_IRQ_VBUS_4V5_F,
	PMU_IRQ_MBWV_R_10S_WAIT,
	PMU_IRQ_BBLOW,
	PMU_IRQ_LOWBAT,
	PMU_IRQ_VERYLOWBAT,
	PMU_IRQ_RTM_DATA_RDY,
	PMU_IRQ_RTM_IN_CON_MEAS,
	PMU_IRQ_RTM_UPPER,
	PMU_IRQ_RTM_IGNORE,
	PMU_IRQ_RTM_OVERRIDDEN,
	PMU_IRQ_AUD_HSAB_SHCKT,
	PMU_IRQ_AUD_IHFD_SHCKT,
	PMU_IRQ_MBC_TF,
	PMU_IRQ_CSROVRI,
	PMU_IRQ_IOSROVRI,
	PMU_IRQ_SDSROVRI,
	PMU_IRQ_ASROVRI,
	PMU_IRQ_UBPD_CHG_F,
	PMU_IRQ_ACD_INS,
	PMU_IRQ_ACD_RM,
	PMU_IRQ_PONKEYB_HOLD,
	PMU_IRQ_PONKEYB_F,
	PMU_IRQ_PONKEYB_R,
	PMU_IRQ_PONKEYB_OFFHOLD,
	PMU_IRQ_PONKEYB_RESTART,
	PMU_IRQ_IDCHG,
	PMU_IRQ_JIG_USB_INS,
	PMU_IRQ_UART_INS,
	PMU_IRQ_ID_INS,
	PMU_IRQ_ID_RM,
	PMU_IRQ_ADP_CHANGE,
	PMU_IRQ_ADP_SNS_END,
	PMU_IRQ_SESSION_END_VLD,
	PMU_IRQ_SESSION_END_INVLD,
	PMU_IRQ_VBUS_OVERCURRENT,
	PMU_IRQ_MAX,
};

enum bcmpmu_adc_sig {
	PMU_ADC_VMBATT,
	PMU_ADC_VBBATT,
	PMU_ADC_VWALL,
	PMU_ADC_VBUS,
	PMU_ADC_ID,
	PMU_ADC_NTC,
	PMU_ADC_BSI,
	PMU_ADC_32KTEMP,
	PMU_ADC_PATEMP,
	PMU_ADC_ALS,
	PMU_ADC_RTM,
	PMU_ADC_FG_CURRSMPL,
	PMU_ADC_FG_RAW,
	PMU_ADC_FG_VMBATT,
	PMU_ADC_BSI_CAL_LO,
	PMU_ADC_BSI_CAL_HI,
	PMU_ADC_NTC_CAL_LO,
	PMU_ADC_NTC_CAL_HI,
	PMU_ADC_MAX,
};

enum bcmpmu_adc_ctrl {
	PMU_ADC_RST_CNT,
	PMU_ADC_RTM_START,
	PMU_ADC_RTM_MASK,
	PMU_ADC_RTM_SEL,
	PMU_ADC_RTM_DLY,
	PMU_ADC_GSM_DBNC,
	PMU_ADC_CTRL_MAX,
};

enum bcmpmu_adc_timing_t {
	PMU_ADC_TM_RTM_TX,
	PMU_ADC_TM_RTM_RX,
	PMU_ADC_TM_RTM_SW,
	PMU_ADC_TM_HK,
	PMU_ADC_TM_MAX,
};

enum bcmpmu_chrgr_fc_curr_t {
	PMU_CHRGR_CURR_50,
	PMU_CHRGR_CURR_100,
	PMU_CHRGR_CURR_150,
	PMU_CHRGR_CURR_200,
	PMU_CHRGR_CURR_250,
	PMU_CHRGR_CURR_300,
	PMU_CHRGR_CURR_350,
	PMU_CHRGR_CURR_400,
	PMU_CHRGR_CURR_450,
	PMU_CHRGR_CURR_500,
	PMU_CHRGR_CURR_550,
	PMU_CHRGR_CURR_600,
	PMU_CHRGR_CURR_650,
	PMU_CHRGR_CURR_700,
	PMU_CHRGR_CURR_750,
	PMU_CHRGR_CURR_800,
	PMU_CHRGR_CURR_850,
	PMU_CHRGR_CURR_900,
	PMU_CHRGR_CURR_950,
	PMU_CHRGR_CURR_1000,
	PMU_CHRGR_CURR_MAX,
};

enum bcmpmu_chrgr_qc_curr_t {
	PMU_CHRGR_QC_CURR_50,
	PMU_CHRGR_QC_CURR_60,
	PMU_CHRGR_QC_CURR_70,
	PMU_CHRGR_QC_CURR_80,
	PMU_CHRGR_QC_CURR_90,
	PMU_CHRGR_QC_CURR_100,
	PMU_CHRGR_QC_CURR_MAX,
};

enum bcmpmu_chrgr_eoc_curr_t {
	PMU_CHRGR_EOC_CURR_50,
	PMU_CHRGR_EOC_CURR_60,
	PMU_CHRGR_EOC_CURR_70,
	PMU_CHRGR_EOC_CURR_80,
	PMU_CHRGR_EOC_CURR_90,
	PMU_CHRGR_EOC_CURR_100,
	PMU_CHRGR_EOC_CURR_110,
	PMU_CHRGR_EOC_CURR_120,
	PMU_CHRGR_EOC_CURR_130,
	PMU_CHRGR_EOC_CURR_140,
	PMU_CHRGR_EOC_CURR_150,
	PMU_CHRGR_EOC_CURR_160,
	PMU_CHRGR_EOC_CURR_170,
	PMU_CHRGR_EOC_CURR_180,
	PMU_CHRGR_EOC_CURR_190,
	PMU_CHRGR_EOC_CURR_200,
	PMU_CHRGR_EOC_CURR_MAX,
};

enum bcmpmu_chrgr_volt_t {
	PMU_CHRGR_VOLT_3600,
	PMU_CHRGR_VOLT_3625,
	PMU_CHRGR_VOLT_3650,
	PMU_CHRGR_VOLT_3675,
	PMU_CHRGR_VOLT_3700,
	PMU_CHRGR_VOLT_3725,
	PMU_CHRGR_VOLT_3750,
	PMU_CHRGR_VOLT_3775,
	PMU_CHRGR_VOLT_3800,
	PMU_CHRGR_VOLT_3825,
	PMU_CHRGR_VOLT_3850,
	PMU_CHRGR_VOLT_3875,
	PMU_CHRGR_VOLT_3900,
	PMU_CHRGR_VOLT_3925,
	PMU_CHRGR_VOLT_3950,
	PMU_CHRGR_VOLT_3975,
	PMU_CHRGR_VOLT_4000,
	PMU_CHRGR_VOLT_4025,
	PMU_CHRGR_VOLT_4050,
	PMU_CHRGR_VOLT_4075,
	PMU_CHRGR_VOLT_4100,
	PMU_CHRGR_VOLT_4125,
	PMU_CHRGR_VOLT_4150,
	PMU_CHRGR_VOLT_4175,
	PMU_CHRGR_VOLT_4200,
	PMU_CHRGR_VOLT_4225,
	PMU_CHRGR_VOLT_4250,
	PMU_CHRGR_VOLT_4275,
	PMU_CHRGR_VOLT_4300,
	PMU_CHRGR_VOLT_4325,
	PMU_CHRGR_VOLT_4350,
	PMU_CHRGR_VOLT_4375,
	PMU_CHRGR_VOLT_MAX,
};

enum bcmpmu_chrgr_type_t {
	PMU_CHRGR_TYPE_NONE,
	PMU_CHRGR_TYPE_SDP,
	PMU_CHRGR_TYPE_CDP,
	PMU_CHRGR_TYPE_DCP,
	PMU_CHRGR_TYPE_TYPE1,
	PMU_CHRGR_TYPE_TYPE2,
	PMU_CHRGR_TYPE_PS2,
	PMU_CHRGR_TYPE_ACA,
	PMU_CHRGR_TYPE_MAX,
};

enum bcmpmu_usb_type_t {
	PMU_USB_TYPE_NONE,
	PMU_USB_TYPE_SDP,
	PMU_USB_TYPE_DCP,
	PMU_USB_TYPE_CDP,
	PMU_USB_TYPE_ACA,
	PMU_USB_TYPE_MAX,
};

enum bcmpmu_usb_adp_mode_t {
	PMU_USB_ADP_MODE_REPEAT,
	PMU_USB_ADP_MODE_CALIBRATE,
	PMU_USB_ADP_MODE_ONESHOT,
};

enum bcmpmu_usb_id_lvl_t {
	PMU_USB_ID_NOT_SUPPORTED,
	PMU_USB_ID_GROUND,
	PMU_USB_ID_RID_A,
	PMU_USB_ID_RID_B,
	PMU_USB_ID_RID_C,
	PMU_USB_ID_FLOAT,
};

struct bcmpmu_rw_data {
	unsigned int map;
	unsigned int addr;
	unsigned int val;
	unsigned int mask;
};

struct bcmpmu_reg_map {
	unsigned int map;
	unsigned int addr;
	unsigned int mask;
	unsigned int ro;
	unsigned int shift;
};

struct bcmpmu_irq_map {
	unsigned int map;
	unsigned int int_addr;
	unsigned int mask_addr;
	unsigned int bit_mask;
};

struct bcmpmu_adc_map {
	unsigned int map;
	unsigned int addr0;
	unsigned int addr1;
	unsigned int dmask;
	unsigned int vmask;
	unsigned int rtmsel;
	unsigned int vrng;
};

struct bcmpmu_adc_ctrl_map {
	unsigned int addr;
	unsigned int mask;
	unsigned int shift;
};

struct bcmpmu_adc_setting {
	unsigned int tx_rx_sel_addr;
	unsigned int tx_delay;
	unsigned int rx_delay;
};

struct bcmpmu_adc_req {
	enum bcmpmu_adc_sig sig;
	enum bcmpmu_adc_timing_t tm;
	unsigned int raw;
	unsigned int cal;
	unsigned int cnv;
	bool ready;
	struct list_head list;
};

struct bcmpmu_temp_map {
	int adc;
	int temp;
};

struct bcmpmu_charge_zone {
	int tl;
	int th;
	int v;
	int fc;
	int qc;
};

struct bcmpmu_adc_cal {
	unsigned int gain;
	unsigned int offset;
};

enum bcmpmu_ioctl {
	PMU_EM_IOCTL_ADC_REQ,
	PMU_EM_IOCTL_ADC_LOAD_CAL,
	PMU_EM_IOCTL_ENV_STATUS,
};

#define PMU_EM_ADC_REQ _IOWR(0, PMU_EM_IOCTL_ADC_REQ, struct bcmpmu_adc_req*)
#define PMU_EM_ADC_LOAD_CAL _IOW(0, PMU_EM_IOCTL_ADC_LOAD_CAL, u8*)
#define PMU_EM_ENV_STATUS _IOR(0, PMU_EM_IOCTL_ENV_STATUS, unsigned long*)

enum bcmpmu_batt_event_t {
	BCMPMU_BATT_EVENT_PRESENT,
	BCMPMU_BATT_EVENT_MBOV,
	BCMPMU_BATT_EVENT_MAX,
};

enum bcmpmu_usb_accy_t {
	USB,
	CHARGER,
	AUDIO_MONO,
	AUDIO_STEREO,
	AUDIO_HEADPHONE,
	UART,
	TTY,
	UART_JIG,
	USB_JIG,
	CARKIT,
};

enum bcmpmu_env_bit_t {
	PMU_ENV_MBWV_DELTA,
	PMU_ENV_CGPD_ENV,
	PMU_ENV_UBPD_ENV,
	PMU_ENV_UBPD_USBDET,
	PMU_ENV_CGPD_PRI,
	PMU_ENV_UBPD_PRI,
	PMU_ENV_WAC_VALID,
	PMU_ENV_USB_VALID,
	PMU_ENV_P_CGPD_CHG,
	PMU_ENV_P_UBPD_CHR,
	PMU_ENV_PORT_DISABLE,
	PMU_ENV_MBPD,
	PMU_ENV_MBOV,
	PMU_ENV_MBMC,
	PMU_ENV_MAX,
};

enum bcmpmu_usb_event_t {
	/* events for usb driver */
	BCMPMU_USB_EVENT_USB_DETECTION,
	BCMPMU_USB_EVENT_IN,
	BCMPMU_USB_EVENT_RM,
	BCMPMU_USB_EVENT_ADP_CHANGE,
	BCMPMU_USB_EVENT_ADP_SENSE_END,
	BCMPMU_USB_EVENT_ADP_CALIBRATION_DONE,
	BCMPMU_USB_EVENT_ID_CHANGE,
	BCMPMU_USB_EVENT_VBUS_VALID,
	BCMPMU_USB_EVENT_VBUS_INVALID,
	BCMPMU_USB_EVENT_SESSION_VALID,
	BCMPMU_USB_EVENT_SESSION_INVALID,
	BCMPMU_USB_EVENT_SESSION_END_INVALID,
	BCMPMU_USB_EVENT_SESSION_END_VALID,
	BCMPMU_USB_EVENT_VBUS_OVERCURRENT,
	BCMPMU_USB_EVENT_RIC_C_TO_FLOAT,
	/* events for battery charging */
	BCMPMU_CHRGR_EVENT_CHGR_DETECTION,
	BCMPMU_CHRGR_EVENT_CHRG_CURR_LMT,
	BCMPMU_EVENT_MAX,
};

enum bcmpmu_usb_ctrl_t {
	BCMPMU_USB_CTRL_CHRG_CURR_LMT,
	BCMPMU_USB_CTRL_VBUS_ON_OFF,
	BCMPMU_USB_CTRL_SET_VBUS_DEB_TIME,
	BCMPMU_USB_CTRL_SRP_VBUS_PULSE,
	BCMPMU_USB_CTRL_DISCHRG_VBUS,
	BCMPMU_USB_CTRL_START_STOP_ADP_SENS_PRB,
	BCMPMU_USB_CTRL_START_STOP_ADP_PRB,
	BCMPMU_USB_CTRL_START_ADP_CAL_PRB,
	BCMPMU_USB_CTRL_SET_ADP_PRB_MOD,
	BCMPMU_USB_CTRL_SET_ADP_PRB_CYC_TIME,
	BCMPMU_USB_CTRL_SET_ADP_COMP_METHOD,
	BCMPMU_USB_CTRL_GET_ADP_CHANGE_STATUS,
	BCMPMU_USB_CTRL_GET_ADP_SENSE_STATUS,
	BCMPMU_USB_CTRL_GET_ADP_PRB_RISE_TIMES,
	BCMPMU_USB_CTRL_GET_VBUS_STATUS,
	BCMPMU_USB_CTRL_GET_SESSION_STATUS,
	BCMPMU_USB_CTRL_GET_SESSION_END_STATUS,
	BCMPMU_USB_CTRL_GET_ID_VALUE,
	BCMPMU_USB_CTRL_SW_UP,
};

enum bcmpmu_bc_t {
	BCMPMU_BC_BB_BC11,
	BCMPMU_BC_BB_BC12,
	BCMPMU_BC_PMU_BC12,
};

#define	PMU_ENV_BITMASK_MBWV_DELTA		1<<0
#define	PMU_ENV_BITMASK_CGPD_ENV		1<<1
#define	PMU_ENV_BITMASK_UBPD_ENV		1<<2
#define	PMU_ENV_BITMASK_UBPD_USBDET		1<<3
#define	PMU_ENV_BITMASK_CGPD_PRI		1<<4
#define	PMU_ENV_BITMASK_UBPD_PRI		1<<5
#define	PMU_ENV_BITMASK_WAC_VALID		1<<6
#define	PMU_ENV_BITMASK_USB_VALID		1<<7
#define	PMU_ENV_BITMASK_P_CGPD_CHG		1<<8
#define	PMU_ENV_BITMASK_P_UBPD_CHR		1<<9
#define	PMU_ENV_BITMASK_PORT_DISABLE		1<<10
#define	PMU_ENV_BITMASK_MBPD			1<<11
#define	PMU_ENV_BITMASK_MBOV			1<<12
#define PMU_ENV_BITMASK_MBMC			1<<13

struct bcmpmu_env_info {
 	struct bcmpmu_reg_map regmap;
	unsigned long bitmask;
};

struct bcmpmu_batt_state {
	int capacity;
	int voltage;
	int temp;
	int present;
	int capacity_lvl;
	int status;
	int health;
};

struct bcmpmu_usb_accy_data {
	enum bcmpmu_chrgr_type_t chrgr_type;
	enum bcmpmu_usb_type_t usb_type;
	int max_curr_chrgr;
	int batt_present;
};

struct bcmpmu_platform_data;
struct bcmpmu {
	struct device *dev;
	void *accinfo;
	void *irqinfo;
	void *adcinfo;
	void *battinfo;
	void *chrgrinfo;
	void *rtcinfo;
	void *envinfo;
	void *fginfo;
	void *accyinfo;
	void *eminfo;
	void *ponkeyinfo;
	void *rpcinfo;

	/* reg access */
	int (*read_dev)(struct bcmpmu *bcmpmu, int reg, unsigned int *val, unsigned int mask);
	int (*write_dev)(struct bcmpmu *bcmpmu, int reg, unsigned int val, unsigned int mask);
	int (*read_dev_drct)(struct bcmpmu *bcmpmu, int map, int addr, unsigned int *val, unsigned int mask);
	int (*write_dev_drct)(struct bcmpmu *bcmpmu, int map, int addr, unsigned int val, unsigned int mask);
	int (*read_dev_bulk)(struct bcmpmu *bcmpmu, int map, int addr, unsigned int *val, int len);
	int (*write_dev_bulk)(struct bcmpmu *bcmpmu, int map, int addr, unsigned int *val, int len);
	const struct bcmpmu_reg_map *regmap;
	/* irq */	
	int (*register_irq)(struct bcmpmu *pmu, enum bcmpmu_irq irq,
		void (*callback)(enum bcmpmu_irq irq, void *), void *data);
	int (*unregister_irq)(struct bcmpmu *pmu, enum bcmpmu_irq irq);
	int (*mask_irq)(struct bcmpmu *pmu, enum bcmpmu_irq irq);
	int (*unmask_irq)(struct bcmpmu *pmu, enum bcmpmu_irq irq);

	/* adc */
	int (*adc_req)(struct bcmpmu *pmu, struct bcmpmu_adc_req *req);

	/* env */
	void (*update_env_status)(struct bcmpmu *pmu, unsigned long *env);
	bool (*is_env_bit_set)(struct bcmpmu *pmu, enum bcmpmu_env_bit_t env_bit);
	bool (*get_env_bit_status)(struct bcmpmu *pmu, enum bcmpmu_env_bit_t env_bit);

	/* charge */
	int (*chrgr_usb_en)(struct bcmpmu *bcmpmu, int en);
	int (*chrgr_wac_en)(struct bcmpmu *bcmpmu, int en);
	int (*set_icc_fc)(struct bcmpmu *pmu, int curr);
	int (*set_icc_qc)(struct bcmpmu *pmu, int curr);
	int (*set_eoc)(struct bcmpmu *pmu, int curr);
	int (*set_vfloat)(struct bcmpmu *pmu, int volt);
	
	/* fg */
	int (*fg_currsmpl)(struct bcmpmu *pmu, int *data);
	int (*fg_vmbatt)(struct bcmpmu *pmu, int *data);
	int (*fg_acc_mas)(struct bcmpmu *pmu, int *data);
	int (*fg_enable)(struct bcmpmu *pmu, int en);
	int (*fg_reset)(struct bcmpmu *pmu);
	
	/* usb accy */
	struct bcmpmu_usb_accy_data usb_accy_data;
	int (* register_usb_callback)(struct bcmpmu *pmu,
			void (*callback)(struct bcmpmu *pmu,
				unsigned char event, void *, void *),
			void *data);
	int (*usb_set)(struct bcmpmu *pmu,
				enum bcmpmu_usb_ctrl_t ctrl, unsigned long val);
	int (*usb_get)(struct bcmpmu *pmu,
				enum bcmpmu_usb_ctrl_t ctrl, void *val);

	struct bcmpmu_platform_data *pdata;
	struct regulator_desc *rgltr_desc;
	struct bcmpmu_reg_info *rgltr_info;

	void *debugfs_root_dir;
	
	/* Client devices */
	struct platform_device *pdev[BCMPMU_REGULATOR_MAX];
};


/**
 * Data to be supplied by the platform to initialise the BCMPMU.
 *
 * @init: Function called during driver initialisation.  Should be
 *        used by the platform to configure GPIO functions and similar.
 */
struct bcmpmu_platform_data {
	struct i2c_slave_platform_data i2c_pdata;
	int (*init)(struct bcmpmu *bcmpmu);
	int (*exit)(struct bcmpmu *bcmpmu);
	struct i2c_board_info *i2c_board_info_map1;
	int i2c_adapter_id;
	int i2c_pagesize;
	int irq;
	struct bcmpmu_rw_data *init_data;
	int init_max;
	struct bcmpmu_regulator_init_data *regulator_init_data;
	struct bcmpmu_temp_map *batt_temp_map;
	int batt_temp_map_len;
	struct bcmpmu_adc_setting *adc_setting;
	int fg_smpl_rate;
	int fg_slp_rate;
	int fg_slp_curr_ua;
	int chrg_1c_rate;
	struct bcmpmu_charge_zone *chrg_zone_map;
	int fg_capacity_full;
	int support_fg;
	enum bcmpmu_bc_t bc;
	int rpc_rate;
};

int bcmpmu_clear_irqs(struct bcmpmu *bcmpmu);
int bcmpmu_sel_adcsync(enum bcmpmu_adc_timing_t timing);

const struct bcmpmu_reg_map *bcmpmu_get_regmap(void);
const struct bcmpmu_irq_map *bcmpmu_get_irqmap(void);
const struct bcmpmu_adc_map *bcmpmu_get_adcmap(void);
const struct bcmpmu_reg_map *bcmpmu_get_irqregmap(int *len);
const struct bcmpmu_reg_map *bcmpmu_get_adc_ctrl_map(void);
const struct bcmpmu_env_info *bcmpmu_get_envregmap(int *len);
const int *bcmpmu_get_usb_id_map(int *len);

struct regulator_desc *bcmpmu_rgltr_desc(void);
struct bcmpmu_reg_info *bcmpmu_rgltr_info(void);

void bcmpmu_reg_dev_init(struct bcmpmu *bcmpmu);
void bcmpmu_reg_dev_exit(struct bcmpmu *bcmpmu);
int bcmpmu_usb_add_notifier(u32, struct notifier_block *);
int bcmpmu_usb_remove_notifier(u32, struct notifier_block *);
int bcmpmu_batt_add_notifier(u32, struct notifier_block *);
int bcmpmu_batt_remove_notifier(u32, struct notifier_block *);

void bcmpmu_client_power_off(void);

#endif
