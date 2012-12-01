/*********************************************************************
 * *
 * *  Copyright 2010 Broadcom Corporation
 * *
 * *  Unless you and Broadcom execute a separate written software license
 * *  agreement governing use of this software, this software is licensed
 * *  to you under the terms of the GNU
 * *  General Public License version 2 (the GPL), available at
 * *  http://www.broadcom.com/licenses/GPLv2.php with the following added
 * *  to such license:
 * *  As a special exception, the copyright holders of this software give
 * *  you permission to link this software with independent modules, and
 * *  to copy and distribute the resulting executable under terms of your
 * *  choice, provided that you also meet, for each linked independent module,
 * *  the terms and conditions of the license of that module. An independent
 * *  module is a module which is not derived from this software.  The special
 * *  exception does not apply to any modifications of the software.
 * *  Notwithstanding the above, under no circumstances may you combine this
 * *  software in any way with any other Broadcom software provided under a
 * *  license other than the GPL, without Broadcom's express prior written
 * *  consent.
 * ***********************************************************************/

#include <linux/version.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/serial_8250.h>
#include <linux/dma-contiguous.h>
#include <linux/dma-mapping.h>
#include <mach/io_map.h>
#ifdef CONFIG_ANDROID_PMEM
#include <linux/android_pmem.h>
#endif
#include <linux/kernel_stat.h>
#include <linux/broadcom/ipcinterface.h>
#include <linux/memblock.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/pmu.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/kona.h>
#include <mach/hawaii.h>
#include <mach/rdb/brcm_rdb_uartb.h>
#include <plat/chal/chal_trace.h>
#ifdef CONFIG_KONA_AVS
#include <plat/kona_avs.h>
#include "pm_params.h"
#endif
#include <trace/stm.h>
#if defined(CONFIG_KONA_CPU_FREQ_DRV)
#include <plat/kona_cpufreq_drv.h>
#include <linux/cpufreq.h>
#include <mach/clock.h>
#include <linux/clk.h>
#include <mach/pi_mgr.h>
#endif

#ifdef CONFIG_UNICAM
#include <plat/kona_unicam.h>
#endif

#ifdef CONFIG_KONA_POWER_MGR
#include <plat/pwr_mgr.h>

#endif
#ifdef CONFIG_SENSORS_KONA
#include <linux/broadcom/kona-thermal.h>
#ifdef CONFIG_MFD_BCM_PMU590XX
/* Remove this comment when the Hawaii pmu header is added here*/
#else
#include <linux/csapi_adc.h>
#include <linux/mfd/bcmpmu.h>
#endif
#endif

#ifdef CONFIG_USB_DWC_OTG
#include <mach/clock.h>
#include <linux/usb/bcm_hsotgctrl.h>
#include <linux/usb/otg.h>
#endif

#ifdef CONFIG_ION
#include <linux/ion.h>
#include <linux/broadcom/kona_ion.h>
#endif

#ifdef CONFIG_KONA_MEMC
#include <plat/kona_memc.h>
#endif

#include "devices.h"

/* dynamic ETM support */
unsigned int etm_on;
EXPORT_SYMBOL(etm_on);

#ifdef CONFIG_ANDROID_PMEM
struct platform_device android_pmem = {
	.name = "android_pmem",
	.id = 0,
	.dev = {
		.platform_data = &android_pmem_data,
	},
};
#endif

#ifdef CONFIG_ION
struct platform_device ion_system_device = {
	.name = "ion-kona",
	.id = 2,
	.num_resources = 0,
};

struct platform_device ion_carveout_device = {
	.name = "ion-kona",
	.id = 0,
	.dev = {
		.platform_data = &ion_carveout_data,
	},
	.num_resources = 0,
};

#ifdef CONFIG_CMA
static u64 ion_dmamask = DMA_BIT_MASK(32);
struct platform_device ion_cma_device = {
	.name = "ion-kona",
	.id = 1,
	.dev = {
		.dma_mask = &ion_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &ion_cma_data,
	},
	.num_resources = 0,
};
#endif	/* CONFIG_CMA */
#endif	/* CONFIG_ION */

struct platform_device hawaii_serial_device = {
	.name = "serial8250_dw",
	.id = PLAT8250_DEV_PLATFORM,
};

