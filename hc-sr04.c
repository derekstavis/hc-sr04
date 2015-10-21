/** 
    Copyright (C) 2014 Sergio Tanzilli
    Copyright (C) 2015 Derek Stavis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**/

#include <linux/kernel.h>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/device.h>
#include <linux/interrupt.h>

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Sergio Tanzilli / Derek Stavis");
MODULE_DESCRIPTION("Driver for HC-SR04 ultrasonic sensor");

static int gpio_echo = 17;
static int gpio_trigger = 4;

static int gpio_irq = -1;
static int valid_value = 0;

static ktime_t echo_start;
static ktime_t echo_end;

/* Module param for GPIO configuration */
module_param(gpio_trigger, int, 0);
MODULE_PARM_DESC(gpio_trigger, "GPIO which HC-SR04 trigger is connected to. Defaults to 4");

module_param(gpio_echo, int, 0);
MODULE_PARM_DESC(gpio_echo, "GPIO which HC-SR04 echo is connected to. Defaults to 17");

// This function is called when you write something on /sys/class/hc_sr04/value
static ssize_t hc_sr04_value_write(struct class *class,
                                   struct class_attribute *attr,
                                   const char *buf, size_t len) {
  return -EINVAL;
}

// This function is called when you read /sys/class/hc_sr04/value
static ssize_t hc_sr04_value_read(struct class *class,
                                  struct class_attribute *attr, char *buf) {
  int counter;

  // Send a 10uS pulse to the TRIGGER line
  gpio_set_value(gpio_trigger, 1);
  udelay(10);
  gpio_set_value(gpio_trigger, 0);
  valid_value = 0;

  counter = 0;
  while (0 == valid_value) {
    // Out of range
    if (++counter > 23200) {
      return sprintf(buf, "%d\n", -1);
    }
    udelay(1);
  }

  return sprintf(buf, "%lld\n", ktime_to_us(ktime_sub(echo_end, echo_start)));
}

// Sysfs definitions for hc_sr04 class
static struct class_attribute hc_sr04_class_attrs[] = {
    __ATTR(value, S_IRUGO | S_IWUSR, hc_sr04_value_read, hc_sr04_value_write),
    __ATTR_NULL,
};

// Name of directory created in /sys/class
static struct class hc_sr04_class = {
    .name = "distance",
    .owner = THIS_MODULE,
    .class_attrs = hc_sr04_class_attrs,
};

// Interrupt handler on ECHO signal
static irqreturn_t hc_sr04_isr(int irq, void *data) {
  ktime_t ktime_dummy;

  if (0 == valid_value) {
    ktime_dummy = ktime_get();
    if (1 == gpio_get_value(gpio_echo)) {
      echo_start = ktime_dummy;
    } else {
      echo_end = ktime_dummy;
      valid_value = 1;
    }
  }

  return IRQ_HANDLED;
}

static int hc_sr04_init(void) {
  int ret;

  printk(KERN_INFO "HC-SR04: Driver v0.32 initializing on GPIOs %d and %d\n",
         gpio_trigger, gpio_echo);

  if (class_register(&hc_sr04_class) < 0) {
    goto fail;
  }

  ret = gpio_request(gpio_trigger, "hc-sr04.gpio.trigger");
  if (0 != ret) {
    printk(KERN_ERR "HC-SR04: Error requesting GPIO %d.\n", gpio_trigger);
    goto fail;
  }

  ret = gpio_request(gpio_echo, "hc-sr04.gpio.echo");
  if (0 != ret) {
    printk(KERN_ERR "HC-SR04: Error requesting GPIO %d.\n", gpio_echo);
    goto fail;
  }

  ret = gpio_direction_output(gpio_trigger, 0);
  if (0 != ret) {
    printk(KERN_ERR "HC-SR04: Error setting GPIO %d direction.\n",
           gpio_trigger);
    goto fail;
  }

  ret = gpio_direction_input(gpio_echo);
  if (0 != ret) {
    printk(KERN_ERR "HC-SR04: Error setting GPIO %d direction\n", gpio_echo);
    goto fail;
  }

  // http://lwn.net/Articles/532714/
  ret = gpio_to_irq(gpio_echo);
  if (ret < 0) {
    printk(KERN_ERR "HC-SR04: Error requesting IRQ.\n");
    goto fail;
  } else {
    gpio_irq = ret;
  }

#define IRQ_FLAG IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_DISABLED

  ret = request_irq(gpio_irq, hc_sr04_isr, IRQ_FLAG, "hc-sr04.trigger", NULL);

  if (ret) {
    printk(KERN_ERR "HC-SR04: Error requesting IRQ: %d\n", ret);
    goto fail;
  }

  printk(KERN_INFO "HC-SR04: Ready!\n");

  return 0;

fail:
  return -1;
}

static void hc_sr04_exit(void) {
  if (-1 != gpio_irq) {
    free_irq(gpio_irq, NULL);
  }

  gpio_free(gpio_trigger);
  gpio_free(gpio_echo);

  class_unregister(&hc_sr04_class);

  printk(KERN_INFO "HC-SR04: Driver unloaded.\n");
}

module_init(hc_sr04_init);
module_exit(hc_sr04_exit);
