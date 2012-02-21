/*****************************************************************************
*  Copyright 2001 - 2011 Broadcom Corporation.  All rights reserved.
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
#define PMU_DEVICE_I2C_BUSNO 2

static struct bcmpmu_rw_data register_init_data[] = {
	{.map = 0, .addr = 0x0c, .val = 0x1b, .mask = 0xFF},
	{.map = 0, .addr = 0x40, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x41, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x42, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x43, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x44, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x45, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x46, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x47, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x48, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x49, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x4a, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x4b, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x4c, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x4d, .val = 0xFF, .mask = 0xFF},
	{.map = 0, .addr = 0x50, .val = 0x6B, .mask = 0xFF},
	{.map = 0, .addr = 0x51, .val = 0x03, .mask = 0xFF},
	{.map = 0, .addr = 0x52, .val = 0x08, .mask = 0xFF},
	{.map = 0, .addr = 0x53, .val = 0x00, .mask = 0xFF},
	{.map = 0, .addr = 0x54, .val = 0x03, .mask = 0xFF},
	{.map = 0, .addr = 0x55, .val = 0x08, .mask = 0xFF},
	{.map = 0, .addr = 0x56, .val = 0x08, .mask = 0xFF},
	{.map = 0, .addr = 0x57, .val = 0x07, .mask = 0xFF},
	{.map = 0, .addr = 0x58, .val = 0x01, .mask = 0xFF},
	{.map = 0, .addr = 0x59, .val = 0x00, .mask = 0xFF},
	{.map = 0, .addr = 0x5a, .val = 0x07, .mask = 0xFF},
	{.map = 0, .addr = 0x69, .val = 0x10, .mask = 0xFF},
	/*
	* OTG registers
	*/
	{.map = 0, .addr = 0x71, .val = 0x09, .mask = 0xFF},
	{.map = 0, .addr = 0x77, .val = 0xD4, .mask = 0xFF},
	{.map = 0, .addr = 0x78, .val = 0x98, .mask = 0xFF},
	{.map = 0, .addr = 0x79, .val = 0xF0, .mask = 0xFF},
	{.map = 0, .addr = 0x7A, .val = 0x60, .mask = 0xFF},
	{.map = 0, .addr = 0x7B, .val = 0xC3, .mask = 0xFF},
	{.map = 0, .addr = 0x7C, .val = 0xA7, .mask = 0xFF},
	{.map = 0, .addr = 0x7D, .val = 0x08, .mask = 0xFF},

	/*Init SDSR NM, NM2 and LPM voltages to 1.2V
	*/
	{.map = 0, .addr = 0xD0, .val = 0x13, .mask = 0xFF},
	{.map = 0, .addr = 0xD1, .val = 0x13, .mask = 0xFF},
	{.map = 0, .addr = 0xD2, .val = 0x13, .mask = 0xFF},

	/*Init CSR LPM  to 0.9 V
	CSR NM2 to 1.22V
	*/
	{.map = 0, .addr = 0xC1, .val = 0x04, .mask = 0xFF},
	{.map = 0, .addr = 0xC2, .val = 0x14, .mask = 0xFF},
	
	/*Set IOSR LMP voltage to 1.8V*/
	{.map = 0, .addr = 0xC9, .val = 0x1B, .mask = 0xFF},

	/*PLLCTRL, Clear Bit 0 to disable PLL when PC2:PC1 = 0b00*/
	{.map = 0, .addr = 0x0A, .val = 0x0E, .mask = 0x0F},
	/*CMPCTRL13, Set bits 4, 1 for BSI Sync. Mode */
	{.map = 0, .addr = 0x1C, .val = 0x13, .mask = 0xFF},
	/*CMPCTRL12, Set bits 4, 1 for NTC Sync. Mode*/
	{.map = 0, .addr = 0x1B, .val = 0x13, .mask = 0xFF},

};

