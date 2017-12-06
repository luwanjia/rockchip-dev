/*
 * Simple vga encoder driver
 *
 * Copyright (C) 2014 Heiko Stuebner <heiko@sntech.de>
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

#include <linux/module.h>
#include <linux/component.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/of_graph.h>
#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include "../rockchip/rockchip_drm_drv.h"

#define encoder_to_vga_encoder(x) container_of(x, struct vga_encoder, encoder)

struct vga_encoder {
	struct drm_encoder encoder;
	struct device *dev;
	struct regulator *vaa_reg;
	struct gpio_desc *psave_gpio;

	struct mutex enable_lock;
	bool enabled;
};

static void vga_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs vga_encoder_funcs = {
	.destroy = vga_encoder_destroy,
};

static void vga_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct vga_encoder *vga = encoder_to_vga_encoder(encoder);
    int ret;

	mutex_lock(&vga->enable_lock);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		if (vga->enabled)
			goto out;

		if (!IS_ERR(vga->vaa_reg))
			ret = regulator_enable(vga->vaa_reg);
            if( ret < 0) 
                pr_debug("vga encoder vaa reg fail\n");

		if (vga->psave_gpio)
            gpiod_direction_output(vga->psave_gpio, 1);
			//gpiod_set_value(vga->psave_gpio, 1);

		vga->enabled = true;
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		if (!vga->enabled)
			goto out;

		if (vga->psave_gpio)
            gpiod_direction_output(vga->psave_gpio, 0);
			//gpiod_set_value(vga->psave_gpio, 0);

		if (!IS_ERR(vga->vaa_reg))
			ret = regulator_enable(vga->vaa_reg);
            if( ret < 0) 
                pr_debug("vga encoder vaa reg fail\n");

		vga->enabled = false;
		break;
	default:
		break;
	}

out:
	mutex_unlock(&vga->enable_lock);
}

static bool vga_encoder_mode_fixup(struct drm_encoder *encoder,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void vga_encoder_prepare(struct drm_encoder *encoder)
{
    dump_stack();
}

static void vga_encoder_mode_set(struct drm_encoder *encoder,
					struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
}

static int
vga_encoder_atomic_check(struct drm_encoder *encoder,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

    pr_info("aaaaa  %s %d %d \n",__FUNCTION__,__LINE__, conn_state->connector->connector_type);
	//s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_mode = 0;
	s->output_type = DRM_MODE_CONNECTOR_VGA;//conn_state->connector->connector_type;

	return 0;
}

static void vga_encoder_commit(struct drm_encoder *encoder)
{
	vga_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
}

static void vga_encoder_disable(struct drm_encoder *encoder)
{
	vga_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static const struct drm_encoder_helper_funcs vga_encoder_helper_funcs = {
	.dpms = vga_encoder_dpms,
	.mode_fixup = vga_encoder_mode_fixup,
	.prepare = vga_encoder_prepare,
	.mode_set = vga_encoder_mode_set,
	.commit = vga_encoder_commit,
	.disable = vga_encoder_disable,
	.atomic_check = vga_encoder_atomic_check,
};

/*
 * Component helper functions
 */

static int vga_encoder_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct vga_encoder *vga = dev_get_drvdata(dev);
	struct device_node *np = vga->encoder.of_node;
	struct drm_device *drm_dev = data;

	vga->encoder.possible_crtcs = drm_of_find_possible_crtcs(drm_dev, np);

	drm_encoder_helper_add(&vga->encoder, &vga_encoder_helper_funcs);
	drm_encoder_init(drm_dev, &vga->encoder, &vga_encoder_funcs,
			 DRM_MODE_ENCODER_DAC,NULL);

	return component_bind_all(dev, drm_dev);
}

static void vga_encoder_unbind(struct device *dev, struct device *master,
				    void *data)
{
	struct vga_encoder *vga = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	component_unbind_all(dev, drm_dev);
	vga->encoder.funcs->destroy(&vga->encoder);
}

