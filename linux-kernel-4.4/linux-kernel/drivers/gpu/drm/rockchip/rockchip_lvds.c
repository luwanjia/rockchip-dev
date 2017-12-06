/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_of.h>

#include <linux/component.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <video/display_timing.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"
#include "rockchip_lvds.h"

#define DISPLAY_OUTPUT_RGB		0
#define DISPLAY_OUTPUT_LVDS		1
#define DISPLAY_OUTPUT_DUAL_LVDS	2

#define connector_to_lvds(c) \
		container_of(c, struct rockchip_lvds, connector)

#define encoder_to_lvds(c) \
		container_of(c, struct rockchip_lvds, encoder)
#define bridge_to_lvds(c)  \
  container_of(c, struct rockchip_lvds, bridge)

/*
 * @grf_offset: offset inside the grf regmap for setting the rockchip lvds
 */
struct rockchip_lvds_soc_data {
	int grf_soc_con6;
	int grf_soc_con7;
};

struct rockchip_lvds {
	void *base;
	struct device *dev;
	void __iomem *regs;
	struct regmap *grf;
	struct clk *pclk;
	const struct rockchip_lvds_soc_data *soc_data;

  struct regulator_bulk_data supplies[3];

	int output;
	int format;

	struct drm_device *drm_dev;
	struct drm_panel *panel;
	struct drm_connector connector;
	struct drm_encoder encoder;
  struct drm_bridge bridge;
  struct drm_encoder *ext_encoder;

	struct mutex suspend_lock;
	int suspend;

  unsigned int val;
};

static inline void lvds_writel(struct rockchip_lvds *lvds, u32 offset, u32 val)
{
	writel_relaxed(val, lvds->regs + offset);
	writel_relaxed(val, lvds->regs + offset + 0x100);
}

static inline int lvds_name_to_format(const char *s)
{
	if (!s)
		return -EINVAL;

	if (strncmp(s, "jeida", 6) == 0)
		return LVDS_FORMAT_JEIDA;
	else if (strncmp(s, "vesa", 5) == 0)
		return LVDS_FORMAT_VESA;

	return -EINVAL;
}

static inline int lvds_name_to_output(const char *s)
{
	if (!s)
		return -EINVAL;

	if (strncmp(s, "rgb", 3) == 0)
		return DISPLAY_OUTPUT_RGB;
	else if (strncmp(s, "lvds", 4) == 0)
		return DISPLAY_OUTPUT_LVDS;
	else if (strncmp(s, "duallvds", 8) == 0)
		return DISPLAY_OUTPUT_DUAL_LVDS;

	return -EINVAL;
}

