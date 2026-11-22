// SPDX-License-Identifier: GPL-2.0-only
/*
 * BOE RM692E5 1080x2412 120Hz DSC command-mode DSI panel driver
 * for OnePlus Ace Pro (PGP110 / ovaltine).
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct boe_rm692e5 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_connector *connector;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *vout_gpio;
	struct gpio_desc *vddr_aod_en_gpio;
	struct regulator *vddio;
	struct regulator *vci;
};

static inline struct boe_rm692e5 *to_boe_rm692e5(struct drm_panel *panel)
{
	return container_of(panel, struct boe_rm692e5, panel);
}

static void boe_rm692e5_reset(struct boe_rm692e5 *ctx)
{
	/* Match vendor reset sequence: <1 10>, <0 5>, <1 32> (ACTIVE_LOW) */
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(32);
}

static int boe_rm692e5_power_on(struct boe_rm692e5 *ctx)
{
	int ret;

	/* Enable VDDI (1.8V) first, then panel VOUT GPIO, then VCI (3.1V) */
	ret = regulator_enable(ctx->vddio);
	if (ret < 0)
		return ret;

	if (ctx->vout_gpio) {
		gpiod_set_value_cansleep(ctx->vout_gpio, 1);
		usleep_range(3000, 3500);
	}

	ret = regulator_enable(ctx->vci);
	if (ret < 0)
		goto err_vci;

	if (ctx->vddr_aod_en_gpio)
		gpiod_set_value_cansleep(ctx->vddr_aod_en_gpio, 1);

	usleep_range(2000, 2500);

	return 0;

err_vci:
	if (ctx->vout_gpio)
		gpiod_set_value_cansleep(ctx->vout_gpio, 0);
	regulator_disable(ctx->vddio);
	return ret;
}

static void boe_rm692e5_power_off(struct boe_rm692e5 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	if (ctx->vddr_aod_en_gpio)
		gpiod_set_value_cansleep(ctx->vddr_aod_en_gpio, 0);

	regulator_disable(ctx->vci);

	if (ctx->vout_gpio)
		gpiod_set_value_cansleep(ctx->vout_gpio, 0);

	regulator_disable(ctx->vddio);
}

static int boe_rm692e5_on(struct boe_rm692e5 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	/* Page F4 */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0xb3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x29);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x74);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x72);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0xdd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x61);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0a, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0b, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0d, 0x0c);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1d, 0xde);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0xd7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x5d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x82);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x56);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x26, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0xcf);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x00);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0d, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0f, 0xf9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x5e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0xb2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x5c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x4d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x3d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0x03);

	/* Page D2: DSC PPS (v1.2, 10bpc, 10bpp, 12x540 slice) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xd2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x52, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x54, 0x6c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x56, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x9a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5e, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0xbb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6a, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6b, 0xec);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6c, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6e, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x71, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x72, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x73, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x74, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x54);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7a, 0x62);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7b, 0x69);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7c, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7d, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7e, 0x79);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7f, 0x7b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x7d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x7e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0xc2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x86, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x87, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x89, 0xbe);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8a, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8b, 0xfc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8c, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8d, 0xfa);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8e, 0x3a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8f, 0xf8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x93, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x95, 0x76);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x96, 0x4b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x97, 0xb6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x98, 0x4b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x99, 0xb6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9a, 0x4b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9b, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9c, 0x5b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9e, 0x7c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9f, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa2, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa3, 0xa3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa4, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa5, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa6, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa7, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xaa, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa0, 0xa0);

	/* Panel tuning */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xd4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xfd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x36);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xa1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x74, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xa1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc4, 0x9c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc5, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x23);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x13);

	/*
	 * Vendor timing-switch: FE 40 / BD <rate> / FE 00
	 * Selects the panel's internal TE/scan rate to match DPU framerate.
	 *   120Hz -> BD 05 (h-front-porch=9)
	 *    90Hz -> BD 06 (h-front-porch=10)
	 *    60Hz -> BD 00 (h-front-porch=8)
	 * Read mode from CRTC state; fall back to 120Hz on first enable.
	 */
	{
		struct drm_crtc_state *crtc_state = NULL;
		int vrefresh = 120;

		if (ctx->connector && ctx->connector->state &&
		    ctx->connector->state->crtc) {
			crtc_state = ctx->connector->state->crtc->state;
			vrefresh = drm_mode_vrefresh(&crtc_state->mode);
		}

		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x40);
		if (vrefresh <= 61)
			mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x00);
		else if (vrefresh <= 91)
			mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x06);
		else
			mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x05);
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x00);
	}

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x86, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xa1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0x80);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xa1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x1f);

	/* Setting AOD Black Frames */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x22);

	struct drm_dsc_picture_parameter_set pps;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfa, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0x08);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	/* Send DSC PPS and Compression Mode before 11h/29h so display-on latches valid DSC state */
	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);
	mipi_dsi_picture_parameter_set_multi(&dsi_ctx, &pps);
	mipi_dsi_compression_mode_ext_multi(&dsi_ctx, true,
					     MIPI_DSI_COMPRESSION_DSC, 0);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 28);

	return dsi_ctx.accum_err;
}