static struct bcmpmu_temp_map batt_temp_map[] = {
	/*
	* This table is hardware dependent and need to get from platform team
	*/
	/*
	* adc temp
	*/
	{932, 233},			/* -40 C */
	{900, 238},			/* -35 C */
	{860, 243},			/* -30 C */
	{816, 248},			/* -25 C */
	{760, 253},			/* -20 C */
	{704, 258},			/* -15 C */
	{636, 263},			/* -10 C */
	{568, 268},			/* -5 C */
	{500, 273},			/* 0 C */
	{440, 278},			/* 5 C */
	{376, 283},			/* 10 C */
	{324, 288},			/* 15 C */
	{272, 293},			/* 20 C */
	{228, 298},			/* 25 C */
	{192, 303},			/* 30 C */
	{160, 308},			/* 35 C */
	{132, 313},			/* 40 C */
	{112, 318},			/* 45 C */
	{92, 323},			/* 50 C */
	{76, 328},			/* 55 C */
	{64, 333},			/* 60 C */
	{52, 338},			/* 65 C */
	{44, 343},			/* 70 C */
	{36, 348},			/* 75 C */
	{32, 353},			/* 80 C */
	{28, 358},			/* 85 C */
	{24, 363},			/* 90 C */
	{20, 368},			/* 95 C */
	{16, 373},			/* 100 C */
};

static struct bcmpmu_temp_map batt_temp_volt_map[] = {
	/*
	* This table is hardware dependent and need to get from platform team
	*/
	/*
	* adc temp
	*/
	{1091, 233},		/* -40 C */
	{1056, 238},		/* -35 C */
	{1011, 243},		/* -30 C */
	{956, 248},			/* -25 C */
	{893, 253},			/* -20 C */
	{823, 258},			/* -15 C */
	{748, 263},			/* -10 C */
	{669, 268},			/* -5 C */
	{591, 273},			/* 0 C */
	{515, 278},			/* 5 C */
	{443, 283},			/* 10 C */
	{378, 288},			/* 15 C */
	{320, 293},			/* 20 C */
	{270, 298},			/* 25 C */
	{226, 303},			/* 30 C */
	{189, 308},			/* 35 C */
	{158, 313},			/* 40 C */
	{132, 318},			/* 45 C */
	{111, 323},			/* 50 C */
	{93, 328},			/* 55 C */
	{78, 333},			/* 60 C */
	{65, 338},			/* 65 C */
	{55, 343},			/* 70 C */
	{47, 348},			/* 75 C */
	{40, 353},			/* 80 C */
	{34, 358},			/* 85 C */
	{29, 363},			/* 90 C */
	{25, 368},			/* 95 C */
	{21, 373},			/* 100 C */
	{18, 378},			/* 105 C */
	{16, 383},			/* 110 C */
	{14, 388},			/* 115 C */
};


