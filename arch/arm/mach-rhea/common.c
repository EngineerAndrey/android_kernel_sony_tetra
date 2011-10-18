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
#include <linux/serial_8250.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <mach/hardware.h>
#include <linux/i2c.h>
#include <linux/i2c-kona.h>
#include <mach/kona.h>
#include <mach/rhea.h>
#include <mach/rdb/brcm_rdb_uartb.h>
#include <asm/mach/map.h>
#include <linux/broadcom/ipcinterface.h>
#include <asm/pmu.h>
#include <linux/spi/spi.h>
#include <plat/spi_kona.h>
#include <plat/chal/chal_trace.h>
#include <trace/stm.h>
#include <linux/usb/android_composite.h>

#ifdef CONFIG_KONA_AVS
#include <plat/kona_avs.h>
#endif

#ifdef CONFIG_UNICAM
#include <plat/kona_unicam.h>
#endif

#ifdef CONFIG_KONA_POWER_MGR
#include <plat/pwr_mgr.h>

#define VLT_LUT_SIZE 16
#endif

#include <plat/bcm_pwm_block.h>
/*
 * todo: 8250 driver has problem autodetecting the UART type -> have to
 * use FIXED type
 * confuses it as an XSCALE UART.  Problem seems to be that it reads
 * bit6 in IER as non-zero sometimes when it's supposed to be 0.
 */
#define KONA_UART0_PA	UARTB_BASE_ADDR
#define KONA_UART1_PA	UARTB2_BASE_ADDR
#define KONA_UART2_PA	UARTB3_BASE_ADDR



#define PID_PLATFORM				0xE600
#define FD_MASS_PRODUCT_ID			0x0001
#define FD_SICD_PRODUCT_ID			0x0002
#define FD_VIDEO_PRODUCT_ID			0x0004
#define FD_DFU_PRODUCT_ID			0x0008
#define FD_MTP_ID					0x000C
#define FD_CDC_ACM_PRODUCT_ID		0x0020
#define FD_CDC_RNDIS_PRODUCT_ID		0x0040
#define FD_CDC_OBEX_PRODUCT_ID		0x0080


#define	BRCM_VENDOR_ID				0x0a5c
#define	BIG_ISLAND_PRODUCT_ID		0x2816

/* FIXME borrow Google Nexus One ID to use windows driver */
#define	GOOGLE_VENDOR_ID			0x18d1
#define	NEXUS_ONE_PROD_ID			0x0d02

#define	VENDOR_ID					GOOGLE_VENDOR_ID
#define	PRODUCT_ID					NEXUS_ONE_PROD_ID

/* use a seprate PID for RNDIS */
#define RNDIS_PRODUCT_ID			0x4e13
#define ACM_PRODUCT_ID				0x8888
#define OBEX_PRODUCT_ID				0x685E