static struct resource hawaii_i2c0_resource[] = {
	[0] = {
			.start = BSC1_BASE_ADDR,
			.end = BSC1_BASE_ADDR + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
	},
	[1] = {
			.start = BCM_INT_ID_I2C0,
			.end = BCM_INT_ID_I2C0,
			.flags = IORESOURCE_IRQ,
	},
};

static struct resource hawaii_i2c1_resource[] = {
	[0] = {
			.start = BSC2_BASE_ADDR,
			.end = BSC2_BASE_ADDR + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
	},
	[1] = {
			.start = BCM_INT_ID_I2C1,
			.end = BCM_INT_ID_I2C1,
			.flags = IORESOURCE_IRQ,
	},
};

static struct resource hawaii_i2c2_resource[] = {
	[0] = {
			.start = BSC3_BASE_ADDR,
			.end = BSC3_BASE_ADDR + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
	},
	[1] = {
			.start = BCM_INT_ID_I2C2,
			.end = BCM_INT_ID_I2C2,
			.flags = IORESOURCE_IRQ,
	},
};

static struct resource hawaii_i2c3_resource[] = {
	[0] = {
			.start = BSC4_BASE_ADDR,
			.end = BSC4_BASE_ADDR + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
	},
	[1] = {
			.start = BCM_INT_ID_I2C3,
			.end = BCM_INT_ID_I2C3,
			.flags = IORESOURCE_IRQ,
	},
};

static struct resource hawaii_pmu_bsc_resource[] = {
	[0] = {
			.start = PMU_BSC_BASE_ADDR,
			.end = PMU_BSC_BASE_ADDR + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
	},
	[1] = {
			.start = BCM_INT_ID_PM_I2C,
			.end = BCM_INT_ID_PM_I2C,
			.flags = IORESOURCE_IRQ,
	},
};

#define HAWAII_I2C_ADAP(num, res)			\
{							\
	.name = "bsc-i2c",				\
	.id   = num,					\
	.resource = res,				\
	.num_resources = ARRAY_SIZE(res),		\
}							\

struct platform_device hawaii_i2c_adap_devices[] = {
	HAWAII_I2C_ADAP(0, hawaii_i2c0_resource),
	HAWAII_I2C_ADAP(1, hawaii_i2c1_resource),
	HAWAII_I2C_ADAP(2, hawaii_i2c2_resource),
	HAWAII_I2C_ADAP(3, hawaii_i2c3_resource),
	HAWAII_I2C_ADAP(4, hawaii_pmu_bsc_resource),
};