struct regulator_consumer_supply rf_supply[] = {
	{.supply = "rfldo_uc"},
};
static struct regulator_init_data bcm59055_rfldo_data = {
	.constraints = {
			.name = "rfldo",
			.min_uV = 1300000,
			.max_uV = 3300000,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(rf_supply),
	.consumer_supplies = rf_supply,
};

struct regulator_consumer_supply cam_supply[] = {
	{.supply = "camldo_uc"},
};
static struct regulator_init_data bcm59055_camldo_data = {
	.constraints = {
			.name = "camldo",
			.min_uV = 1300000,
			.max_uV = 3300000,
			.valid_ops_mask =
			REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(cam_supply),
	.consumer_supplies = cam_supply,
};


struct regulator_consumer_supply hv1_supply[] = {
	{.supply = "hv1ldo_uc"},
	{.supply = "2v9_aud"},
};
static struct regulator_init_data bcm59055_hv1ldo_data = {
	.constraints = {
			.name = "hv1ldo",
			.min_uV = 1300000,
			.max_uV = 3300000,
			.valid_ops_mask =
			REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE |
			REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(hv1_supply),
	.consumer_supplies = hv1_supply,
};

struct regulator_consumer_supply hv2_supply[] = {
	{.supply = "hv2ldo_uc"},
};
static struct regulator_init_data bcm59055_hv2ldo_data = {
	.constraints = {
			.name = "hv2ldo",
			.min_uV = 1300000,
			.max_uV = 3300000,
			.valid_ops_mask =
			REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE |
			REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,

			},
	.num_consumer_supplies = ARRAY_SIZE(hv2_supply),
	.consumer_supplies = hv2_supply,
};

struct regulator_consumer_supply hv3_supply[] = {
	{.supply = "hv3ldo_uc"},
};
static struct regulator_init_data bcm59055_hv3ldo_data = {
	.constraints = {
			.name = "hv3ldo",
			.min_uV = 1300000,
			.max_uV = 3300000,
			.valid_ops_mask =
			REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE |
			REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,

			},
	.num_consumer_supplies = ARRAY_SIZE(hv3_supply),
	.consumer_supplies = hv3_supply,
};

struct regulator_consumer_supply hv4_supply[] = {
	{.supply = "hv4ldo_uc"},
};
static struct regulator_init_data bcm59055_hv4ldo_data = {
	.constraints = {
			.name = "hv4ldo",
			.min_uV = 1300000,
			.max_uV = 3300000,
			.valid_ops_mask =
			REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE |
			REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,

			},
	.num_consumer_supplies = ARRAY_SIZE(hv4_supply),
	.consumer_supplies = hv4_supply,
};

struct regulator_consumer_supply hv5_supply[] = {
	{.supply = "hv5ldo_uc"},
};
static struct regulator_init_data bcm59055_hv5ldo_data = {
	.constraints = {
			.name = "hv5ldo",
			.min_uV = 1300000,
			.max_uV = 3300000,
			.valid_ops_mask =
			REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE |
			REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,

			},
	.num_consumer_supplies = ARRAY_SIZE(hv5_supply),
	.consumer_supplies = hv5_supply,
};

struct regulator_consumer_supply hv6_supply[] = {
	{.supply = "hv6ldo_uc"},
	{.supply = "vdd_sdio"},
};
static struct regulator_init_data bcm59055_hv6ldo_data = {
	.constraints = {
			.name = "hv6ldo",
			.min_uV = 1300000,
			.max_uV = 3300000,
			.valid_ops_mask =
			REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE |
			REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(hv6_supply),
	.consumer_supplies = hv6_supply,
};

struct regulator_consumer_supply hv7_supply[] = {
	{.supply = "hv7ldo_uc"},
};
static struct regulator_init_data bcm59055_hv7ldo_data = {
	.constraints = {
			.name = "hv7ldo",
			.min_uV = 1300000,
			.max_uV = 3300000,
			.valid_ops_mask =
			REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE |
			REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			.valid_modes_mask =
			REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY |
			REGULATOR_MODE_IDLE},
	.num_consumer_supplies = ARRAY_SIZE(hv7_supply),
	.consumer_supplies = hv7_supply,
};

struct regulator_consumer_supply sim_supply[] = {
	{.supply = "simldo_uc"},
	{.supply = "sim_vcc"},
};
static struct regulator_init_data bcm59055_simldo_data = {
	.constraints = {
			.name = "simldo",
			.min_uV = 1300000,
			.max_uV = 3300000,
			.valid_ops_mask =
			REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE |
			REGULATOR_CHANGE_VOLTAGE,
/*TODO: We observed that, on Rhearay HW, interrupt from GPIO expander
is not detected by baseband if SIMLDO is disabled. As a temp. workaround
we keep SIMLDO ON by default for Rhearay till the issue is root casued*/
#ifdef CONFIG_MACH_RHEA_RAY_EDN2X
			.always_on = 1,
#endif
			},
	.num_consumer_supplies = ARRAY_SIZE(sim_supply),
	.consumer_supplies = sim_supply,
};


struct regulator_consumer_supply csr_nm_supply[] = {
	{.supply = "csr_nm_uc"},
};
static struct regulator_init_data bcm59055_csr_nm_data = {
	.constraints = {
			.name = "csr_nm",
			.min_uV = 700000,
			.max_uV = 1800000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(csr_nm_supply),
	.consumer_supplies = csr_nm_supply,
};

struct regulator_consumer_supply csr_nm2_supply[] = {
	{.supply = "csr_nm2_uc"},
};
static struct regulator_init_data bcm59055_csr_nm2_data = {
	.constraints = {
			.name = "csr_nm2",
			.min_uV = 700000,
			.max_uV = 1800000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(csr_nm2_supply),
	.consumer_supplies = csr_nm2_supply,
};

struct regulator_consumer_supply csr_lpm_supply[] = {
	{.supply = "csr_lpm_uc"},
};
static struct regulator_init_data bcm59055_csr_lpm_data = {
	.constraints = {
			.name = "csr_lpm",
			.min_uV = 700000,
			.max_uV = 1800000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(csr_lpm_supply),
	.consumer_supplies = csr_lpm_supply,
};


struct regulator_consumer_supply iosr_nm_supply[] = {
	{.supply = "iosr_nm_uc"},
};
static struct regulator_init_data bcm59055_iosr_nm_data = {
	.constraints = {
			.name = "iosr_nm",
			.min_uV = 700000,
			.max_uV = 1800000,
			.valid_ops_mask =
			REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(iosr_nm_supply),
	.consumer_supplies = iosr_nm_supply,
};

struct regulator_consumer_supply iosr_nm2_supply[] = {
	{.supply = "iosr_nm2_uc"},
};
static struct regulator_init_data bcm59055_iosr_nm2_data = {
	.constraints = {
			.name = "iosr_nm2",
			.min_uV = 700000,
			.max_uV = 1800000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(iosr_nm2_supply),
	.consumer_supplies = iosr_nm2_supply,
};
struct regulator_consumer_supply iosr_lpm_supply[] = {
	{.supply = "iosr_lmp_uc"},
};
static struct regulator_init_data bcm59055_iosr_lpm_data = {
	.constraints = {
			.name = "iosr_lmp",
			.min_uV = 700000,
			.max_uV = 1800000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(iosr_lpm_supply),
	.consumer_supplies = iosr_lpm_supply,
};

struct regulator_consumer_supply sdsr_nm_supply[] = {
	{.supply = "sdsr_nm_uc"},
};

static struct regulator_init_data bcm59055_sdsr_nm_data = {
	.constraints = {
			.name = "sdsr_nm",
			.min_uV = 700000,
			.max_uV = 1800000,
			.valid_ops_mask =REGULATOR_CHANGE_MODE ,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(sdsr_nm_supply),
	.consumer_supplies = sdsr_nm_supply,
};

struct regulator_consumer_supply sdsr_nm2_supply[] = {
	{.supply = "sdsr_nm2_uc"},
};

static struct regulator_init_data bcm59055_sdsr_nm2_data = {
	.constraints = {
			.name = "sdsr_nm2",
			.min_uV = 700000,
			.max_uV = 1800000,
			.valid_ops_mask =
			REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(sdsr_nm2_supply),
	.consumer_supplies = sdsr_nm2_supply,
};

struct regulator_consumer_supply sdsr_lpm_supply[] = {
	{.supply = "sdsr_lpm_uc"},
};

static struct regulator_init_data bcm59055_sdsr_lpm_data = {
	.constraints = {
			.name = "sdsr_lpm",
			.min_uV = 700000,
			.max_uV = 1800000,
			.valid_ops_mask =
			REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_VOLTAGE,
			.always_on = 1,
			},
	.num_consumer_supplies = ARRAY_SIZE(sdsr_lpm_supply),
	.consumer_supplies = sdsr_lpm_supply,
};

struct bcmpmu_regulator_init_data bcm59055_regulators[BCMPMU_REGULATOR_MAX] = {
	[BCMPMU_REGULATOR_RFLDO] = {
		BCMPMU_REGULATOR_RFLDO, &bcm59055_rfldo_data, 0x01, 0
	},
	[BCMPMU_REGULATOR_CAMLDO] = {
		BCMPMU_REGULATOR_CAMLDO, &bcm59055_camldo_data, 0x11, 0
	},
	[BCMPMU_REGULATOR_HV1LDO] =	{
		BCMPMU_REGULATOR_HV1LDO, &bcm59055_hv1ldo_data, 0x22, 0
	},
	[BCMPMU_REGULATOR_HV2LDO] =	{
		BCMPMU_REGULATOR_HV2LDO, &bcm59055_hv2ldo_data, 0x11, 0
	},
	[BCMPMU_REGULATOR_HV3LDO] = {
		BCMPMU_REGULATOR_HV3LDO, &bcm59055_hv3ldo_data, 0x22, 0
	},
	[BCMPMU_REGULATOR_HV4LDO] =	{
		BCMPMU_REGULATOR_HV4LDO, &bcm59055_hv4ldo_data, 0x11, 0
	},
	[BCMPMU_REGULATOR_HV5LDO] = {
		BCMPMU_REGULATOR_HV5LDO, &bcm59055_hv5ldo_data, 0x11, 0
	},
	[BCMPMU_REGULATOR_HV6LDO] = {
		BCMPMU_REGULATOR_HV6LDO, &bcm59055_hv6ldo_data, 0x11, 0
	},
	[BCMPMU_REGULATOR_HV7LDO] = {
		BCMPMU_REGULATOR_HV7LDO, &bcm59055_hv7ldo_data, 0x22, 0
	},

/*TODO: We observed that, on Rhearay HW, interrupt from GPIO expander
is not detected by baseband if SIMLDO is disabled. As a temp. workaround
we keep SIMLDO ON by default for Rhearay till the issue is root casued*/
#ifdef CONFIG_MACH_RHEA_RAY_EDN2X
	[BCMPMU_REGULATOR_SIMLDO] = {
		BCMPMU_REGULATOR_SIMLDO, &bcm59055_simldo_data, 0x00,
			BCMPMU_REGL_LPM_IN_DSM
	},
#else
	[BCMPMU_REGULATOR_SIMLDO] = {
		BCMPMU_REGULATOR_SIMLDO, &bcm59055_simldo_data, 0xAA,
			BCMPMU_REGL_LPM_IN_DSM
	},
#endif
	[BCMPMU_REGULATOR_CSR_NM] =	{
		BCMPMU_REGULATOR_CSR_NM, &bcm59055_csr_nm_data, 0x31, 0
	},
	[BCMPMU_REGULATOR_CSR_NM2] = {
		BCMPMU_REGULATOR_CSR_NM2, &bcm59055_csr_nm2_data, 0xFF, 0
	},
	[BCMPMU_REGULATOR_CSR_LPM] = {
		BCMPMU_REGULATOR_CSR_LPM, &bcm59055_csr_lpm_data, 0xFF, 0
	},
	[BCMPMU_REGULATOR_IOSR_NM] = {
		BCMPMU_REGULATOR_IOSR_NM, &bcm59055_iosr_nm_data, 0x01, 0
	},
	[BCMPMU_REGULATOR_IOSR_NM2] = {
		BCMPMU_REGULATOR_IOSR_NM2, &bcm59055_iosr_nm2_data, 0xFF, 0
	},
	[BCMPMU_REGULATOR_IOSR_LPM] = {
		BCMPMU_REGULATOR_IOSR_LPM, &bcm59055_iosr_lpm_data, 0xFF, 0
	},
	[BCMPMU_REGULATOR_SDSR_NM] = {
		BCMPMU_REGULATOR_SDSR_NM, &bcm59055_sdsr_nm_data, 0x11, 0
	},
	[BCMPMU_REGULATOR_SDSR_NM2] = {
		BCMPMU_REGULATOR_SDSR_NM2, &bcm59055_sdsr_nm2_data, 0xFF, 0
	},
	[BCMPMU_REGULATOR_SDSR_LPM] = {
		BCMPMU_REGULATOR_SDSR_LPM, &bcm59055_sdsr_lpm_data, 0xFF, 0
	},
};

static struct bcmpmu_wd_setting bcm59055_wd_setting = {
	.flags = WATCHDOG_OTP_ENABLED,
	.watchdog_timeout = 32,
};

static struct platform_device bcmpmu_audio_device = {
	.name = "bcmpmu_audio",
	.id = -1,
	.dev.platform_data = NULL,
};

static struct platform_device bcmpmu_em_device = {
	.name = "bcmpmu_em",
	.id = -1,
	.dev.platform_data = NULL,
};

#ifdef CONFIG_BCMPMU_CSAPI_ADC
static struct platform_device bcmpmu_adc_chipset_api = {
	.name = "bcmpmu_adc_chipset_api",
	.id = -1,
	.dev.platform_data = NULL,
};
#endif

static struct platform_device bcmpmu_otg_xceiv_device = {
	.name = "bcmpmu_otg_xceiv",
	.id = -1,
	.dev.platform_data = NULL,
};

#ifdef CONFIG_BCMPMU_SELFTEST
static struct platform_device bcmpmu_selftest_device = {
	.name = "bcmpmu_selftest",
	.id = -1,
	.dev.platform_data = NULL,
};
#endif

#ifdef CONFIG_BCMPMU_RPC
static struct platform_device bcmpmu_rpc = {
	.name = "bcmpmu_rpc",
	.id = -1,
	.dev.platform_data = NULL,
};
#endif

static struct platform_device *bcmpmu_client_devices[] = {
	&bcmpmu_audio_device,
	&bcmpmu_em_device,
#ifdef CONFIG_BCMPMU_CSAPI_ADC
	&bcmpmu_adc_chipset_api,
#endif
	&bcmpmu_otg_xceiv_device,
#ifdef CONFIG_BCMPMU_SELFTEST
	&bcmpmu_selftest_device,
#endif
#ifdef CONFIG_BCMPMU_RPC
	&bcmpmu_rpc,
#endif
};

static int __init bcmpmu_init_platform_hw(struct bcmpmu *bcmpmu)
{
	int i;
	printk(KERN_INFO "%s: called.\n", __func__);

	for (i = 0; i < ARRAY_SIZE(bcmpmu_client_devices); i++)
		bcmpmu_client_devices[i]->dev.platform_data = bcmpmu;
	platform_add_devices(bcmpmu_client_devices,
			ARRAY_SIZE(bcmpmu_client_devices));

	return 0;
}

static int __init bcmpmu_exit_platform_hw(struct bcmpmu *bcmpmu)
{
	printk(KERN_INFO"REG: pmu_init_platform_hw called\n");

	return 0;
}

static struct i2c_board_info pmu_info_map1 = {
	I2C_BOARD_INFO("bcmpmu_map1", PMU_DEVICE_I2C_ADDR1),
};

static struct bcmpmu_adc_setting adc_setting = {
	.tx_rx_sel_addr = 0,
	.tx_delay = 0,
	.rx_delay = 0,
	.sw_timeout = 50,		/* revisit */
	.txrx_timeout = 2000,	/* revisit */
	.compensation_samples = 8,	/* from experiments */
	.compensation_volt_lo = 72,	/* 6% channel (of 1200 mV) */
	.compensation_volt_hi = 1128,	/* 94% channel (of 1200 mV) */
	.compensation_interval = 900,
};

static struct bcmpmu_charge_zone chrg_zone[] = {
	{.tl = 253, .th = 333, .v = 3000, .fc = 10, .qc = 100},	/* Zone QC */
	{.tl = 253, .th = 272, .v = 4100, .fc = 50, .qc = 0},	/* Zone LL */
	{.tl = 273, .th = 282, .v = 4200, .fc = 50, .qc = 0},	/* Zone L */
	{.tl = 283, .th = 318, .v = 4200, .fc = 100, .qc = 0},	/* Zone N */
	{.tl = 319, .th = 323, .v = 4200, .fc = 50, .qc = 0},	/* Zone H */
	{.tl = 324, .th = 333, .v = 4100, .fc = 50, .qc = 0},	/* Zone HH */
	{.tl = 253, .th = 333, .v = 0, .fc = 0, .qc = 0},	/* Zone OUT */
};

/*
* Initialization: batt_temp, pa_temp and x32_temp could use different NTCs,
* but that is not the case so far
*/
static struct bcmpmu_platform_data bcmpmu_plat_data = {
	.init = bcmpmu_init_platform_hw,
	.exit = bcmpmu_exit_platform_hw,
	.i2c_board_info_map1 = &pmu_info_map1,
	.i2c_adapter_id = PMU_DEVICE_I2C_BUSNO,
	.i2c_pagesize = 256,
	.init_data = &register_init_data[0],
	.init_max = ARRAY_SIZE(register_init_data),
	.batt_temp_voltmap = &batt_temp_volt_map[0],
	.batt_temp_voltmap_len = ARRAY_SIZE(batt_temp_volt_map),
	.pa_temp_voltmap = &batt_temp_volt_map[0],
	.pa_temp_voltmap_len = ARRAY_SIZE(batt_temp_volt_map),
	.x32_temp_voltmap = &batt_temp_volt_map[0],
	.x32_temp_voltmap_len = ARRAY_SIZE(batt_temp_volt_map),
	.batt_temp_map = &batt_temp_map[0],
	.batt_temp_map_len = ARRAY_SIZE(batt_temp_map),
	.adc_setting = &adc_setting,
	.num_of_regl = ARRAY_SIZE(bcm59055_regulators),
	.regulator_init_data = bcm59055_regulators,
	.fg_smpl_rate = 2083,
	.fg_slp_rate = 32000,
	.fg_slp_curr_ua = 1000,
	.fg_factor = 976,		/* 59055 specific */
	.chrg_1c_rate = 1000,
	.chrg_zone_map = &chrg_zone[0],
	.fg_capacity_full = 1500 * 3600,
	.support_fg = 1,
	.bc = BCMPMU_BC_BB_BC12,
	.wd_setting = &bcm59055_wd_setting,
	.batt_model = "Unknown",
	.cutoff_volt = 3200,
	.cutoff_count_max = 3,
};

static struct i2c_board_info __initdata pmu_info[] = {
	{
	I2C_BOARD_INFO("bcmpmu", PMU_DEVICE_I2C_ADDR),
	.platform_data = &bcmpmu_plat_data,
	},
};


__init int board_pmu_init(void)
{
	int             ret;
	int             irq;
	ret = gpio_request(PMU_DEVICE_INT_GPIO, "bcmpmu-irq");
	if (ret < 0) {

		printk(KERN_ERR "%s filed at gpio_request.\n", __FUNCTION__);
		goto exit;
	}
	ret = gpio_direction_input(PMU_DEVICE_INT_GPIO);
	if (ret < 0) {

		printk(KERN_ERR "%s filed at gpio_direction_input.\n", __FUNCTION__);
		goto exit;
	}
	irq = gpio_to_irq(PMU_DEVICE_INT_GPIO);
	bcmpmu_plat_data.irq = irq;

	i2c_register_board_info(PMU_DEVICE_I2C_BUSNO,
				pmu_info, ARRAY_SIZE(pmu_info));
exit:
	return ret;
}

arch_initcall(board_pmu_init);