static int rockchip_lvds_poweron(struct rockchip_lvds *lvds)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(lvds->supplies),
				    lvds->supplies);
	if (ret < 0)
		return ret;

	ret = clk_enable(lvds->pclk);
	if (ret < 0) {
		dev_err(lvds->dev, "failed to enable lvds pclk %d\n", ret);
		return ret;
	}

	ret = pm_runtime_get_sync(lvds->dev);
	if (ret < 0) {
		dev_err(lvds->dev, "failed to get pm runtime: %d\n", ret);
		return ret;
	}

	if (lvds->output == DISPLAY_OUTPUT_RGB) {
		lvds_writel(lvds, RK3288_LVDS_CH0_REG0,
			    RK3288_LVDS_CH0_REG0_TTL_EN |
			    RK3288_LVDS_CH0_REG0_LANECK_EN |
			    RK3288_LVDS_CH0_REG0_LANE4_EN |
			    RK3288_LVDS_CH0_REG0_LANE3_EN |
			    RK3288_LVDS_CH0_REG0_LANE2_EN |
			    RK3288_LVDS_CH0_REG0_LANE1_EN |
			    RK3288_LVDS_CH0_REG0_LANE0_EN);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG2,
			    RK3288_LVDS_PLL_FBDIV_REG2(0x46));

		lvds_writel(lvds, RK3288_LVDS_CH0_REG3,
			    RK3288_LVDS_PLL_FBDIV_REG3(0x46));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG4,
			    RK3288_LVDS_CH0_REG4_LANECK_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE4_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE3_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE2_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE1_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE0_TTL_MODE);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG5,
			    RK3288_LVDS_CH0_REG5_LANECK_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE4_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE3_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE2_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE1_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE0_TTL_DATA);
		lvds_writel(lvds, RK3288_LVDS_CH0_REGD,
			    RK3288_LVDS_PLL_PREDIV_REGD(0x0a));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG20,
			    RK3288_LVDS_CH0_REG20_LSB);
	} else {
		lvds_writel(lvds, RK3288_LVDS_CH0_REG0,
			    RK3288_LVDS_CH0_REG0_LVDS_EN |
			    RK3288_LVDS_CH0_REG0_LANECK_EN |
			    RK3288_LVDS_CH0_REG0_LANE4_EN |
			    RK3288_LVDS_CH0_REG0_LANE3_EN |
			    RK3288_LVDS_CH0_REG0_LANE2_EN |
			    RK3288_LVDS_CH0_REG0_LANE1_EN |
			    RK3288_LVDS_CH0_REG0_LANE0_EN);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG1,
			    RK3288_LVDS_CH0_REG1_LANECK_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE4_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE3_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE2_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE1_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE0_BIAS);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG2,
			    RK3288_LVDS_CH0_REG2_RESERVE_ON |
			    RK3288_LVDS_CH0_REG2_LANECK_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE4_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE3_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE2_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE1_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE0_LVDS_MODE |
			    RK3288_LVDS_PLL_FBDIV_REG2(0x46));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG3,
			    RK3288_LVDS_PLL_FBDIV_REG3(0x46));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG4, 0x00);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG5, 0x00);
		lvds_writel(lvds, RK3288_LVDS_CH0_REGD,
			    RK3288_LVDS_PLL_PREDIV_REGD(0x0a));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG20,
			    RK3288_LVDS_CH0_REG20_LSB);
	}

	writel(RK3288_LVDS_CFG_REGC_PLL_ENABLE,
	       lvds->regs + RK3288_LVDS_CFG_REGC);
	writel(RK3288_LVDS_CFG_REG21_TX_ENABLE,
	       lvds->regs + RK3288_LVDS_CFG_REG21);

	return 0;
}

static void rockchip_lvds_poweroff(struct rockchip_lvds *lvds)
{
	int ret;

	ret = regmap_write(lvds->grf,
			   lvds->soc_data->grf_soc_con7, 0xffff8000);
	if (ret != 0)
		dev_err(lvds->dev, "Could not write to GRF: %d\n", ret);

	writel(RK3288_LVDS_CFG_REG21_TX_DISABLE,
	       lvds->regs + RK3288_LVDS_CFG_REG21);
	writel(RK3288_LVDS_CFG_REGC_PLL_DISABLE,
	       lvds->regs + RK3288_LVDS_CFG_REGC);

	pm_runtime_put(lvds->dev);
	clk_disable(lvds->pclk);

	regulator_bulk_disable(ARRAY_SIZE(lvds->supplies),
			       lvds->supplies);
}

static enum drm_connector_status
rockchip_lvds_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rockchip_lvds_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static struct drm_connector_funcs rockchip_lvds_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = rockchip_lvds_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rockchip_lvds_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int rockchip_lvds_connector_get_modes(struct drm_connector *connector)
{
	struct rockchip_lvds *lvds = connector_to_lvds(connector);
	struct drm_panel *panel = lvds->panel;

	return panel->funcs->get_modes(panel);
}

static struct drm_encoder *
rockchip_lvds_connector_best_encoder(struct drm_connector *connector)
{
	struct rockchip_lvds *lvds = connector_to_lvds(connector);

	return &lvds->encoder;
}

static enum drm_mode_status rockchip_lvds_connector_mode_valid(
		struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	return MODE_OK;
}

static
struct drm_connector_helper_funcs rockchip_lvds_connector_helper_funcs = {
	.get_modes = rockchip_lvds_connector_get_modes,
	.mode_valid = rockchip_lvds_connector_mode_valid,
	.best_encoder = rockchip_lvds_connector_best_encoder,
};

static void rockchip_lvds_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);
	int ret;

	mutex_lock(&lvds->suspend_lock);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		if (!lvds->suspend)
			goto out;

		drm_panel_prepare(lvds->panel);
		ret = rockchip_lvds_poweron(lvds);
		if (ret < 0) {
			drm_panel_unprepare(lvds->panel);
			goto out;
		}
		drm_panel_enable(lvds->panel);

		lvds->suspend = false;
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		if (lvds->suspend)
			goto out;

		drm_panel_disable(lvds->panel);
		rockchip_lvds_poweroff(lvds);
		drm_panel_unprepare(lvds->panel);

		lvds->suspend = true;
		break;
	default:
		break;
	}