static struct resource hawaii_sdio1_resource[] = {
	[0] = {
		.start = SDIO1_BASE_ADDR,
		.end = SDIO1_BASE_ADDR + SZ_64K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = BCM_INT_ID_SDIO0,
		.end = BCM_INT_ID_SDIO0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource hawaii_sdio2_resource[] = {
	[0] = {
		.start = SDIO2_BASE_ADDR,
		.end = SDIO2_BASE_ADDR + SZ_64K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = BCM_INT_ID_SDIO1,
		.end = BCM_INT_ID_SDIO1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource hawaii_sdio3_resource[] = {
	[0] = {
		.start = SDIO3_BASE_ADDR,
		.end = SDIO3_BASE_ADDR + SZ_64K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = BCM_INT_ID_SDIO_NAND,
		.end = BCM_INT_ID_SDIO_NAND,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device hawaii_sdio1_device = {
	.name = "sdhci",
	.id = 0,
	.resource = hawaii_sdio1_resource,
	.num_resources   = ARRAY_SIZE(hawaii_sdio1_resource),
};

struct platform_device hawaii_sdio2_device = {
	.name = "sdhci",
	.id = 1,
	.resource = hawaii_sdio2_resource,
	.num_resources   = ARRAY_SIZE(hawaii_sdio2_resource),
};

struct platform_device hawaii_sdio3_device = {
	.name = "sdhci",
	.id = 2,
	.resource = hawaii_sdio3_resource,
	.num_resources   = ARRAY_SIZE(hawaii_sdio3_resource),
};

#ifdef CONFIG_KEYBOARD_BCM
struct platform_device hawaii_kp_device = {
	.name = "bcm_keypad",
	.id = -1,
};
#endif

#ifdef CONFIG_KONA_HEADSET_MULTI_BUTTON
#define HS_IRQ		gpio_to_irq(92)
#define HSB_IRQ		BCM_INT_ID_AUXMIC_COMP2
#define HSB_REL_IRQ	BCM_INT_ID_AUXMIC_COMP2_INV

static struct resource board_headset_resource[] = {
	{	/* For AUXMIC */
		.start = AUXMIC_BASE_ADDR,
		.end = AUXMIC_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{	/* For ACI */
		.start = ACI_BASE_ADDR,
		.end = ACI_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{	/* For Headset IRQ */
		.start = HS_IRQ,
		.end = HS_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{	/* For Headset button  press IRQ */
		.start = HSB_IRQ,
		.end = HSB_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{	/* For Headset button  release IRQ */
		.start = HSB_REL_IRQ,
		.end = HSB_REL_IRQ,
		.flags = IORESOURCE_IRQ,
	},
		/* For backward compatibility keep COMP1
		 * as the last resource. The driver which
		 * uses only GPIO and COMP2, might not use this at all
		 */
	{	/* COMP1 for type detection */
		.start = BCM_INT_ID_AUXMIC_COMP1,
		.end = HSB_REL_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device hawaii_headset_device = {
	.name = "konaaciheadset",
	.id = -1,
	.resource = board_headset_resource,
	.num_resources	= ARRAY_SIZE(board_headset_resource),
};
#endif /* CONFIG_KONA_HEADSET_MULTI_BUTTON */

#ifdef CONFIG_DMAC_PL330
struct platform_device hawaii_pl330_dmac_device = {
	.name = "kona-dmac-pl330",
	.id = 0,
	.dev = {
		.coherent_dma_mask  = DMA_BIT_MASK(64),
	},
};
#endif

#ifdef CONFIG_BACKLIGHT_PWM
struct platform_device hawaii_backlight_device = {
	.name	= "pwm-backlight",
	.id	= 0,
};
#endif

static struct resource hawaii_pmu_resource = {
	.start = BCM_INT_ID_PMU_IRQ0,
	.end = BCM_INT_ID_PMU_IRQ0,
	.flags = IORESOURCE_IRQ,
};

struct platform_device pmu_device = {
	.name = "arm-pmu",
	.id = ARM_PMU_DEVICE_CPU,
	.resource = &hawaii_pmu_resource,
	.num_resources = 1,
};

static struct resource hawaii_pwm_resource = {
	.start = PWM_BASE_ADDR,
	.end = PWM_BASE_ADDR + SZ_4K - 1,
	.flags = IORESOURCE_MEM,
};

struct platform_device hawaii_pwm_device = {
	.name = "kona_pwmc",
	.id = -1,
	.resource = &hawaii_pwm_resource,
	.num_resources = 1,
};

static struct resource hawaii_ssp0_resource[] = {
	[0] = {
			.start = SSP0_BASE_ADDR,
			.end = SSP0_BASE_ADDR + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
	},
	[1] = {
			.start = BCM_INT_ID_SSP0,
			.end = BCM_INT_ID_SSP0,
			.flags = IORESOURCE_IRQ,
	},
};

struct platform_device hawaii_ssp0_device = {
	.name = "kona_sspi_spi",
	.id = 0,
	.resource = hawaii_ssp0_resource,
	.num_resources = ARRAY_SIZE(hawaii_ssp0_resource),
};

static struct resource hawaii_ssp1_resource[] = {
	[0] = {
			.start = SSP1_BASE_ADDR,
			.end = SSP1_BASE_ADDR + SZ_4K - 1,
			.flags = IORESOURCE_MEM,
	},
	[1] = {
			.start = BCM_INT_ID_SSP1,
			.end = BCM_INT_ID_SSP1,
			.flags = IORESOURCE_IRQ,
	},
};

struct platform_device hawaii_ssp1_device = {
	.name = "kona_sspi_spi",
	.id = 1,
	.resource = hawaii_ssp1_resource,
	.num_resources = ARRAY_SIZE(hawaii_ssp1_resource),
};

#ifdef CONFIG_SENSORS_KONA
static struct thermal_sensor_config sensor_data[] = {
	{			/* TMON sensor */
		.thermal_id = 1,
		.thermal_name = "tmon",
		.thermal_type = SENSOR_BB_TMON,
		.thermal_mc = 0,
		.thermal_read = SENSOR_READ_DIRECT,
		.thermal_location = 1,
		.thermal_warning_lvl_1 = 100000,
		.thermal_warning_lvl_2 = 110000,
		.thermal_fatal_lvl = 120000,
		.thermal_warning_action = THERM_ACTION_NOTIFY,
		.thermal_fatal_action = THERM_ACTION_NOTIFY_SHUTDOWN,
		.thermal_sensor_param = 0,
		.thermal_control = SENSOR_INTERRUPT,
		.convert_callback = NULL,
	},
	{			/* NTC (battery) sensor */
		.thermal_id = 2,
		.thermal_name = "battery",
		.thermal_type = SENSOR_BATTERY,
		.thermal_mc = 0,
		.thermal_read = SENSOR_READ_PMU_I2C,
		.thermal_location = 2,
		.thermal_warning_lvl_1 = 105000,
		.thermal_warning_lvl_2 = 115000,
		.thermal_fatal_lvl = 125000,
		.thermal_warning_action = THERM_ACTION_NOTIFY,
		.thermal_fatal_action = THERM_ACTION_NOTIFY_SHUTDOWN,
#ifdef CONFIG_MFD_BCM_PMU590XX
		.thermal_sensor_param = ADC_NTC_CHANNEL,
#else
		.thermal_sensor_param = PMU_ADC_NTC,
#endif
		.thermal_control = SENSOR_PERIODIC_READ,
		.convert_callback = NULL,
	},
	{			/* 32kHz crystal sensor */
		.thermal_id = 3,
		.thermal_name = "32k",
		.thermal_type = SENSOR_CRYSTAL,
		.thermal_mc = 0,
		.thermal_read = SENSOR_READ_PMU_I2C,
		.thermal_location = 3,
		.thermal_warning_lvl_1 = 106000,
		.thermal_warning_lvl_2 = 116000,
		.thermal_fatal_lvl = 126000,
		.thermal_warning_action = THERM_ACTION_NOTIFY,
		.thermal_fatal_action = THERM_ACTION_NOTIFY_SHUTDOWN,
#ifdef CONFIG_MFD_BCM_PMU590XX
		.thermal_sensor_param = ADC_32KTEMP_CHANNEL,
#else
		.thermal_sensor_param = PMU_ADC_32KTEMP,
#endif
		.thermal_control = SENSOR_PERIODIC_READ,
		.convert_callback = NULL,
	},
	{			/* PA sensor */
		.thermal_id = 4,
		.thermal_name = "PA",
		.thermal_type = SENSOR_PA,
		.thermal_mc = 0,
		.thermal_read = SENSOR_READ_PMU_I2C,
		.thermal_location = 4,
		.thermal_warning_lvl_1 = 107000,
		.thermal_warning_lvl_2 = 117000,
		.thermal_fatal_lvl = 127000,
		.thermal_warning_action = THERM_ACTION_NOTIFY,
		.thermal_fatal_action = THERM_ACTION_NOTIFY_SHUTDOWN,
#ifdef CONFIG_MFD_BCM_PMU590XX
		.thermal_sensor_param = ADC_PATEMP_CHANNEL,
#else
		.thermal_sensor_param = PMU_ADC_PATEMP,
#endif
		.thermal_control = SENSOR_PERIODIC_READ,
		.convert_callback = NULL,
	 }
};

static struct therm_data thermal_pdata = {
	.flags = 0,
	.thermal_update_interval = 0,
	.num_sensors = 4,
	.sensors = sensor_data,
};

static struct resource hawaii_tmon_resource[] = {
	{
		.start = TMON_BASE_ADDR,
		.end = TMON_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = BCM_INT_ID_TEMP_MON,
		.end = BCM_INT_ID_TEMP_MON,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device hawaii_tmon_device = {
	.name = "kona-tmon",
	.id = -1,
	.resource = hawaii_tmon_resource,
	.num_resources = ARRAY_SIZE(hawaii_tmon_resource),
};

static struct resource hawaii_thermal_resource[] = {
	{
		.start = TMON_BASE_ADDR,
		.end = TMON_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = BCM_INT_ID_TEMP_MON,
		.end = BCM_INT_ID_TEMP_MON,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device thermal_device = {
	.name = "kona-thermal",
	.id = -1,
	.resource = hawaii_thermal_resource,
	.num_resources = ARRAY_SIZE(hawaii_thermal_resource),
	.dev = {
		.platform_data = &thermal_pdata,
	},
};

#endif

#ifdef CONFIG_STM_TRACE
struct platform_device hawaii_stm_device = {
	.name = "stm",
	.id = -1,
};
#endif

#if defined(CONFIG_HW_RANDOM_KONA)
static struct resource rng_device_resource[] = {
	[0] = {
	       .start = SEC_RNG_BASE_ADDR,
	       .end = SEC_RNG_BASE_ADDR + 0x14,
	       .flags = IORESOURCE_MEM,
	},
	[1] = {
	       .start = BCM_INT_ID_SECURE_TRAP1,
	       .end = BCM_INT_ID_SECURE_TRAP1,
	       .flags = IORESOURCE_IRQ,
	},
};

struct platform_device rng_device = {
	.name = "kona_rng",
	.id = -1,
	.resource = rng_device_resource,
	.num_resources = ARRAY_SIZE(rng_device_resource),
};
#endif

#if defined(CONFIG_USB_DWC_OTG) || defined(CONFIG_USB_DWC_OTG_MODULE)
static struct resource hawaii_hsotgctrl_platform_resource[] = {
	[0] = {
	       .start = HSOTG_CTRL_BASE_ADDR,
	       .end = HSOTG_CTRL_BASE_ADDR + SZ_4K - 1,
	       .flags = IORESOURCE_MEM,
	},
	[1] = {
	       .start = CHIPREGS_BASE_ADDR,
	       .end = CHIPREGS_BASE_ADDR + SZ_4K - 1,
	       .flags = IORESOURCE_MEM,
	},
	[2] = {
	       .start = HUB_CLK_BASE_ADDR,
	       .end = HUB_CLK_BASE_ADDR + SZ_4K - 1,
	       .flags = IORESOURCE_MEM,
	},
	[3] = {
	       .start = BCM_INT_ID_HSOTG_WAKEUP,
	       .end = BCM_INT_ID_HSOTG_WAKEUP,
	       .flags = IORESOURCE_IRQ,
	},
};

struct platform_device hawaii_hsotgctrl_platform_device = {
	.name = "bcm_hsotgctrl",
	.id = -1,
	.resource = hawaii_hsotgctrl_platform_resource,
	.num_resources = ARRAY_SIZE(hawaii_hsotgctrl_platform_resource),
};

static struct resource hawaii_otg_platform_resource[] = {
	[0] = { /* Keep HSOTG_BASE_ADDR as first IORESOURCE_MEM to
				be compatible with legacy code */
	       .start = HSOTG_BASE_ADDR,
	       .end = HSOTG_BASE_ADDR + SZ_64K - 1,
	       .flags = IORESOURCE_MEM,
	},
	[1] = {
	       .start = BCM_INT_ID_USB_HSOTG,
	       .end = BCM_INT_ID_USB_HSOTG,
	       .flags = IORESOURCE_IRQ,
	},
};

struct platform_device hawaii_otg_platform_device = {
	.name = "dwc_otg",
	.id = -1,
	.resource = hawaii_otg_platform_resource,
	.num_resources = ARRAY_SIZE(hawaii_otg_platform_resource),
};
#endif

#ifdef CONFIG_KONA_CPU_FREQ_DRV
struct kona_freq_tbl kona_freq_tbl[] = {
	FTBL_INIT(312000, PI_OPP_ECONOMY),
	FTBL_INIT(666667, PI_OPP_NORMAL),
	FTBL_INIT(1000000, PI_OPP_TURBO),
};

void hawaii_cpufreq_init(void)
{
	struct clk *a9_pll_chnl0;
	struct clk *a9_pll_chnl1;
	a9_pll_chnl0 = clk_get(NULL, A9_PLL_CHNL0_CLK_NAME_STR);
	a9_pll_chnl1 = clk_get(NULL, A9_PLL_CHNL1_CLK_NAME_STR);

	BUG_ON(IS_ERR_OR_NULL(a9_pll_chnl0) || IS_ERR_OR_NULL(a9_pll_chnl1));

	/*Update DVFS freq table based on PLL settings done by the loader */
	/*For B0 and above, ECONOMY:0 NORMAL:1 TURBO:2 */
	kona_freq_tbl[1].cpu_freq = clk_get_rate(a9_pll_chnl0) / 1000;
	kona_freq_tbl[2].cpu_freq = clk_get_rate(a9_pll_chnl1) / 1000;

	pr_info("%s a9_pll_chnl0 freq = %dKhz a9_pll_chnl1 freq = %dKhz\n",
		__func__, kona_freq_tbl[1].cpu_freq, kona_freq_tbl[2].cpu_freq);
}

struct kona_cpufreq_drv_pdata kona_cpufreq_drv_pdata = {

	.freq_tbl = kona_freq_tbl,
	.num_freqs = ARRAY_SIZE(kona_freq_tbl),
	/*FIX ME: To be changed according to the cpu latency */
	.latency = 10 * 1000,
	.pi_id = PI_MGR_PI_ID_ARM_CORE,
	.cpufreq_init = hawaii_cpufreq_init,
};

struct platform_device kona_cpufreq_device = {
	.name = "kona-cpufreq-drv",
	.id = -1,
	.dev = {
		.platform_data = &kona_cpufreq_drv_pdata,
	},
};
#endif /*CONFIG_KONA_CPU_FREQ_DRV */

#ifdef CONFIG_KONA_AVS
struct platform_device kona_avs_device = {
	.name = "kona-avs",
	.id = -1,
};
#endif

#ifdef CONFIG_KONA_AVS
void avs_silicon_type_notify(u32 silicon_type, int freq_id)
{
	pr_info("%s : silicon type = %d freq = %d\n", __func__,
			silicon_type,
			freq_id);
	pm_init_pmu_sr_vlt_map_table(silicon_type);
}

u32 svt_pmos_bin[3 + 1] = { 125, 146, 171, 201 };
u32 svt_nmos_bin[3 + 1] = { 75, 96, 126, 151 };

u32 lvt_pmos_bin[3 + 1] = { 150, 181, 216, 251 };
u32 lvt_nmos_bin[3 + 1] = { 90, 111, 146, 181 };

u32 svt_silicon_type_lut[3 * 3] = {
	SILICON_TYPE_SLOW, SILICON_TYPE_SLOW, SILICON_TYPE_TYPICAL,
	SILICON_TYPE_SLOW, SILICON_TYPE_TYPICAL, SILICON_TYPE_TYPICAL,
	SILICON_TYPE_TYPICAL, SILICON_TYPE_TYPICAL, SILICON_TYPE_FAST
};

u32 lvt_silicon_type_lut[3 * 3] = {
	SILICON_TYPE_SLOW, SILICON_TYPE_SLOW, SILICON_TYPE_TYPICAL,
	SILICON_TYPE_SLOW, SILICON_TYPE_TYPICAL, SILICON_TYPE_TYPICAL,
	SILICON_TYPE_TYPICAL, SILICON_TYPE_TYPICAL, SILICON_TYPE_FAST
};
#endif

#ifdef CONFIG_KONA_MEMC
struct kona_memc_pdata kmemc_plat_data = {
	.flags = KONA_MEMC_ENABLE_SELFREFRESH | KONA_MEMC_DISABLE_DDRLDO |
		KONA_MEMC_SET_SEQ_BUSY_CRITERIA | KONA_MEMC_DDR_PLL_PWRDN_EN |
		KONA_MEMC_HW_FREQ_CHANGE_EN,
	.memc0_ns_base = KONA_MEMC0_NS_VA,
	.chipreg_base = KONA_CHIPREG_VA,
	.seq_busy_val = 2,
	.max_pwr = 3,
};

struct platform_device kona_memc_device = {
	.name = "kona_memc",
	.id = -1,
	.dev = {
		.platform_data = &kmemc_plat_data,
	},
};
#endif

#ifdef CONFIG_UNICAM
/* Remove this comment once the unicam data is updated for Hawaii*/
static struct kona_unicam_platform_data unicam_pdata = {
	.csi0_gpio = 12,
	.csi1_gpio = 13,
};
#endif

#if defined(CONFIG_CRYPTO_DEV_BRCM_SPUM_HASH)
static struct resource hawaii_spum_resource[] = {
	[0] = {
	       .start = SEC_SPUM_NS_APB_BASE_ADDR,
	       .end = SEC_SPUM_NS_APB_BASE_ADDR + SZ_64K - 1,
	       .flags = IORESOURCE_MEM,
	},
	[1] = {
	       .start = SPUM_NS_BASE_ADDR,
	       .end = SPUM_NS_BASE_ADDR + SZ_64K - 1,
	       .flags = IORESOURCE_MEM,
	}
};

struct platform_device hawaii_spum_device = {
	.name = "brcm-spum",
	.id = 0,
	.resource = hawaii_spum_resource,
	.num_resources = ARRAY_SIZE(hawaii_spum_resource),
};
#endif

#if defined(CONFIG_CRYPTO_DEV_BRCM_SPUM_AES)
static struct resource hawaii_spum_aes_resource[] = {
	[0] = {
	       .start = SEC_SPUM_NS_APB_BASE_ADDR,
	       .end = SEC_SPUM_NS_APB_BASE_ADDR + SZ_64K - 1,
	       .flags = IORESOURCE_MEM,
	},
	[1] = {
	       .start = SPUM_NS_BASE_ADDR,
	       .end = SPUM_NS_BASE_ADDR + SZ_64K - 1,
	       .flags = IORESOURCE_MEM,
	}
};

struct platform_device hawaii_spum_aes_device = {
	.name = "brcm-spum-aes",
	.id = 0,
	.resource = hawaii_spum_aes_resource,
	.num_resources = ARRAY_SIZE(hawaii_spum_aes_resource),
};
#endif

#ifdef CONFIG_UNICAM
struct platform_device hawaii_unicam_device = {
	.name = "kona-unicam",
	.id = 1,
	.dev = {
		.platform_data = &unicam_pdata,
	},
};
#endif

#ifdef CONFIG_VIDEO_UNICAM_CAMERA
static u64 unicam_camera_dma_mask = DMA_BIT_MASK(32);

static struct resource hawaii_unicam_resource[] = {
	[0] = {
	       .start = BCM_INT_ID_CSI,
	       .end = BCM_INT_ID_CSI,
	       .flags = IORESOURCE_IRQ,
	},
};

struct platform_device hawaii_camera_device = {
	.name = "unicam-camera",
	.id = 0,
	.resource = hawaii_unicam_resource,
	.num_resources = ARRAY_SIZE(hawaii_unicam_resource),
	.dev = {
		.dma_mask = &unicam_camera_dma_mask,
		.coherent_dma_mask = 0xffffffff,
	},
};
#endif

#ifdef CONFIG_SND_BCM_SOC
struct platform_device caph_i2s_device = {
	.name = "caph-i2s",
};

struct platform_device caph_pcm_device = {
	.name = "caph-pcm-audio",
};
#endif

static int __init setup_etm(char *p)
{
	get_option(&p, &etm_on);
	return 1;
}
early_param("etm_on", setup_etm);


#ifdef CONFIG_ANDROID_PMEM
static int __init setup_pmem_pages(char *str)
{
	char *endp = NULL;
	if (str) {
		android_pmem_data.cmasize = memparse((const char *)str, &endp);
		printk(KERN_INFO "PMEM size is 0x%08x Bytes\n",
		       (unsigned int)android_pmem_data.cmasize);
	} else {
		printk(KERN_EMERG "\"pmem=\" option is not set!!!\n");
		printk(KERN_EMERG "Unable to determine the memory region for pmem!!!\n");
	}
	return 0;
}
early_param("pmem", setup_pmem_pages);

static int __init setup_pmem_carveout_pages(char *str)
{
	char *endp = NULL;
	phys_addr_t carveout_size = 0;
	if (str) {
		carveout_size = memparse((const char *)str, &endp);
		if (carveout_size & (PAGE_SIZE - 1)) {
			printk(KERN_INFO"carveout size is not aligned to 0x%08x\n",
					(1 << MAX_ORDER));
			carveout_size = ALIGN(carveout_size, PAGE_SIZE);
			printk(KERN_INFO"Aligned carveout size is 0x%08x\n",
					carveout_size);
		}
		printk(KERN_INFO"PMEM: Carveout Mem (0x%08x)\n", carveout_size);
	} else {
		printk(KERN_EMERG"PMEM: Invalid \"carveout=\" value.\n");
	}

	if (carveout_size)
		android_pmem_data.carveout_size = carveout_size;

	return 0;
}
early_param("carveout", setup_pmem_carveout_pages);

static void __init pmem_reserve_memory(void)
{
	int err;
	phys_addr_t carveout_size, carveout_base;
	unsigned long cmasize;

	carveout_size = android_pmem_data.carveout_size;
	cmasize = android_pmem_data.cmasize;
	carveout_base = android_pmem_data.carveout_base;

	if (carveout_size) {
		do {
			carveout_base = memblock_alloc_from_range(
				carveout_size, SZ_16M, carveout_base,
			carveout_base + carveout_size);

			if (!carveout_base) {
				pr_err("FATAL: PMEM: unable to");
				pr_err(" carveout at 0x%x\n",
					android_pmem_data.carveout_base);
				break;
			}

			if (carveout_base !=
				android_pmem_data.carveout_base) {
				pr_err("PMEM: Requested block at 0x%x,",
					android_pmem_data.carveout_base);
				pr_err(" but got 0x%x", carveout_base);
			}

			memblock_free(carveout_base, carveout_size);
			err = memblock_remove(carveout_base, carveout_size);
			if (!err) {
				printk(KERN_INFO"PMEM: Carve memory from (%08x-%08x)\n",
						carveout_base,
						carveout_base + carveout_size);
				android_pmem_data.carveout_base = carveout_base;
			} else {
				printk(KERN_INFO"PMEM: Carve out memory failed\n");
			}
		} while (0);
	}

	if (dma_declare_contiguous(&android_pmem.dev, cmasize, 0, 0)) {
		printk(KERN_ERR"PMEM: Failed to reserve CMA region\n");
		android_pmem_data.cmasize = 0;
	}
}
#endif

#ifdef CONFIG_ION
static int __init setup_ion_pages(char *str, struct ion_platform_data *pdata,
		int idx)
{
	struct ion_platform_heap *heap;
	char *endp = NULL;

	if (str && (pdata->nr > idx)) {
		heap = &(pdata->heaps[idx]);
		heap->size = memparse((const char *)str, &endp);
		pr_info("ion: Cmdline sets %16s heap, id(%d) size(%d)MB\n",
				heap->name, heap->id, (heap->size)>>20);
	}

	return 0;
}

static int __init setup_ion_carveout0_pages(char *str)
{
	return setup_ion_pages(str, &ion_carveout_data, 0);
}
early_param("carveout0", setup_ion_carveout0_pages);

static int __init setup_ion_carveout1_pages(char *str)
{
	return setup_ion_pages(str, &ion_carveout_data, 1);
}
early_param("carveout1", setup_ion_carveout1_pages);

#ifdef CONFIG_CMA
static int __init setup_ion_cma0_pages(char *str)
{
	return setup_ion_pages(str, &ion_cma_data, 0);
}
early_param("cma0", setup_ion_cma0_pages);
#endif

static phys_addr_t __init ion_find_memory_base(struct ion_platform_heap *heap,
		unsigned char *heap_type, int idx)
{
	phys_addr_t base;

	pr_info("ion: Try %8s %16s(%d) id(%d) size(%3d)MB base(%08x) limit(%08x)\n",
			heap_type, heap->name, idx, heap->id, (heap->size)>>20,
			(u32)heap->base, (u32)heap->limit);
	base = memblock_alloc_from_range(
			heap->size, SZ_1M, heap->base,
			heap->limit);
	if (base)
		memblock_free(base, heap->size);
	else
		pr_err("ion: Failed Reserving  %16s(%d)\n", heap->name, idx);

	return base;
}

static void __init ion_reserve_memory(void)
{
	struct ion_platform_heap *heap;
	phys_addr_t base;
	int i;

	/* Carveout memory for ION */
	for (i = 0; i < ion_carveout_data.nr; i++) {
		heap = &ion_carveout_data.heaps[i];
		if (heap->size) {
			base = ion_find_memory_base(heap, "Carveout", i);
			if (base) {
				memblock_remove(base, heap->size);
				pr_info("ion: %16s %3dMB (%08x-%08x)\n",
					heap->name, (heap->size>>20),
					base, base+heap->size);
				heap->base = base;
				continue;
			}
		}
		heap->id = ION_INVALID_HEAP_ID;
	}

#ifdef CONFIG_CMA
	/* Reserve CMA memory for ION */
	for (i = 0; i < ion_cma_data.nr; i++) {
		heap = &ion_cma_data.heaps[i];
		if (heap->size) {
			base = ion_find_memory_base(heap, "CMA", i);
			if (base) {
				if (!dma_declare_contiguous(&ion_cma_device.dev,
							heap->size, base,
							heap->limit)) {
					pr_info("ion: %16s %3dMB (%08x-%08x)\n",
							heap->name,
							(heap->size>>20),
							base, base+heap->size);
					heap->base = base;
					continue;
				} else {
					pr_info("ion: Failed CMA %3dMB (%08x-%08x)\n",
							(heap->size>>20),
							base, base+heap->size);
				}
			}
		}
		heap->id = ION_INVALID_HEAP_ID;
	}
#endif
}
#endif

void __init hawaii_reserve(void)
{

#ifdef CONFIG_ION
	ion_reserve_memory();
#endif

#ifdef CONFIG_ANDROID_PMEM
	pmem_reserve_memory();
#endif

}