static int boe_rm692e5_disable(struct drm_panel *panel)
{
	struct boe_rm692e5 *ctx = to_boe_rm692e5(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 10);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);

	return dsi_ctx.accum_err;
}

static int boe_rm692e5_prepare(struct drm_panel *panel)
{
	struct boe_rm692e5 *ctx = to_boe_rm692e5(panel);
	int ret;

	ret = boe_rm692e5_power_on(ctx);
	if (ret < 0)
		return ret;

	boe_rm692e5_reset(ctx);
	usleep_range(1000, 2000);

	ret = boe_rm692e5_on(ctx);
	if (ret < 0)
		goto err_off;

	return 0;

err_off:
	boe_rm692e5_power_off(ctx);
	return ret;
}

static int boe_rm692e5_unprepare(struct drm_panel *panel)
{
	struct boe_rm692e5 *ctx = to_boe_rm692e5(panel);

	boe_rm692e5_power_off(ctx);
	return 0;
}

static const struct drm_display_mode boe_rm692e5_modes[] = {
	{ /* 120Hz default */
		.clock = (1080 + 9 + 2 + 21) * (2412 + 52 + 14 + 22) * 120 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 9,
		.hsync_end = 1080 + 9 + 2,
		.htotal = 1080 + 9 + 2 + 21,
		.vdisplay = 2412,
		.vsync_start = 2412 + 52,
		.vsync_end = 2412 + 52 + 14,
		.vtotal = 2412 + 52 + 14 + 22,
		.width_mm = 70,
		.height_mm = 155,
		.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	{ /* 90Hz */
		.clock = (1080 + 10 + 2 + 21) * (2412 + 52 + 14 + 22) * 90 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 10,
		.hsync_end = 1080 + 10 + 2,
		.htotal = 1080 + 10 + 2 + 21,
		.vdisplay = 2412,
		.vsync_start = 2412 + 52,
		.vsync_end = 2412 + 52 + 14,
		.vtotal = 2412 + 52 + 14 + 22,
		.width_mm = 70,
		.height_mm = 155,
		.type = DRM_MODE_TYPE_DRIVER,
	},
	{ /* 60Hz */
		.clock = (1080 + 8 + 2 + 21) * (2412 + 52 + 14 + 22) * 60 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 8,
		.hsync_end = 1080 + 8 + 2,
		.htotal = 1080 + 8 + 2 + 21,
		.vdisplay = 2412,
		.vsync_start = 2412 + 52,
		.vsync_end = 2412 + 52 + 14,
		.vtotal = 2412 + 52 + 14 + 22,
		.width_mm = 70,
		.height_mm = 155,
		.type = DRM_MODE_TYPE_DRIVER,
	},
};

static int boe_rm692e5_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	struct boe_rm692e5 *ctx = to_boe_rm692e5(panel);
	int count = 0;

	/* Save connector so boe_rm692e5_on() can read the selected mode */
	ctx->connector = connector;

	for (int i = 0; i < ARRAY_SIZE(boe_rm692e5_modes); i++)
		count += drm_connector_helper_get_modes_fixed(connector,
							      &boe_rm692e5_modes[i]);

	return count;
}