out:
	mutex_unlock(&lvds->suspend_lock);
}

static bool
rockchip_lvds_encoder_mode_fixup(struct drm_encoder *encoder,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void rockchip_lvds_mode_set(struct rockchip_lvds *lvds,
					  struct drm_display_mode *mode,
					  struct drm_display_mode *adjusted)
{
	u32 h_bp = mode->htotal - mode->hsync_start;
	u8 pin_hsync = (mode->flags & DRM_MODE_FLAG_PHSYNC) ? 1 : 0;
	u8 pin_dclk = (mode->flags & DRM_MODE_FLAG_PCSYNC) ? 1 : 0;
	u32 val;
	int ret;

	val = lvds->format;
	if (lvds->output == DISPLAY_OUTPUT_DUAL_LVDS)
		val |= LVDS_DUAL | LVDS_CH0_EN | LVDS_CH1_EN;
	else if (lvds->output == DISPLAY_OUTPUT_LVDS)
		val |= LVDS_CH0_EN;
	else if (lvds->output == DISPLAY_OUTPUT_RGB)
		val |= LVDS_TTL_EN | LVDS_CH0_EN | LVDS_CH1_EN;

	if (h_bp & 0x01)
		val |= LVDS_START_PHASE_RST_1;

	val |= (pin_dclk << 8) | (pin_hsync << 9);
	val |= (0xffff << 16);
	ret = regmap_write(lvds->grf, lvds->soc_data->grf_soc_con7, val);
	if (ret != 0) {
		dev_err(lvds->dev, "Could not write to GRF: %d\n", ret);
		return;
	}
}

static void rockchip_lvds_encoder_mode_set(struct drm_encoder *encoder,
					  struct drm_display_mode *mode,
					  struct drm_display_mode *adjusted)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);
    rockchip_lvds_mode_set(lvds, mode, adjusted);
}

static int rockchip_lvds_set_vop_source(struct rockchip_lvds *lvds,
					struct drm_encoder *encoder)
{
	u32 val;
	int ret;

	ret = drm_of_encoder_active_endpoint_id(lvds->dev->of_node, encoder);
	if (ret < 0)
		return ret;

	if (ret)
		val = RK3288_LVDS_SOC_CON6_SEL_VOP_LIT |
		      (RK3288_LVDS_SOC_CON6_SEL_VOP_LIT << 16);
	else
		val = RK3288_LVDS_SOC_CON6_SEL_VOP_LIT << 16;

	ret = regmap_write(lvds->grf, lvds->soc_data->grf_soc_con6, val);
	if (ret < 0)
		return ret;

	return 0;
}

static int
rockchip_lvds_encoder_atomic_check(struct drm_encoder *encoder,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;

	s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_type = conn_state->connector->connector_type;
	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];

	return 0;
}

static void rockchip_lvds_encoder_commit(struct drm_encoder *encoder)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);

	rockchip_lvds_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
	rockchip_lvds_set_vop_source(lvds, encoder);
}