static const struct component_ops vga_encoder_ops = {
	.bind = vga_encoder_bind,
	.unbind = vga_encoder_unbind,
};

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int vga_encoder_master_bind(struct device *dev)
{
	return 0;
}

static void vga_encoder_master_unbind(struct device *dev)
{
	/* do nothing */
}

static const struct component_master_ops vga_encoder_master_ops = {
	.bind = vga_encoder_master_bind,
	.unbind = vga_encoder_master_unbind,
};

static int vga_encoder_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *port, *connector_node;
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;
	struct vga_encoder *vga;
	int ret;

	if (!np)
		return -ENODEV;

	vga = devm_kzalloc(dev, sizeof(*vga), GFP_KERNEL);
	if (!vga)
		return -ENOMEM;

	vga->dev = dev;
	dev_set_drvdata(dev, vga);
	mutex_init(&vga->enable_lock);

	vga->psave_gpio = devm_gpiod_get(dev, "psave", GPIOD_OUT_LOW);
	if (IS_ERR(vga->psave_gpio)) {
		ret = PTR_ERR(vga->psave_gpio);
		dev_err(dev, "failed to request GPIO: %d\n", ret);
		return ret;
	}

	vga->enabled = false;
	vga->vaa_reg = devm_regulator_get_optional(dev, "vaa");
	vga->encoder.of_node = np;

	port = of_graph_get_port_by_id(dev->of_node, 1);
	if (port) {
		struct device_node *endpoint;

		endpoint = of_get_child_by_name(port, "endpoint");
		if (endpoint) {
			connector_node = of_graph_get_remote_port_parent(endpoint);
			of_node_put(endpoint);
		}

		of_node_put(port);
	}

	if (!of_drm_find_connector(connector_node))
		return -EPROBE_DEFER;

	component_match_add(dev, &match, compare_of, connector_node);

	ret = drm_encoder_add(&vga->encoder);
	if (ret < 0)
		return ret;

	ret = component_master_add_with_match(dev, &vga_encoder_master_ops, match);
	if (ret < 0)
		goto err_encoder_remove;

	ret = component_add(dev, &vga_encoder_ops);
	if (ret < 0)
		goto err_master_remove;

	return 0;

err_master_remove:
	component_master_del(&pdev->dev, &vga_encoder_master_ops);
err_encoder_remove:
	drm_encoder_remove(&vga->encoder);

	return ret;
}

static int vga_encoder_remove(struct platform_device *pdev)
{
	struct vga_encoder *vga = dev_get_drvdata(&pdev->dev);

	component_del(&pdev->dev, &vga_encoder_ops);
	component_master_del(&pdev->dev, &vga_encoder_master_ops);
	drm_encoder_remove(&vga->encoder);

	return 0;
}

static const struct of_device_id vga_encoder_ids[] = {
	{ .compatible = "adi,adv7123", },
	{},
};
MODULE_DEVICE_TABLE(of, vga_encoder_ids);

static struct platform_driver vga_encoder_driver = {
	.probe  = vga_encoder_probe,
	.remove = vga_encoder_remove,
	.driver = {
		.name = "vga-encoder",
		.of_match_table = vga_encoder_ids,
	},
};

static const struct of_device_id rcar_du_of_table[] = {
	{ .compatible = "renesas,du-r8a7779" },
	{ .compatible = "renesas,du-r8a7790" },
	{ .compatible = "renesas,du-r8a7791" },
	{ }
};

static int __init vga_encoder_init(void)
{
	struct device_node *np;

	/*
	 * Play nice with rcar-du that is having its own implementation
	 * of the adv7123 binding implementation and is not yet
	 * converted to using components.
	 */
	np = of_find_matching_node(NULL, rcar_du_of_table);
	if (np) {
		of_node_put(np);
		return 0;
	}

	return platform_driver_register(&vga_encoder_driver);
}

static void __exit vga_encoder_exit(void)
{
	platform_driver_unregister(&vga_encoder_driver);
}

module_init(vga_encoder_init);
module_exit(vga_encoder_exit);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("Simple vga converter");
MODULE_LICENSE("GPL");