static const struct drm_panel_funcs boe_rm692e5_panel_funcs = {
	.prepare = boe_rm692e5_prepare,
	.unprepare = boe_rm692e5_unprepare,
	.disable = boe_rm692e5_disable,
	.get_modes = boe_rm692e5_get_modes,
};

static int boe_rm692e5_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	const u8 page0 = 0x00;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_write(dsi, 0xfe, &page0, sizeof(page0));
	if (ret < 0)
		goto out;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
out:
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	return ret < 0 ? ret : 0;
}

static int boe_rm692e5_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	const u8 page0 = 0x00;
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_write(dsi, 0xfe, &page0, sizeof(page0));
	if (ret < 0)
		goto out;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
out:
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	return ret < 0 ? ret : brightness;
}

static const struct backlight_ops boe_rm692e5_bl_ops = {
	.update_status = boe_rm692e5_bl_update_status,
	.get_brightness = boe_rm692e5_bl_get_brightness,
};

static struct backlight_device *
boe_rm692e5_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1638,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &boe_rm692e5_bl_ops, &props);
}

static int boe_rm692e5_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boe_rm692e5 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct boe_rm692e5, panel,
				   &boe_rm692e5_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->vddio = devm_regulator_get(dev, "vddio");
	if (IS_ERR(ctx->vddio))
		return dev_err_probe(dev, PTR_ERR(ctx->vddio),
				     "Failed to get vddio regulator\n");

	ctx->vci = devm_regulator_get(dev, "vci");
	if (IS_ERR(ctx->vci))
		return dev_err_probe(dev, PTR_ERR(ctx->vci),
				     "Failed to get vci regulator\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->vout_gpio = devm_gpiod_get_optional(dev, "vout", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->vout_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->vout_gpio),
				     "Failed to get vout-gpios\n");

	ctx->vddr_aod_en_gpio = devm_gpiod_get_optional(dev, "vddr-aod-en",
							GPIOD_OUT_LOW);
	if (IS_ERR(ctx->vddr_aod_en_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->vddr_aod_en_gpio),
				     "Failed to get vddr-aod-en-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS;
	/* Match stock qcom,mdss-dsi-panel-clockrate = 1100 Mbps/lane */
	dsi->hs_rate = 1100000000;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = boe_rm692e5_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	dsi->dsc = &ctx->dsc;
	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 2;
	ctx->dsc.slice_height = 12;
	ctx->dsc.slice_width = 540;
	ctx->dsc.slice_count = 1080 / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = 10;
	ctx->dsc.bits_per_pixel = 10 << 4;
	ctx->dsc.block_pred_enable = true;
	ctx->dsc.first_line_bpg_offset = 12;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void boe_rm692e5_remove(struct mipi_dsi_device *dsi)
{
	struct boe_rm692e5 *ctx = mipi_dsi_get_drvdata(dsi);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe_rm692e5_of_match[] = {
	{ .compatible = "oplus,boe-rm692e5" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe_rm692e5_of_match);

static struct mipi_dsi_driver boe_rm692e5_driver = {
	.probe = boe_rm692e5_probe,
	.remove = boe_rm692e5_remove,
	.driver = {
		.name = "panel-boe-rm692e5",
		.of_match_table = boe_rm692e5_of_match,
	},
};
module_mipi_dsi_driver(boe_rm692e5_driver);

MODULE_DESCRIPTION("DRM driver for BOE RM692E5 cmd mode DSI panel");
MODULE_LICENSE("GPL");