static void rockchip_lvds_encoder_disable(struct drm_encoder *encoder)
{
	rockchip_lvds_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static struct drm_encoder_helper_funcs rockchip_lvds_encoder_helper_funcs = {
	.dpms = rockchip_lvds_encoder_dpms,
	.mode_fixup = rockchip_lvds_encoder_mode_fixup,
	.mode_set = rockchip_lvds_encoder_mode_set,
	.commit = rockchip_lvds_encoder_commit,
	.disable = rockchip_lvds_encoder_disable,
	.atomic_check = rockchip_lvds_encoder_atomic_check,
};

static void rockchip_lvds_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static struct drm_encoder_funcs rockchip_lvds_encoder_funcs = {
	.destroy = rockchip_lvds_encoder_destroy,
};

static void rockchip_lvds_bridge_mode_set(struct drm_bridge *bridge,
					  struct drm_display_mode *mode,
					  struct drm_display_mode *adjusted)
{
	struct rockchip_lvds *lvds = bridge_to_lvds(bridge);
    rockchip_lvds_mode_set(lvds, mode, adjusted);
}

static void rockchip_lvds_bridge_pre_enable(struct drm_bridge *bridge)
{
}

/*
 * post_disable is called right after encoder prepare, so do lvds and crtc
 * mode config here.
 */
static void rockchip_lvds_bridge_post_disable(struct drm_bridge *bridge)
{
	struct rockchip_lvds *lvds = bridge_to_lvds(bridge);
	struct drm_connector *connector;
	int ret, connector_type = DRM_MODE_CONNECTOR_Unknown;
	struct rockchip_crtc_state *s;

	if (!bridge->encoder->crtc)
		return;

	list_for_each_entry(connector, &bridge->dev->mode_config.connector_list,
			head) {
		if (connector->encoder == bridge->encoder)
			connector_type = connector->connector_type;
	}

    s = to_rockchip_crtc_state(bridge->encoder->crtc->state);
	s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_type = connector_type;

/*
	ret = rockchip_drm_crtc_mode_config(bridge->encoder->crtc,
						connector_type,
						ROCKCHIP_OUT_MODE_P888);
	if (ret < 0) {
		dev_err(lvds->dev, "Could not set crtc mode config: %d\n", ret);
		return;
	}
*/

	ret = rockchip_lvds_set_vop_source(lvds, bridge->encoder);
	if (ret < 0) {
		dev_err(lvds->dev, "Could not set vop source: %d\n", ret);
		return;
	}
}

static void rockchip_lvds_bridge_enable(struct drm_bridge *bridge)
{
	struct rockchip_lvds *lvds = bridge_to_lvds(bridge);
	struct drm_connector *connector;
	int ret, connector_type = DRM_MODE_CONNECTOR_Unknown;
	struct rockchip_crtc_state *s;

	mutex_lock(&lvds->suspend_lock);

	if (!lvds->suspend)
		goto out;

	ret = rockchip_lvds_poweron(lvds);
	if (ret < 0) {
		dev_err(lvds->dev, "could not enable lvds\n");
		goto out;
	}

	lvds->suspend = false;
out:
	mutex_unlock(&lvds->suspend_lock);

	if (!bridge->encoder->crtc)
		return;

	list_for_each_entry(connector, &bridge->dev->mode_config.connector_list,
			head) {
		if (connector->encoder == bridge->encoder)
			connector_type = connector->connector_type;
	}

    s = to_rockchip_crtc_state(bridge->encoder->crtc->state);
	s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_type = connector_type;

/*
	ret = rockchip_drm_crtc_mode_config(bridge->encoder->crtc,
						connector_type,
						ROCKCHIP_OUT_MODE_P888);
	if (ret < 0) {
		dev_err(lvds->dev, "Could not set crtc mode config: %d\n", ret);
		return;
	}
*/

	ret = rockchip_lvds_set_vop_source(lvds, bridge->encoder);
	if (ret < 0) {
		dev_err(lvds->dev, "Could not set vop source: %d\n", ret);
		return;
	}

}

static void rockchip_lvds_bridge_disable(struct drm_bridge *bridge)
{
	struct rockchip_lvds *lvds = bridge_to_lvds(bridge);

	mutex_lock(&lvds->suspend_lock);

	if (lvds->suspend)
		goto out;

	rockchip_lvds_poweroff(lvds);
	lvds->suspend = true;

out:
	mutex_unlock(&lvds->suspend_lock);
}

static struct drm_bridge_funcs rockchip_lvds_bridge_funcs = {
	.mode_set = rockchip_lvds_bridge_mode_set,
	.enable = rockchip_lvds_bridge_enable,
	.disable = rockchip_lvds_bridge_disable,
	.pre_enable = rockchip_lvds_bridge_pre_enable,
	.post_disable = rockchip_lvds_bridge_post_disable,
};

static struct rockchip_lvds_soc_data rk3288_lvds_data = {
	.grf_soc_con6 = 0x025c,
	.grf_soc_con7 = 0x0260,
};

static const struct of_device_id rockchip_lvds_dt_ids[] = {
	{
		.compatible = "rockchip,rk3288-lvds",
		.data = &rk3288_lvds_data
	},
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_lvds_dt_ids);

static int rockchip_lvds_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	lvds->drm_dev = drm_dev;

	if (!lvds->panel) {
		struct drm_bridge *bridge = &lvds->bridge;

		if (!lvds->ext_encoder->of_node)
			return -ENODEV;

		ret = component_bind_all(dev, drm_dev);
		if (ret < 0)
			return ret;

		/**
		 * Override any possible crtcs set by the encoder itself,
		 * as they are connected to the lvds instead.
		 */
		encoder = of_drm_find_encoder(lvds->ext_encoder->of_node);
		encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
								dev->of_node);

		encoder->bridge = bridge;
		bridge->encoder = encoder;
		ret = drm_bridge_attach(drm_dev, bridge);
		if (ret < 0) {
			component_unbind_all(dev, drm_dev);
			return ret;
		}

		pm_runtime_enable(dev);
		return 0;
	}

	encoder = &lvds->encoder;
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dev->of_node);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_lvds_encoder_funcs,
			       DRM_MODE_ENCODER_LVDS, NULL);
	if (ret < 0) {
		DRM_ERROR("failed to initialize encoder with drm\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &rockchip_lvds_encoder_helper_funcs);

	connector = &lvds->connector;
	connector->dpms = DRM_MODE_DPMS_OFF;

	ret = drm_connector_init(drm_dev, connector,
				 &rockchip_lvds_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret < 0) {
		DRM_ERROR("failed to initialize connector with drm\n");
		goto err_free_encoder;
	}

	drm_connector_helper_add(connector,
				 &rockchip_lvds_connector_helper_funcs);

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		DRM_ERROR("failed to attach connector and encoder\n");
		goto err_free_connector;
	}

	ret = drm_panel_attach(lvds->panel, connector);
	if (ret < 0) {
		DRM_ERROR("failed to attach connector and encoder\n");
		goto err_free_connector;
	}

	pm_runtime_enable(dev);

	return 0;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
	return ret;
}

