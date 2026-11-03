/*
 * SPDX-License-Identifier:     GPL-2.0+
 *
 * (C) Copyright 2023 Rockchip Electronics Co., Ltd
 */

#include <common.h>
#include <dm.h>
#include <dwc3-uboot.h>
#include <pwm.h>
#include <usb.h>
#include <dt-bindings/pwm/pwm.h>
#include <linux/usb/phy-rockchip-usbdp.h>
#include <asm/io.h>
#include <rockusb.h>

DECLARE_GLOBAL_DATA_PTR;

#if defined(CONFIG_DM_PWM) && !defined(CONFIG_SPL_BUILD) && !defined(CONFIG_SUPPORT_USBPLUG)

#define DSHANPI_FAN_DUTY_PERCENT	40

int rk_board_init(void)
{
	struct ofnode_phandle_args args;
	struct udevice		  *pwm;
	ofnode			   fan;
	uint			   duty_ns;
	int			   ret;

	/* board_init() may already have switched to the kernel device tree. */
	fan = ofnode_path("/pwm-fan");
	if (!ofnode_valid(fan) || !ofnode_is_available(fan))
		return 0;

	ret = ofnode_parse_phandle_with_args(fan, "pwms", "#pwm-cells", 0, 0, &args);
	if (ret)
		goto err;
	if (args.args_count != 3 || !args.args[1]) {
		ret = -EINVAL;
		goto err;
	}

	ret = uclass_get_device_by_ofnode(UCLASS_PWM, args.node, &pwm);
	if (ret)
		goto err;

	ret = pwm_set_invert(pwm, args.args[0], args.args[2] & PWM_POLARITY_INVERTED);
	if (ret)
		goto err;

	/* Duty is the active portion, which is low for the DShanPi fan. */
	duty_ns = (u64)args.args[1] * DSHANPI_FAN_DUTY_PERCENT / 100;
	ret     = pwm_set_config(pwm, args.args[0], args.args[1], duty_ns);
	if (ret)
		goto err;

	ret = pwm_set_enable(pwm, args.args[0], true);
	if (ret)
		goto err;

	printf("Fan: PWM duty %u%%\n", DSHANPI_FAN_DUTY_PERCENT);
	return 0;

err:
	/* A fan setup failure must not prevent the OS from booting. */
	printf("Fan: PWM setup failed (%d)\n", ret);
	return 0;
}
#endif

#ifdef CONFIG_USB_DWC3
#define CRU_BASE		0x27200000
#define CRU_SOFTRST_CON47	0x0abc
#define U3PHY_BASE		0x2b010000

static struct dwc3_device dwc3_device_data = {
	.maximum_speed = USB_SPEED_SUPER,
	.base = 0x23000000,
	.dr_mode = USB_DR_MODE_PERIPHERAL,
	.index = 0,
	.dis_u2_susphy_quirk = 1,
	.dis_u1u2_quirk = 1,
	.usb2_phyif_utmi_width = 16,
};

int usb_gadget_handle_interrupts(int index)
{
	dwc3_uboot_handle_interrupt(0);
	return 0;
}

bool rkusb_usb3_capable(void)
{
	return true;
}

static void usb_reset_otg_controller(void)
{
	writel(0x00200020, CRU_BASE + CRU_SOFTRST_CON47);
	mdelay(1);
	writel(0x00200000, CRU_BASE + CRU_SOFTRST_CON47);
	mdelay(1);
}

int board_usb_init(int index, enum usb_init_type init)
{
	u32 ret = 0;

	usb_reset_otg_controller();

#if defined(CONFIG_SUPPORT_USBPLUG)
	dwc3_device_data.maximum_speed = USB_SPEED_HIGH;

	if (rkusb_switch_usb3_enabled()) {
		dwc3_device_data.maximum_speed = USB_SPEED_SUPER;
		ret = rockchip_u3phy_uboot_init(U3PHY_BASE);
		if (ret) {
			rkusb_force_to_usb2(true);
			dwc3_device_data.maximum_speed = USB_SPEED_HIGH;
		}
	}
#else
	ret = rockchip_u3phy_uboot_init(U3PHY_BASE);
	if (ret) {
		rkusb_force_to_usb2(true);
		dwc3_device_data.maximum_speed = USB_SPEED_HIGH;
	}
#endif

	return dwc3_uboot_init(&dwc3_device_data);
}

#if defined(CONFIG_SUPPORT_USBPLUG)
int board_usb_cleanup(int index, enum usb_init_type init)
{
	dwc3_uboot_exit(index);
	return 0;
}
#endif

#endif