#define KONA_8250PORT(name,clk)				\
{								\
	.membase    = (void __iomem *)(KONA_##name##_VA), 	\
	.mapbase    = (resource_size_t)(KONA_##name##_PA),    	\
	.irq	    = BCM_INT_ID_##name,               		\
	.uartclk    = 26000000,					\
	.regshift   = 2,					\
	.iotype	    = UPIO_DWAPB,					\
	.type	    = PORT_16550A,          			\
	.flags	    = UPF_BOOT_AUTOCONF | UPF_FIXED_TYPE | UPF_SKIP_TEST,	\
	.private_data = (void __iomem *)((KONA_##name##_VA) + UARTB_USR_OFFSET), \
	.clk_name = clk,	\
}

static struct plat_serial8250_port uart_data[] = {
	KONA_8250PORT(UART0,"uartb_clk"),
	KONA_8250PORT(UART1,"uartb2_clk"),
	KONA_8250PORT(UART2,"uartb3_clk"),
	{
		.flags		= 0,
	},
};

static struct platform_device board_serial_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
		.platform_data = uart_data,
	},
};

static char *android_function_rndis[] = {
#ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
};

static char *android_function_acm[] = {
#ifdef CONFIG_USB_ANDROID_ACM
	"acm",
	"acm1",
#endif
};

static char *android_function_msc_acm[] = {
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
	"usb_mass_storage",
#endif
#ifdef CONFIG_USB_ANDROID_ACM
	"acm",
	"acm1",
#endif
};

static char *android_function_obex[] = {
#ifdef CONFIG_USB_ANDROID_OBEX
	"obex",
#endif
};

static char *android_function_adb_msc[] = {
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
	"usb_mass_storage",
#endif
#ifdef CONFIG_USB_ANDROID_ADB
	"adb",
#endif
};

static char *android_functions_all[] = {
#ifdef CONFIG_USB_ANDROID_MASS_STORAGE
	"usb_mass_storage",
#endif
#ifdef CONFIG_USB_ANDROID_ADB
	"adb",
#endif
#ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
#ifdef CONFIG_USB_ANDROID_ACM
	"acm",
#endif
#ifdef CONFIG_USB_ANDROID_OBEX
	"obex",
#endif
};


static struct usb_mass_storage_platform_data android_mass_storage_pdata = {
#ifdef CONFIG_USB_DUAL_DISK_SUPPORT
	.nluns		=	2,
#else
	.nluns		=	1,
#endif
	.vendor		=	"Broadcom",
	.product	=	"Rhea",
	.release	=	0x0100
};

static struct platform_device android_mass_storage_device = {
	.name	=	"usb_mass_storage",
	.id	=	-1,
	.dev	=	{
		.platform_data	=	&android_mass_storage_pdata,
	}
};

static struct usb_ether_platform_data android_rndis_pdata = {
        /* ethaddr FIXME */
        .vendorID       = __constant_cpu_to_le16(VENDOR_ID),
        .vendorDescr    = "Broadcom RNDIS",
};

static struct platform_device android_rndis_device = {
        .name   = "rndis",
        .id     = -1,
        .dev    = {
                .platform_data = &android_rndis_pdata,
        },
};

static struct android_usb_product android_products[] = {
	{
		.product_id	= 	__constant_cpu_to_le16(PRODUCT_ID),
		.num_functions	=	ARRAY_SIZE(android_function_adb_msc),
		.functions	=	android_function_adb_msc,
	},
	{
		.product_id	= 	__constant_cpu_to_le16(PID_PLATFORM | FD_CDC_RNDIS_PRODUCT_ID),
		.num_functions	=	ARRAY_SIZE(android_function_rndis),
		.functions	=	android_function_rndis,
	},
	{
		.product_id	= 	__constant_cpu_to_le16(PID_PLATFORM | FD_CDC_ACM_PRODUCT_ID),
		.num_functions	=	ARRAY_SIZE(android_function_acm),
		.functions	=	android_function_acm,
	},
	{
		.product_id =	__constant_cpu_to_le16(PID_PLATFORM | FD_CDC_ACM_PRODUCT_ID | FD_MASS_PRODUCT_ID),
		.num_functions	=	ARRAY_SIZE(android_function_msc_acm),
		.functions	=	android_function_msc_acm,
	},
	{
		.product_id =	__constant_cpu_to_le16(PID_PLATFORM | FD_CDC_OBEX_PRODUCT_ID),
		.num_functions	=	ARRAY_SIZE(android_function_obex),
		.functions	=	android_function_obex,
	},
};

static struct android_usb_platform_data android_usb_data = {
	.vendor_id		= 	__constant_cpu_to_le16(VENDOR_ID),
	.product_id		=	__constant_cpu_to_le16(PRODUCT_ID),
	.version		=	0,
	.product_name		=	"Rhea",
	.manufacturer_name	= 	"Broadcom",
	.serial_number		=	"0123456789ABCDEF",

	.num_products		=	ARRAY_SIZE(android_products),
	.products		=	android_products,

	.num_functions		=	ARRAY_SIZE(android_functions_all),
	.functions		=	android_functions_all,
};

static struct platform_device android_usb = {
	.name 	= "android_usb",
	.id	= 1,
	.dev	= {
		.platform_data = &android_usb_data,
	},
};



static struct resource board_i2c0_resource[] = {
	[0] =
	{
		.start = BSC1_BASE_ADDR,
		.end = BSC1_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] =
	{
		.start = BCM_INT_ID_I2C0,
		.end = BCM_INT_ID_I2C0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource board_i2c1_resource[] = {
	[0] =
	{
		.start = BSC2_BASE_ADDR,
		.end = BSC2_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] =
	{
		.start = BCM_INT_ID_I2C1,
		.end = BCM_INT_ID_I2C1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource board_pmu_bsc_resource[] = {
	[0] =
	{
		.start = PMU_BSC_BASE_ADDR,
		.end = PMU_BSC_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] =
	{
		.start = BCM_INT_ID_PM_I2C,
		.end = BCM_INT_ID_PM_I2C,
		.flags = IORESOURCE_IRQ,
	},
};

static struct bsc_adap_cfg bsc_i2c_cfg[] = {
	{ /* for BSC0 */
		.speed = BSC_BUS_SPEED_50K,
		.dynamic_speed = 1,
		.bsc_clk = "bsc1_clk",
		.bsc_apb_clk = "bsc1_apb_clk",
		.retries = 1,
	},
	{ /* for BSC1*/
		.speed = BSC_BUS_SPEED_50K,
		.dynamic_speed = 1,
		.bsc_clk = "bsc2_clk",
		.bsc_apb_clk = "bsc2_apb_clk",
		.retries = 3,
	},
	{ /* for PMU */
		.speed = BSC_BUS_SPEED_50K,
		.dynamic_speed = 1,
		.bsc_clk = "pmu_bsc_clk",
		.bsc_apb_clk = "pmu_bsc_apb",
		.retries = 1,
	},
};

static struct platform_device board_i2c_adap_devices[] =
{
	{  /* for BSC0 */
		.name = "bsc-i2c",
		.id = 0,
		.resource = board_i2c0_resource,
		.num_resources	= ARRAY_SIZE(board_i2c0_resource),
		.dev      = {
			.platform_data = &bsc_i2c_cfg[0],
		},
	},
	{  /* for BSC1 */
		.name = "bsc-i2c",
		.id = 1,
		.resource = board_i2c1_resource,
		.num_resources	= ARRAY_SIZE(board_i2c1_resource),
		.dev	  = {
			.platform_data = &bsc_i2c_cfg[1],
		},

	},
	{  /* for PMU BSC */
		.name = "bsc-i2c",
		.id = 2,
		.resource = board_pmu_bsc_resource,
		.num_resources	= ARRAY_SIZE(board_pmu_bsc_resource),
		.dev      = {
			.platform_data = &bsc_i2c_cfg[2],
		},
	},
};

/* ARM performance monitor unit */
static struct resource pmu_resource = {
       .start = BCM_INT_ID_PMU_IRQ0,
       .end = BCM_INT_ID_PMU_IRQ0,
       .flags = IORESOURCE_IRQ,
};

static struct platform_device pmu_device = {
       .name = "arm-pmu",
       .id   = ARM_PMU_DEVICE_CPU,
       .resource = &pmu_resource,
       .num_resources = 1,
};

// PWM configuration.
static struct resource kona_pwm_resource = {
                .start = PWM_BASE_ADDR,
                .end = PWM_BASE_ADDR + SZ_4K - 1,
                .flags = IORESOURCE_MEM,
};

static struct pwm_platform_data pwm_dev = {
        .max_pwm_id = 6,
        .syscfg_inf = NULL,
};

void set_pwm_board_sysconfig(int (*syscfg_inf) (uint32_t module, uint32_t op))
{
	pwm_dev.syscfg_inf = syscfg_inf;
}

static struct platform_device kona_pwm_device = {
		.dev = {
			.platform_data = &pwm_dev, 
		},
                .name = "kona_pwmc",
                .id = -1,
                .resource = &kona_pwm_resource,
                .num_resources  = 1,
};

/* SPI configuration */
static struct resource kona_sspi_spi0_resource[] = {
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

static struct spi_kona_platform_data sspi_spi0_info = {
	.enable_dma = 1,
	.cs_line = 1,
	.mode = SPI_LOOP | SPI_MODE_3,
};

static struct platform_device kona_sspi_spi0_device = {
	.dev = {
		.platform_data = &sspi_spi0_info,
	},
	.name = "kona_sspi_spi",
	.id = 0,
	.resource = kona_sspi_spi0_resource,
	.num_resources  = ARRAY_SIZE(kona_sspi_spi0_resource),
};

#ifdef CONFIG_SENSORS_KONA
static struct resource board_tmon_resource[] = {
	{	/* For Current Temperature */
		.start = TMON_BASE_ADDR,
		.end = TMON_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{	/* For Temperature IRQ */
		.start = BCM_INT_ID_TEMP_MON,
		.end = BCM_INT_ID_TEMP_MON,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device tmon_device = {
	.name = "kona-tmon",
	.id = -1,
	.resource = board_tmon_resource,
	.num_resources = ARRAY_SIZE(board_tmon_resource),
};
#endif

#ifdef CONFIG_STM_TRACE
static struct stm_platform_data stm_pdata = {
	.regs_phys_base       = STM_BASE_ADDR,
	.channels_phys_base   = SWSTM_BASE_ADDR,
	.id_mask              = 0x0,   /* Skip ID check/match */
	.final_funnel	      = CHAL_TRACE_FIN_FUNNEL,
};

struct platform_device kona_stm_device = {
	.name = "stm",
	.id = -1,
	.dev = {
	        .platform_data = &stm_pdata,
	},
};
#endif

#if defined(CONFIG_HW_RANDOM_KONA)
static struct resource rng_device_resource[] = {
	[0] = {
		.start = SEC_RNG_BASE_ADDR,
		.end   = SEC_RNG_BASE_ADDR + 0x14,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = BCM_INT_ID_SECURE_TRAP1,
		.end   = BCM_INT_ID_SECURE_TRAP1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device rng_device =
{
	.name			= "kona_rng",
	.id				= -1,
	.resource	  = rng_device_resource,
	.num_resources = ARRAY_SIZE(rng_device_resource),
};
#endif

#ifdef CONFIG_USB
static struct resource kona_hsotgctrl_platform_resource[] = {
	[0] = {
		.start = HSOTG_CTRL_BASE_ADDR,
		.end = HSOTG_CTRL_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device board_kona_hsotgctrl_platform_device =
{
	.name = "bcm_hsotgctrl",
	.id = -1,
	.resource = kona_hsotgctrl_platform_resource,
	.num_resources = ARRAY_SIZE(kona_hsotgctrl_platform_resource),
};
#endif

#ifdef CONFIG_USB_DWC_OTG
static struct resource kona_otg_platform_resource[] = {
	[0] = { /* Keep HSOTG_BASE_ADDR as first IORESOURCE_MEM to be compatible with legacy code */
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

static struct platform_device board_kona_otg_platform_device =
{
	.name = "dwc_otg",
	.id = -1,
	.resource = kona_otg_platform_resource,
	.num_resources = ARRAY_SIZE(kona_otg_platform_resource),
};
#endif

#ifdef CONFIG_KONA_AVS

void avs_silicon_type_notify(u32 silicon_type)
{
#ifdef CONFIG_KONA_POWER_MGR
	u8* volt_lut = kona_avs_get_volt_table();
	BUG_ON(volt_lut == NULL);
/*re-program volt lookup table based on silicon type*/
	pwr_mgr_pm_i2c_var_data_write(volt_lut,VLT_LUT_SIZE);
#endif
	pr_info("%s:silicon_type = %d\n",__func__,silicon_type);
}

static u32 svt_pmos_bin[3+1] = {125,146,171,201};
static u32 svt_nmos_bin[3+1] = {75,96,126,151};

static u32 lvt_pmos_bin[3+1] = {150,181,216,251};
static u32 lvt_nmos_bin[3+1] = {90,111,146,181};

u32 svt_silicon_type_lut[3*3] =
	{
		SILICON_TYPE_SLOW,SILICON_TYPE_SLOW,SILICON_TYPE_TYPICAL,
		SILICON_TYPE_SLOW,SILICON_TYPE_TYPICAL,SILICON_TYPE_TYPICAL,
		SILICON_TYPE_TYPICAL,SILICON_TYPE_TYPICAL,SILICON_TYPE_FAST
	};

u32 lvt_silicon_type_lut[3*3] =
	{
		SILICON_TYPE_SLOW,SILICON_TYPE_SLOW,SILICON_TYPE_TYPICAL,
		SILICON_TYPE_SLOW,SILICON_TYPE_TYPICAL,SILICON_TYPE_TYPICAL,
		SILICON_TYPE_TYPICAL,SILICON_TYPE_TYPICAL,SILICON_TYPE_FAST
	};

u8 ss_vlt_tbl[] = {0x3, 0x3, 0x4, 0x4, 0x4, 0xe, 0xe, 0xe,
						0xe, 0xe, 0xe, 0xe, 0x13, 0x13, 0x13, 0x13};

u8 tt_vlt_tbl[] = {0x3, 0x3, 0x4, 0x4, 0x4, 0xb, 0xb, 0xb,
							0xb, 0xb, 0xb, 0xb,  0xd,  0xd,  0xd, 0xd};

u8 ff_vlt_tbl[] = { 0x3, 0x3, 0x4, 0x4,0x4, 0x4, 0xb, 0xb,
							0xb, 0xb, 0xb, 0xb, 0xb, 0xd, 0xd, 0xd };
static u8* volt_table[] = {ss_vlt_tbl, tt_vlt_tbl, ff_vlt_tbl};

static struct kona_avs_pdata avs_pdata =
{
	.flags = AVS_TYPE_OPEN|AVS_READ_FROM_MEM,
	.param = 0x3404BFA8, /*AVS_READ_FROM_MEM - Address location where monitor values are copied by ABI */
	.nmos_bin_size = 3,
	.pmos_bin_size = 3,

	.svt_pmos_bin = svt_pmos_bin,
	.svt_nmos_bin = svt_nmos_bin,

	.lvt_pmos_bin = lvt_pmos_bin,
	.lvt_nmos_bin = lvt_nmos_bin,

	.svt_silicon_type_lut = svt_silicon_type_lut,
	.lvt_silicon_type_lut = lvt_silicon_type_lut,

	.volt_table = volt_table,

	.silicon_type_notify = avs_silicon_type_notify,
};

struct platform_device kona_avs_device = {
	.name = "kona-avs",
	.id = -1,
	.dev = {
	        .platform_data = &avs_pdata,
	},
};

#endif

#if defined(CONFIG_CRYPTO_DEV_BRCM_SPUM_HASH)
static struct resource board_spum_resource[] = {
       [0] =
       {
               .start  =       SEC_SPUM_NS_APB_BASE_ADDR,
               .end    =       SEC_SPUM_NS_APB_BASE_ADDR + SZ_64K - 1,
               .flags  =       IORESOURCE_MEM,
       },
       [1] =
       {
               .start  =       SPUM_NS_BASE_ADDR,
               .end    =       SPUM_NS_BASE_ADDR + SZ_64K - 1,
               .flags  =       IORESOURCE_MEM,
       }
};

static struct platform_device board_spum_device = {
       .name           =       "brcm-spum",
       .id             =       0,
       .resource       =       board_spum_resource,
       .num_resources  =       ARRAY_SIZE(board_spum_resource),
#endif

#ifdef CONFIG_UNICAM
static struct kona_unicam_platform_data unicam_pdata =
{
	.csi0_gpio = 12,
	.csi1_gpio = 13,
};

static struct platform_device board_unicam_device = {
	.name = "kona-unicam",
	.id = 1,
	.dev      = {
		.platform_data = &unicam_pdata,
	},
};
#endif

/* Common devices among all the Rhea boards (Rhea Ray, Rhea Berri, etc.) */
static struct platform_device *board_common_plat_devices[] __initdata = {
	&board_serial_device,
	&board_i2c_adap_devices[0],
	&board_i2c_adap_devices[1],
	&board_i2c_adap_devices[2],
	&android_rndis_device,
	&android_mass_storage_device,
	&android_usb,
	&pmu_device,
	&kona_pwm_device,
	&kona_sspi_spi0_device,
#ifdef CONFIG_SENSORS_KONA
	&tmon_device,
#endif
#ifdef CONFIG_STM_TRACE
	&kona_stm_device,
#endif
#if defined(CONFIG_HW_RANDOM_KONA)
	&rng_device,
#endif
#ifdef CONFIG_USB
	&board_kona_hsotgctrl_platform_device,
#endif
#ifdef CONFIG_USB_DWC_OTG
	&board_kona_otg_platform_device,
#endif

#ifdef CONFIG_KONA_AVS
	&kona_avs_device,
#endif

#ifdef CONFIG_CRYPTO_DEV_BRCM_SPUM_HASH
       &board_spum_device,
#endif

#ifdef CONFIG_UNICAM
       &board_unicam_device,
#endif
};




void __init board_add_common_devices(void)
{
	platform_add_devices(board_common_plat_devices, ARRAY_SIZE(board_common_plat_devices));
}
