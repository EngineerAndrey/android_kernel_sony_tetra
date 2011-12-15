/************************************************************************************************/
/*                                                                                              */
/*  Copyright 2010  Broadcom Corporation                                                        */
/*                                                                                              */
/*     Unless you and Broadcom execute a separate written software license agreement governing  */
/*     use of this software, this software is licensed to you under the terms of the GNU        */
/*     General Public License version 2 (the GPL), available at                                 */
/*                                                                                              */
/*          http://www.broadcom.com/licenses/GPLv2.php                                          */
/*                                                                                              */
/*     with the following added to such license:                                                */
/*                                                                                              */
/*     As a special exception, the copyright holders of this software give you permission to    */
/*     link this software with independent modules, and to copy and distribute the resulting    */
/*     executable under terms of your choice, provided that you also meet, for each linked      */
/*     independent module, the terms and conditions of the license of that module.              */
/*     An independent module is a module which is not derived from this software.  The special  */
/*     exception does not apply to any modifications of the software.                           */
/*                                                                                              */
/*     Notwithstanding the above, under no circumstances may you combine this software in any   */
/*     way with any other Broadcom software provided under a license other than the GPL,        */
/*     without Broadcom's express prior written consent.                                        */
/*                                                                                              */
/************************************************************************************************/
#include <linux/version.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <mach/hardware.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/mfd/bcmpmu.h>

#define PMU_DEVICE_I2C_ADDR	0x08
#define PMU_DEVICE_I2C_ADDR1	0x0C
#define PMU_DEVICE_INT_GPIO	29

static struct bcmpmu_rw_data register_init_data[] = {
	{.map=0, .addr=0x01, .val=0x00, .mask=0x01},
	{.map=0, .addr=0x0c, .val=0x1b, .mask=0xFF},
	{.map=0, .addr=0x40, .val=0xFF, .mask=0xFF},
	{.map=0, .addr=0x41, .val=0xFF, .mask=0xFF},
	{.map=0, .addr=0x42, .val=0xFF, .mask=0xFF},
	{.map=0, .addr=0x43, .val=0xFF, .mask=0xFF},
	{.map=0, .addr=0x44, .val=0xFF, .mask=0xFF},
	{.map=0, .addr=0x45, .val=0xFF, .mask=0xFF},
	{.map=0, .addr=0x46, .val=0xFF, .mask=0xFF},
	{.map=0, .addr=0x47, .val=0xFF, .mask=0xFF},
	{.map=0, .addr=0x52, .val=0x04, .mask=0x04},
/* temp workaround for LDOs,
 to be revisited once final OTP value available */
	{.map=0, .addr=0xB1, .val=0x4B, .mask=0xFF},
	{.map=0, .addr=0xB2, .val=0x04, .mask=0xFF},
	{.map=0, .addr=0xB3, .val=0x25, .mask=0xFF},
	{.map=0, .addr=0xB4, .val=0x27, .mask=0xFF},
	{.map=0, .addr=0xB5, .val=0x05, .mask=0xFF},
	{.map=0, .addr=0xB6, .val=0x07, .mask=0xFF},
	{.map=0, .addr=0xB7, .val=0x25, .mask=0xFF},
	{.map=0, .addr=0xB8, .val=0x06, .mask=0xFF},
	{.map=0, .addr=0xB9, .val=0x07, .mask=0xFF},
	{.map=0, .addr=0xBD, .val=0x21, .mask=0xFF},
};

static struct bcmpmu_temp_map batt_temp_map[] = {
/* This table is hardware dependent and need to get from platform team */
/*	adc		temp*/
	{932,		233},/* -40 C */
	{900,		238},/* -35 C */
	{860,		243},/* -30 C */
	{816,		248},/* -25 C */
	{760,		253},/* -20 C */
	{704,		258},/* -15 C */
	{636,		263},/* -10 C */
	{568,		268},/* -5 C */
	{500,		273},/* 0 C */
	{440,		278},/* 5 C */
	{376,		283},/* 10 C */
	{324,		288},/* 15 C */
	{272,		293},/* 20 C */
	{228,		298},/* 25 C */
	{192,		303},/* 30 C */
	{160,		308},/* 35 C */
	{132,		313},/* 40 C */
	{112,		318},/* 45 C */
	{92,		323},/* 50 C */
	{76,		328},/* 55 C */
	{64,		333},/* 60 C */
	{52,		338},/* 65 C */
	{44,		343},/* 70 C */
	{36,		348},/* 75 C */
	{32,		353},/* 80 C */
	{28,		358},/* 85 C */
	{24,		363},/* 90 C */
	{20,		368},/* 95 C */
	{16,		373},/* 100 C */
};

