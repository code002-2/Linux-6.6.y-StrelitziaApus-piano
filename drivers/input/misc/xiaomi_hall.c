// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi hall sensor switches
 *
 * Reports the hall sensors of the keyboard cover, like the lid and the stand
 * of a folio, as input switches. The sensors are watched by a remote
 * processor, which raises separate interrupts for the open and the close
 * edge over smp2p; the gpio gives the state when the input device is opened.
 *
 * Written by sidharthify (Sidharth Sharma) <wednisegit@gmail.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

struct xiaomi_hall_switch {
	const char *label;
	struct gpio_desc *gpiod;
	unsigned int type;
	unsigned int code;
	int open_irq;
	int close_irq;
	struct input_dev *input;
};

struct xiaomi_hall {
	struct input_dev *input;
	int nswitches;
	struct xiaomi_hall_switch switches[];
};

static void xiaomi_hall_report(struct xiaomi_hall_switch *sw, bool closed)
{
	input_event(sw->input, sw->type, sw->code, closed);
	input_sync(sw->input);
}

static irqreturn_t xiaomi_hall_irq(int irq, void *data)
{
	struct xiaomi_hall_switch *sw = data;

	if (irq == sw->open_irq)
		xiaomi_hall_report(sw, false);
	else if (irq == sw->close_irq)
		xiaomi_hall_report(sw, true);

	return IRQ_HANDLED;
}

static int xiaomi_hall_open(struct input_dev *input)
{
	struct xiaomi_hall *hall = input_get_drvdata(input);
	int i, val;

	for (i = 0; i < hall->nswitches; i++) {
		val = gpiod_get_value_cansleep(hall->switches[i].gpiod);
		if (val >= 0)
			xiaomi_hall_report(&hall->switches[i], val);
	}

	return 0;
}

static int xiaomi_hall_parse_switch(struct device *dev,
				    struct fwnode_handle *child,
				    struct xiaomi_hall_switch *sw)
{
	struct device_node *np = to_of_node(child);
	int ret;

	ret = fwnode_property_read_string(child, "label", &sw->label);
	if (ret)
		return dev_err_probe(dev, ret, "switch without a label\n");

	ret = fwnode_property_read_u32(child, "linux,code", &sw->code);
	if (ret)
		return dev_err_probe(dev, ret, "%s: no linux,code\n", sw->label);

	if (fwnode_property_read_u32(child, "linux,input-type", &sw->type))
		sw->type = EV_SW;

	sw->gpiod = devm_fwnode_gpiod_get(dev, child, NULL, GPIOD_IN, sw->label);
	if (IS_ERR(sw->gpiod))
		return dev_err_probe(dev, PTR_ERR(sw->gpiod), "%s: no gpio\n",
				     sw->label);

	sw->open_irq = of_irq_get_byname(np, "open_irq");
	if (sw->open_irq < 0)
		return dev_err_probe(dev, sw->open_irq, "%s: no open_irq\n",
				     sw->label);

	sw->close_irq = of_irq_get_byname(np, "close_irq");
	if (sw->close_irq < 0)
		return dev_err_probe(dev, sw->close_irq, "%s: no close_irq\n",
				     sw->label);

	return 0;
}

static int xiaomi_hall_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *child;
	struct xiaomi_hall *hall;
	struct input_dev *input;
	int i = 0, n, ret;

	n = device_get_child_node_count(dev);
	if (!n)
		return -ENODEV;

	hall = devm_kzalloc(dev, struct_size(hall, switches, n), GFP_KERNEL);
	if (!hall)
		return -ENOMEM;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input->name = "xiaomi-hall";
	input->phys = "xiaomi-hall/input0";
	input->id.bustype = BUS_HOST;
	input->open = xiaomi_hall_open;
	input_set_drvdata(input, hall);
	hall->input = input;

	device_for_each_child_node(dev, child) {
		struct xiaomi_hall_switch *sw = &hall->switches[i];

		ret = xiaomi_hall_parse_switch(dev, child, sw);
		if (ret) {
			fwnode_handle_put(child);
			return ret;
		}

		sw->input = input;
		input_set_capability(input, sw->type, sw->code);
		i++;
	}
	hall->nswitches = i;

	ret = input_register_device(input);
	if (ret)
		return ret;

	for (i = 0; i < hall->nswitches; i++) {
		struct xiaomi_hall_switch *sw = &hall->switches[i];

		ret = devm_request_threaded_irq(dev, sw->open_irq, NULL,
						xiaomi_hall_irq,
						IRQF_TRIGGER_RISING | IRQF_ONESHOT,
						devm_kasprintf(dev, GFP_KERNEL,
							       "%s_open", sw->label),
						sw);
		if (ret)
			return ret;

		ret = devm_request_threaded_irq(dev, sw->close_irq, NULL,
						xiaomi_hall_irq,
						IRQF_TRIGGER_RISING | IRQF_ONESHOT,
						devm_kasprintf(dev, GFP_KERNEL,
							       "%s_close", sw->label),
						sw);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct of_device_id xiaomi_hall_of_match[] = {
	{ .compatible = "xiaomi-hall" },
	{ }
};
MODULE_DEVICE_TABLE(of, xiaomi_hall_of_match);

static struct platform_driver xiaomi_hall_driver = {
	.probe = xiaomi_hall_probe,
	.driver = {
		.name = "xiaomi-hall",
		.of_match_table = xiaomi_hall_of_match,
	},
};
module_platform_driver(xiaomi_hall_driver);

MODULE_DESCRIPTION("Xiaomi hall sensor switches");
MODULE_LICENSE("GPL");
