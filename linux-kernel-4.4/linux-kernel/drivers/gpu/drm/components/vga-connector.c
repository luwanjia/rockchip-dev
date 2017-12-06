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

#define connector_to_vga_connector(x) container_of(x, struct vga_connector, connector)

struct vga_connector {
	struct drm_connector connector;
	struct device *dev;
	struct i2c_adapter *ddc;
	struct drm_encoder *encoder;
};

enum drm_connector_status vga_connector_detect(struct drm_connector *connector,
					    bool force)
{
	struct vga_connector *vga = connector_to_vga_connector(connector);

	if (!vga->ddc)
		return connector_status_unknown;

	if (drm_probe_ddc(vga->ddc))
		return connector_status_connected;

	return connector_status_disconnected;
}

void vga_connector_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

struct drm_connector_funcs vga_connector_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	//.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = vga_connector_detect,
	.destroy = vga_connector_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/*
 * Connector helper functions
 */

static int vga_connector_connector_get_modes(struct drm_connector *connector)
{
	struct vga_connector *vga = connector_to_vga_connector(connector);
	struct edid *edid;
	int ret = 0;

	if (!vga->ddc)
		return 0;

	edid = drm_get_edid(connector, vga->ddc);
	if (edid) {
		drm_mode_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return ret;
}

static int vga_connector_connector_mode_valid(struct drm_connector *connector,
					struct drm_display_mode *mode)
{
	return MODE_OK;
}

static struct drm_encoder
*vga_connector_connector_best_encoder(struct drm_connector *connector)
{
	struct vga_connector *vga = connector_to_vga_connector(connector);

	return vga->encoder;
}

static struct drm_connector_helper_funcs vga_connector_connector_helper_funcs = {
	.get_modes = vga_connector_connector_get_modes,
	.best_encoder = vga_connector_connector_best_encoder,
	.mode_valid = vga_connector_connector_mode_valid,
};


static int vga_connector_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct vga_connector *vga = dev_get_drvdata(dev);
	struct drm_device *drm = data;
	struct device_node *endpoint, *encp = NULL;

	vga->connector.polled = DRM_CONNECTOR_POLL_CONNECT |
				DRM_CONNECTOR_POLL_DISCONNECT;

	drm_connector_helper_add(&vga->connector,
				 &vga_connector_connector_helper_funcs);
	drm_connector_init(drm, &vga->connector, &vga_connector_connector_funcs,
			   DRM_MODE_CONNECTOR_VGA);

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (endpoint)
		encp = of_graph_get_remote_port_parent(endpoint);
	of_node_put(endpoint);

	if (!encp)
		return -ENODEV;

	vga->encoder = of_drm_find_encoder(encp);
	of_node_put(encp);

	if (!vga->encoder)
		return -EPROBE_DEFER;

	drm_mode_connector_attach_encoder(&vga->connector, vga->encoder);

	return 0;
}

static void vga_connector_unbind(struct device *dev, struct device *master,
				    void *data)
{
	struct vga_connector *vga = dev_get_drvdata(dev);

	vga->connector.funcs->destroy(&vga->connector);
}

static const struct component_ops vga_connector_ops = {
	.bind = vga_connector_bind,
	.unbind = vga_connector_unbind,
};

static int vga_connector_probe(struct platform_device *pdev)
{
	struct device_node *ddc_node, *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct vga_connector *vga;
	int ret;

	if (!np)
		return -ENODEV;

	vga = devm_kzalloc(dev, sizeof(*vga), GFP_KERNEL);
	if (!vga)
		return -ENOMEM;

	vga->dev = dev;
	vga->connector.of_node = np;

	dev_set_drvdata(dev, vga);

	ddc_node = of_parse_phandle(np, "ddc-i2c-bus", 0);
	if (ddc_node) {
		vga->ddc = of_find_i2c_adapter_by_node(ddc_node);
		of_node_put(ddc_node);
		if (!vga->ddc) {
			dev_dbg(vga->dev, "failed to read ddc node\n");
			return -EPROBE_DEFER;
		}
	} else {
		dev_dbg(vga->dev, "no ddc property found\n");
	}

	ret = drm_connector_add(&vga->connector);
	if (ret < 0)
		return ret;

	return component_add(dev, &vga_connector_ops);
}

static int vga_connector_remove(struct platform_device *pdev)
{
	struct vga_connector *vga = dev_get_drvdata(&pdev->dev);

	component_del(&pdev->dev, &vga_connector_ops);
	drm_connector_remove(&vga->connector);

	return 0;
}

static const struct of_device_id vga_connector_ids[] = {
	{ .compatible = "vga-connector", },
	{},
};
MODULE_DEVICE_TABLE(of, vga_connector_ids);

static struct platform_driver vga_connector_driver = {
	.probe  = vga_connector_probe,
	.remove = vga_connector_remove,
	.driver = {
		.name = "vga-connector",
		.of_match_table = vga_connector_ids,
	},
};

static const struct of_device_id rcar_du_of_table[] = {
	{ .compatible = "renesas,du-r8a7779" },
	{ .compatible = "renesas,du-r8a7790" },
	{ .compatible = "renesas,du-r8a7791" },
	{ }
};

static int __init vga_connector_init(void)
{
	struct device_node *np;

	/*
	 * Play nice with rcar-du that is having its own implementation
	 * of the vga-connector binding implementation and is not yet
	 * converted to using components.
	 */
	np = of_find_matching_node(NULL, rcar_du_of_table);
	if (np) {
		of_node_put(np);
		return 0;
	}

	return platform_driver_register(&vga_connector_driver);
}

static void __exit vga_connector_exit(void)
{
	platform_driver_unregister(&vga_connector_driver);
}

module_init(vga_connector_init);
module_exit(vga_connector_exit);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("Simple vga converter");
MODULE_LICENSE("GPL");