static void rockchip_lvds_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	if (lvds->panel) {
		rockchip_lvds_encoder_dpms(&lvds->encoder, DRM_MODE_DPMS_OFF);

		drm_panel_detach(lvds->panel);

		drm_connector_cleanup(&lvds->connector);
		drm_encoder_cleanup(&lvds->encoder);
	} else {
		component_unbind_all(dev, drm_dev);
	}

	pm_runtime_disable(dev);
}

static const struct component_ops rockchip_lvds_component_ops = {
	.bind = rockchip_lvds_bind,
	.unbind = rockchip_lvds_unbind,
};

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int rockchip_lvds_master_bind(struct device *dev)
{
	return 0;
}

static void rockchip_lvds_master_unbind(struct device *dev)
{
	/* do nothing */
}

static const struct component_master_ops rockchip_lvds_master_ops = {
	.bind = rockchip_lvds_master_bind,
	.unbind = rockchip_lvds_master_unbind,
};

static int rockchip_lvds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_lvds *lvds;
	struct device_node *port, *output_node = NULL;
	const struct of_device_id *match;
	struct resource *res;
	const char *name;
	int i, ret;

	if (!dev->of_node)
		return -ENODEV;

	lvds = devm_kzalloc(&pdev->dev, sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	lvds->dev = dev;
	lvds->suspend = true;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lvds->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(lvds->regs))
		return PTR_ERR(lvds->regs);

	lvds->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						    "rockchip,grf");
	if (IS_ERR(lvds->grf)) {
		dev_err(dev, "missing rockchip,grf property\n");
		return PTR_ERR(lvds->grf);
	}

	lvds->supplies[0].supply = "avdd1v0";
	lvds->supplies[1].supply = "avdd1v8";
	lvds->supplies[2].supply = "avdd3v3";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(lvds->supplies),
				      lvds->supplies);
	if (ret < 0)
		return ret;

	lvds->pclk = devm_clk_get(&pdev->dev, "pclk_lvds");
	if (IS_ERR(lvds->pclk)) {
		dev_err(dev, "could not get pclk_lvds\n");
		return PTR_ERR(lvds->pclk);
	}
	ret = clk_prepare(lvds->pclk);
	if (ret < 0) {
		dev_err(dev, "failed to prepare pclk_lvds\n");
		return ret;
	}

	match = of_match_node(rockchip_lvds_dt_ids, dev->of_node);
	lvds->soc_data = match->data;

	dev_set_drvdata(dev, lvds);
	mutex_init(&lvds->suspend_lock);

	if (of_property_read_string(dev->of_node, "rockchip,output", &name))
		/* default set it as output rgb */
		lvds->output = DISPLAY_OUTPUT_RGB;
	else
		lvds->output = lvds_name_to_output(name);

	if (lvds->output < 0) {
		dev_err(dev, "invalid output type [%s]\n", name);
		return lvds->output;
	}

	if (of_property_read_string(dev->of_node, "rockchip,data-mapping",
				    &name))
		/* default set it as format jeida */
		lvds->format = LVDS_FORMAT_JEIDA;
	else
		lvds->format = lvds_name_to_format(name);

	if (lvds->format < 0) {
		dev_err(dev, "invalid data-mapping format [%s]\n", name);
		return lvds->format;
	}

	if (of_property_read_u32(dev->of_node, "rockchip,data-width", &i)) {
		lvds->format |= LVDS_24BIT;
	} else {
		if (i == 24) {
			lvds->format |= LVDS_24BIT;
		} else if (i == 18) {
			lvds->format |= LVDS_18BIT;
		} else {
			dev_err(&pdev->dev,
				"rockchip-lvds unsupport data-width[%d]\n", i);
			ret = -EINVAL;
			goto err_unprepare_pclk;
		}
	}

	port = of_graph_get_port_by_id(dev->of_node, 1);
	if (port) {
		struct device_node *endpoint;

		endpoint = of_get_child_by_name(port, "endpoint");
		if (endpoint) {
			output_node = of_graph_get_remote_port_parent(endpoint);
			of_node_put(endpoint);
		}
	}

	if (!output_node) {
		dev_err(&pdev->dev, "no output defined\n");
		ret = -EINVAL;
		goto err_unprepare_pclk;
	}

	lvds->panel = of_drm_find_panel(output_node);
	of_node_put(output_node);
	if (!lvds->panel) {
		struct drm_encoder *encoder;
		struct component_match *match = NULL;

		/* Try to find an encoder in the output node */
		encoder = of_drm_find_encoder(output_node);
		if (!encoder) {
			dev_err(&pdev->dev, "neither panel nor encoder found\n");
			of_node_put(output_node);
			ret = -EPROBE_DEFER;
			goto err_unprepare_pclk;
		}

		lvds->ext_encoder = encoder;

		component_match_add(dev, &match, compare_of, output_node);
		of_node_put(output_node);

		lvds->bridge.funcs = &rockchip_lvds_bridge_funcs;
		lvds->bridge.of_node = dev->of_node;
		ret = drm_bridge_add(&lvds->bridge);
		if (ret) {
			dev_err(&pdev->dev, "failed to add bridge %d\n", ret);
			goto err_unprepare_pclk;
		}

		ret = component_master_add_with_match(dev,
						      &rockchip_lvds_master_ops,
						      match);
		if (ret < 0)
			goto err_bridge_remove;
	}

	ret = component_add(&pdev->dev, &rockchip_lvds_component_ops);
	if (ret < 0)
		goto err_master_remove;

	return 0;

err_master_remove:
	if (!lvds->panel)
		component_master_del(&pdev->dev, &rockchip_lvds_master_ops);
err_bridge_remove:
	if (!lvds->panel)
		drm_bridge_remove(&lvds->bridge);
err_unprepare_pclk:
	clk_unprepare(lvds->pclk);
	return ret;
}

static int rockchip_lvds_remove(struct platform_device *pdev)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(&pdev->dev);

	component_del(&pdev->dev, &rockchip_lvds_component_ops);
	if (!lvds->panel) {
		component_master_del(&pdev->dev, &rockchip_lvds_master_ops);
		drm_bridge_remove(&lvds->bridge);
	}

	clk_unprepare(lvds->pclk);

	return 0;
}

struct platform_driver rockchip_lvds_driver = {
	.probe = rockchip_lvds_probe,
	.remove = rockchip_lvds_remove,
	.driver = {
		   .name = "rockchip-lvds",
		   .of_match_table = of_match_ptr(rockchip_lvds_dt_ids),
	},
};
module_platform_driver(rockchip_lvds_driver);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("ROCKCHIP LVDS Driver");
MODULE_LICENSE("GPL v2");
