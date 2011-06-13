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
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/island_keypad.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/kona.h>
#include <mach/rdb/brcm_rdb_uartb.h>
#include <mach/irqs.h>

#define KONA_UART0_PA   UARTB_BASE_ADDR
#define KONA_UART1_PA   UARTB2_BASE_ADDR
#define KONA_UART2_PA   UARTB3_BASE_ADDR
#define KONA_UART3_PA   UARTB4_BASE_ADDR

#define KONA_8250PORT(name)                                                   \
{                                                                             \
   .membase    = (void __iomem *)(KONA_##name##_VA),                          \
   .mapbase    = (resource_size_t)(KONA_##name##_PA),                         \
   .irq        = BCM_INT_ID_##name,                                           \
   .uartclk    = 13000000,                                                    \
   .regshift   = 2,                                                           \
   .iotype     = UPIO_DWAPB,                                                  \
   .type       = PORT_16550A,                                                 \
   .flags      = UPF_BOOT_AUTOCONF | UPF_FIXED_TYPE | UPF_SKIP_TEST,          \
   .private_data = (void __iomem *)((KONA_##name##_VA) + UARTB_USR_OFFSET),   \
}

static struct plat_serial8250_port uart_data[] = {
	KONA_8250PORT(UART0),
	KONA_8250PORT(UART1),
	KONA_8250PORT(UART2),
	KONA_8250PORT(UART3),
	{ .flags = 0, },
};

static struct platform_device board_serial_device = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = uart_data,
	},
};

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
   .name          = "kona_rng",
   .id            = -1,
   .resource	  = rng_device_resource,
   .num_resources = ARRAY_SIZE(rng_device_resource),
};
#endif

#if defined(CONFIG_KONA_PWMC)
static struct resource pwm_device_resource[] = {
    [0] = {
        .start = PWM_BASE_ADDR,
        .end   = PWM_BASE_ADDR + 0x10,
        .flags = IORESOURCE_MEM,
    },
};

static struct platform_device pwm_device =
{
   .name          = "kona_pwmc",
   .id            = -1,
   .resource	  = pwm_device_resource,
   .num_resources = ARRAY_SIZE(pwm_device_resource),
};
#endif

#if defined(CONFIG_W1_MASTER_DS1WM)
static struct resource d1w_device_resource[] = {
    [0] = {
        .start = D1W_BASE_ADDR,
        .end   = D1W_BASE_ADDR + 0x10,
        .flags = IORESOURCE_MEM,
    },
    [1] = {
        .start = BCM_INT_ID_DALLAS_1_WIRE,
        .end   = BCM_INT_ID_DALLAS_1_WIRE,
        .flags = IORESOURCE_IRQ,
    },
};

static struct platform_device d1w_device =
{
   .name          = "ds1wm",
   .id            = -1,
   .resource	  = d1w_device_resource,
   .num_resources = ARRAY_SIZE(d1w_device_resource),
};
#endif

#if defined(CONFIG_MPCORE_WATCHDOG)
static struct resource wdt_device_resource[] = {
    [0] = {
        .start = PTIM_BASE_ADDR,
        .end   = PTIM_BASE_ADDR + 0x34,
        .flags = IORESOURCE_MEM,
    },
    [1] = {
        .start = BCM_INT_ID_PPI14,
        .end   = BCM_INT_ID_PPI14,
        .flags = IORESOURCE_IRQ,
    },
};

static struct platform_device wdt_device =
{
   .name          = "mpcore_wdt",
   .id            = -1,
   .resource	  = wdt_device_resource,
   .num_resources = ARRAY_SIZE(wdt_device_resource),
};
#endif

#if defined(CONFIG_KEYBOARD_ISLAND)

static struct KEYMAP board_keypad_keymap[] = {
    { 0x01, '1' }, { 0x11, '2' }, { 0x21, '3' }, { 0x31, 'a' }, { 0x41, '(' }, { 0x51, 'e' }, { 0x61, 'U' },
    { 0x02, '4' }, { 0x12, '5' }, { 0x22, '6' }, { 0x32, 'b' }, { 0x42, '>' }, { 0x52, 'L' }, { 0x62, 'C' },
    { 0x03, '7' }, { 0x13, '8' }, { 0x23, '9' }, { 0x33, 'c' }, { 0x43, '<' }, { 0x53, 'f' }, { 0x63, 'D' },
    { 0x04, '*' }, { 0x14, '0' }, { 0x24, '#' }, { 0x34, 'd' }, { 0x44, 'S' }, { 0x54, 'H' }, { 0x64, 'B' },
};

static unsigned int board_keypad_pwroff[] = { '*', '8', '6' };

static struct KEYPAD_DATA board_keypad_param =
{
    .active_mode = 0,
    .keymap      = board_keypad_keymap,
    .keymap_cnt  = ARRAY_SIZE(board_keypad_keymap),
    .pwroff      = board_keypad_pwroff,
    .pwroff_cnt  = ARRAY_SIZE(board_keypad_pwroff),
};

static struct resource keypad_device_resource[] = {
    [0] = {
        .start = KEYPAD_BASE_ADDR,
        .end   = KEYPAD_BASE_ADDR + 0xD0,
        .flags = IORESOURCE_MEM,
    },
    [1] = {
        .start = BCM_INT_ID_KEYPAD,
        .end   = BCM_INT_ID_KEYPAD,
        .flags = IORESOURCE_IRQ,
    },
};

static struct platform_device keypad_device =
{
   .name          = "island-keypad",
   .id            = -1,
   .resource	  = keypad_device_resource,
   .num_resources = ARRAY_SIZE(keypad_device_resource),
   .dev = {
      .platform_data = &board_keypad_param,
   },
};
#endif

#if defined(CONFIG_RTC_DRV_ISLAND)
static struct resource rtc_device_resource[] = {
    [0] = {
        .start = BBL_BASE_ADDR,
        .end   = BBL_BASE_ADDR + 0x24,
        .flags = IORESOURCE_MEM,
    },
    [1] = {
        .start = BCM_INT_ID_BBL0,
        .end   = BCM_INT_ID_BBL0,
        .flags = IORESOURCE_IRQ,
    },
    [2] = {
        .start = BCM_INT_ID_BBL1,
        .end   = BCM_INT_ID_BBL1,
        .flags = IORESOURCE_IRQ,
    },
};

static struct platform_device rtc_device =
{
   .name          = "bcmhana-rtc",
   .id            = -1,
   .resource	  = rtc_device_resource,
   .num_resources = ARRAY_SIZE(rtc_device_resource),
};
#endif

/* Common devices among all Island boards */
static struct platform_device *board_common_plat_devices[] __initdata = {
	&board_serial_device,
#if defined(CONFIG_MPCORE_WATCHDOG)
        &wdt_device,
#endif
#if defined(CONFIG_W1_MASTER_DS1WM)
        &d1w_device,
#endif
#if defined(CONFIG_HW_RANDOM_KONA)
        &rng_device,
#endif
#if defined(CONFIG_RTC_DRV_ISLAND)
        &rtc_device,
#endif
#if defined(CONFIG_KONA_PWMC)
        &pwm_device,
#endif
#if defined(CONFIG_KEYBOARD_ISLAND)
        &keypad_device,
#endif
};

void __init board_add_common_devices(void)
{
	platform_add_devices(board_common_plat_devices,
			ARRAY_SIZE(board_common_plat_devices));
}