struct regulator_consumer_supply rf_supply[] = {
	{ .supply = "rf"},
};
static struct regulator_init_data bcm59039_rfldo_data =  {
	.constraints = {
		.name = "rfldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(rf_supply),
	.consumer_supplies = rf_supply,
};

struct regulator_consumer_supply cam_supply[] = {
	{.supply = "cam"},
};
static struct regulator_init_data bcm59039_camldo_data = {
	.constraints = {
		.name = "camldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS |REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(cam_supply),
	.consumer_supplies = cam_supply,
};


struct regulator_consumer_supply hv1_supply[] = {
	{.supply = "hv1"},
};
static struct regulator_init_data bcm59039_hv1ldo_data = {
	.constraints = {
		.name = "hv1ldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS |REGULATOR_CHANGE_MODE |  REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(hv1_supply),
	.consumer_supplies = hv1_supply,
};

struct regulator_consumer_supply hv2_supply[] = {
	{.supply = "hv2"},
};
static struct regulator_init_data bcm59039_hv2ldo_data = {
	.constraints = {
		.name = "hv2ldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(hv2_supply),
	.consumer_supplies = hv2_supply,
};

struct regulator_consumer_supply hv3_supply[] = {
	{.supply = "hv3"},
};
static struct regulator_init_data bcm59039_hv3ldo_data = {
	.constraints = {
		.name = "hv3ldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(hv3_supply),
	.consumer_supplies = hv3_supply,
};

struct regulator_consumer_supply hv4_supply[] = {
	{.supply = "hv4"},
};
static struct regulator_init_data bcm59039_hv4ldo_data = {
	.constraints = {
		.name = "hv4ldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(hv4_supply),
	.consumer_supplies = hv4_supply,
};

struct regulator_consumer_supply hv5_supply[] = {
	{.supply = "hv5"},
};
static struct regulator_init_data bcm59039_hv5ldo_data = {
	.constraints = {
		.name = "hv5ldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(hv5_supply),
	.consumer_supplies = hv5_supply,
};

struct regulator_consumer_supply hv6_supply[] = {
	{.supply = "hv6"},
};
static struct regulator_init_data bcm59039_hv6ldo_data = {
	.constraints = {
		.name = "hv6ldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(hv6_supply),
	.consumer_supplies = hv6_supply,
};

struct regulator_consumer_supply hv7_supply[] = {
	{.supply = "hv7"},
};
static struct regulator_init_data bcm59039_hv7ldo_data = {
	.constraints = {
		.name = "hv7ldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(hv7_supply),
	.consumer_supplies = hv7_supply,
};

struct regulator_consumer_supply hv8_supply[] = {
	{.supply = "hv8"},
};
static struct regulator_init_data bcm59039_hv8ldo_data = {
	.constraints = {
		.name = "hv8ldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(hv8_supply),
	.consumer_supplies = hv8_supply,
};

struct regulator_consumer_supply hv9_supply[] = {
	{.supply = "hv9"},
};
static struct regulator_init_data bcm59039_hv9ldo_data = {
	.constraints = {
		.name = "hv9ldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(hv9_supply),
	.consumer_supplies = hv9_supply,
};

struct regulator_consumer_supply hv10_supply[] = {
	{.supply = "hv10"},
} ;

static struct regulator_init_data bcm59039_hv10ldo_data = {
	.constraints = {
		.name = "hv10ldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(hv10_supply),
	.consumer_supplies = hv10_supply,
};

struct regulator_consumer_supply sim_supply[] = {
	{.supply = "sim_vcc"},
};
static struct regulator_init_data bcm59039_simldo_data = {
	.constraints = {
		.name = "simldo",
		.min_uV = 1300000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(sim_supply),
	.consumer_supplies = sim_supply,
};

struct regulator_consumer_supply csr_supply[] = {
	{.supply = "csr"},
};
static struct regulator_init_data bcm59039_csr_data = {
	.constraints = {
		.name = "csr",
		.min_uV = 700000,
		.max_uV = 1800000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(csr_supply),
	.consumer_supplies = csr_supply,
};

struct regulator_consumer_supply iosr_supply[] = {
	{.supply = "iosr"},
};
static struct regulator_init_data bcm59039_iosr_data = {
	.constraints = {
		.name = "iosr",
		.min_uV = 700000,
		.max_uV = 1800000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
	},
	.num_consumer_supplies = ARRAY_SIZE(iosr_supply),
	.consumer_supplies = iosr_supply,
};

struct regulator_consumer_supply sdsr_supply[] = {
	{.supply = "sdsr"},
};
static struct regulator_init_data bcm59039_sdsr_data = {
	.constraints = {
		.name = "sdsr",
		.min_uV = 700000,
		.max_uV = 1800000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.initial_mode = REGULATOR_MODE_NORMAL,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY
		},
	.num_consumer_supplies = ARRAY_SIZE(sdsr_supply),
	.consumer_supplies = sdsr_supply,
};

struct bcmpmu_regulator_init_data bcm59039_regulators[] = {
	{BCMPMU_REGULATOR_RFLDO, &bcm59039_rfldo_data},
	{BCMPMU_REGULATOR_CAMLDO, &bcm59039_camldo_data},
	{BCMPMU_REGULATOR_HV1LDO, &bcm59039_hv1ldo_data},
	{BCMPMU_REGULATOR_HV2LDO, &bcm59039_hv2ldo_data},
	{BCMPMU_REGULATOR_HV3LDO, &bcm59039_hv3ldo_data},
	{BCMPMU_REGULATOR_HV4LDO, &bcm59039_hv4ldo_data},
	{BCMPMU_REGULATOR_HV5LDO, &bcm59039_hv5ldo_data},
	{BCMPMU_REGULATOR_HV6LDO, &bcm59039_hv6ldo_data},
	{BCMPMU_REGULATOR_HV7LDO, &bcm59039_hv7ldo_data},
	{BCMPMU_REGULATOR_HV8LDO, &bcm59039_hv8ldo_data},
	{BCMPMU_REGULATOR_HV9LDO, &bcm59039_hv9ldo_data},
	{BCMPMU_REGULATOR_HV10LDO, &bcm59039_hv10ldo_data},
	{BCMPMU_REGULATOR_SIMLDO, &bcm59039_simldo_data},
	{BCMPMU_REGULATOR_CSR, &bcm59039_csr_data},
	{BCMPMU_REGULATOR_IOSR, &bcm59039_iosr_data},
	{BCMPMU_REGULATOR_SDSR, &bcm59039_sdsr_data}
};

static struct platform_device bcmpmu_audio_device = {
	.name 			= "bcmpmu_audio",
	.id			= -1,
	.dev.platform_data 	= NULL,
};

static struct platform_device bcmpmu_em_device = {
	.name 			= "bcmpmu_em",
	.id			= -1,
	.dev.platform_data 	= NULL,
};

static struct platform_device bcmpmu_otg_xceiv_device = {
	.name 			= "bcmpmu_otg_xceiv",
	.id			= -1,
	.dev.platform_data 	= NULL,
};

#ifdef CONFIG_BCMPMU_RPC
static struct platform_device bcmpmu_rpc = {
	.name 			= "bcmpmu_rpc",
	.id			= -1,
	.dev.platform_data 	= NULL,
};
#endif

static struct platform_device *bcmpmu_client_devices[] = {
	&bcmpmu_audio_device,
	&bcmpmu_em_device,
	&bcmpmu_otg_xceiv_device,
#ifdef CONFIG_BCMPMU_RPC
	&bcmpmu_rpc,
#endif
};

static int __init bcmpmu_init_platform_hw(struct bcmpmu *bcmpmu)
{
	int i;
	printk(KERN_INFO "%s: called.\n", __func__);

	for (i = 0; i <ARRAY_SIZE(bcmpmu_client_devices); i++)
		bcmpmu_client_devices[i]->dev.platform_data = bcmpmu;
	platform_add_devices(bcmpmu_client_devices, ARRAY_SIZE(bcmpmu_client_devices));

	return 0;
}

static int __init bcmpmu_exit_platform_hw(struct bcmpmu *bcmpmu)
{
	printk("REG: pmu_init_platform_hw called \n");

	return 0;
}

static struct i2c_board_info pmu_info_map1 = {
	I2C_BOARD_INFO("bcmpmu_map1", PMU_DEVICE_I2C_ADDR1),
};

static struct bcmpmu_adc_setting adc_setting = {
	.tx_rx_sel_addr = 0,
	.tx_delay = 0,
	.rx_delay = 0,
};

static struct bcmpmu_charge_zone chrg_zone[] = {
	{.tl = 253, .th = 333, .v = 3000, .fc = 10, .qc = 100},/* Zone QC */
	{.tl = 253, .th = 272, .v = 4100, .fc = 50, .qc = 0},/* Zone LL */
	{.tl = 273, .th = 282, .v = 4200, .fc = 50, .qc = 0},/* Zone L */
	{.tl = 283, .th = 318, .v = 4200, .fc = 100,.qc = 0},/* Zone N */
	{.tl = 319, .th = 323, .v = 4200, .fc = 50, .qc = 0},/* Zone H */
	{.tl = 324, .th = 333, .v = 4100, .fc = 50, .qc = 0},/* Zone HH */
	{.tl = 253, .th = 333, .v = 0,    .fc = 0,  .qc = 0},/* Zone OUT */
};

static struct bcmpmu_voltcap_map batt_voltcap_map[] = {
/* Battery data for 1350mAH */
/*	volt		capacity*/
	{4160,		100},
	{4130,		95},
	{4085,		90},
	{4040,		85},
	{3986,		80},
	{3948,		75},
	{3914,		70},
	{3877,		65},
	{3842,		60},
	{3815,		55},
	{3794,		50},
	{3776,		45},
	{3761,		40},
	{3751,		35},
	{3742,		30},
	{3724,		25},
	{3684,		20},
	{3659,		15},
	{3612,		10},
	{3565,		8},
	{3507,		6},
	{3430,		4},
	{3340,		2},
	{3236,		0},
};

static struct bcmpmu_platform_data __initdata bcmpmu_plat_data = {
	.i2c_pdata	=  ADD_I2C_SLAVE_SPEED(BSC_BUS_SPEED_400K),
	.init = bcmpmu_init_platform_hw,
	.exit = bcmpmu_exit_platform_hw,
	.i2c_board_info_map1 = &pmu_info_map1,
	.i2c_adapter_id = 2,
	.i2c_pagesize = 256,
	.init_data = &register_init_data[0],
	.init_max = ARRAY_SIZE(register_init_data),
	.batt_temp_map = &batt_temp_map[0],
	.batt_temp_map_len = ARRAY_SIZE(batt_temp_map),
	.adc_setting = &adc_setting,
	.num_of_regl = ARRAY_SIZE(bcm59039_regulators),
	.regulator_init_data = &bcm59039_regulators,
	.support_fg = 1,
	.fg_smpl_rate = 2083,
	.fg_slp_rate = 32000,
	.fg_slp_curr_ua = 1000,
	.fg_factor = 1000,
	.fg_sns_res = 10,
	.batt_voltcap_map = &batt_voltcap_map[0],
	.batt_voltcap_map_len = ARRAY_SIZE(batt_voltcap_map),
	.batt_impedence = 238,
	.chrg_1c_rate = 1350,
	.chrg_eoc = 67,
	.chrg_zone_map = &chrg_zone[0],
	.fg_capacity_full = 1350*3600,
	.support_fg = 1,
	.bc = BCMPMU_BC_PMU_BC12,//BCMPMU_BC_BB_BC12,
};

static struct i2c_board_info __initdata pmu_info[] =
{
	{
		I2C_BOARD_INFO("bcmpmu", PMU_DEVICE_I2C_ADDR),
		.platform_data  = &bcmpmu_plat_data,
		.irq = gpio_to_irq(PMU_DEVICE_INT_GPIO),
	},
};


void __init board_pmu_init(void)
{
	int ret;
	int irq;
	ret = gpio_request(PMU_DEVICE_INT_GPIO, "bcmpmu-irq");
	if (ret < 0)
		printk(KERN_ERR "%s filed at gpio_request.\n", __FUNCTION__);

	ret = gpio_direction_input(PMU_DEVICE_INT_GPIO);
	if (ret < 0)
		printk(KERN_ERR "%s filed at gpio_direction_input.\n", __FUNCTION__);
	irq = gpio_to_irq(PMU_DEVICE_INT_GPIO);
	bcmpmu_plat_data.irq = irq;

	i2c_register_board_info(2,		// This is i2c adapter number.
				pmu_info,
				ARRAY_SIZE(pmu_info));
}

