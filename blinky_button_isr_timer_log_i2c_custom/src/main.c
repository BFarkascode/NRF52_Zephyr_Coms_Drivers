/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>				//include uart module
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>

//define drivers
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define BUTTON1_NODE DT_ALIAS(button1)
#define BUTTON2_NODE DT_ALIAS(button2)

//extract dt handles
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec ext_led = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec btn1 = GPIO_DT_SPEC_GET(BUTTON1_NODE, gpios);
static const struct gpio_dt_spec btn2 = GPIO_DT_SPEC_GET(BUTTON2_NODE, gpios);


//set up timer callback
static void timer_0_expiry_handler(struct k_timer *dummy)
{
    /*Interrupt Context - System Timer ISR */
	gpio_pin_toggle_dt(&ext_led);

}

//set up timer
K_TIMER_DEFINE(timer_0, timer_0_expiry_handler, NULL);

//set up the button flags
static int btn1_pushed = 0;
static int btn2_pushed = 0;

//set up button callbacks
void btn1_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	btn1_pushed = 1;
}

void btn2_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	btn2_pushed = 1;
}

//gpio callback structs
static struct gpio_callback btn1_callback;
static struct gpio_callback btn2_callback;

//uart0
const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart0));					//get uart0 device pointer

LOG_MODULE_REGISTER(blinky_button_isr_log,LOG_LEVEL_DBG);	

//i2c0
#define I2C0_NODE DT_NODELABEL(bmp280_sensor)

static const struct i2c_dt_spec spec_i2c0 = I2C_DT_SPEC_GET(I2C0_NODE);


int main(void)
{
	
	int ret;

	if (!gpio_is_ready_dt(&led)) {
		return -1;
	}

	if (!gpio_is_ready_dt(&ext_led)) {
		return -1;
	}

	if (!gpio_is_ready_dt(&btn1)) {
		return -1;
	}

	if (!gpio_is_ready_dt(&btn2)) {
		return -1;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

	if (ret < 0) {
		return -1;
	}

	ret = gpio_pin_configure_dt(&ext_led, GPIO_OUTPUT_ACTIVE);

	if (ret < 0) {
		return -1;
	}

	ret = gpio_pin_configure_dt(&btn1, GPIO_INPUT);

	if (ret < 0) {
		return -1;
	}

	ret = gpio_pin_configure_dt(&btn2, GPIO_INPUT);

	if (ret < 0) {
		return -1;
	}

	//gpio isr configure
	ret = gpio_pin_interrupt_configure_dt(&btn1, GPIO_INT_EDGE_TO_ACTIVE);

	if (ret < 0) {
		return -1;
	}

	ret = gpio_pin_interrupt_configure_dt(&btn2, GPIO_INT_EDGE_TO_ACTIVE);

	if (ret < 0) {
		return -1;
	}

	//gpio callback struct fill up
	gpio_init_callback(&btn1_callback, btn1_pressed, BIT(btn1.pin));

	gpio_init_callback(&btn2_callback, btn2_pressed, BIT(btn2.pin));

	//callback attach to gpio driver
	gpio_add_callback(btn1.port, &btn1_callback);

	gpio_add_callback(btn2.port, &btn2_callback);

	//i2c0 device check
	if (!device_is_ready(spec_i2c0.bus)) {
		printk("I2C bus %s is not ready!\n\r",spec_i2c0.bus->name);
		return -1;
	}

	//read out BMP280 chip ID. It should be 0x58.
	uint8_t chip_ID_reg = 0xD0;
	uint8_t chip_ID[1] = {0x0};

	ret = 1;																	//we give ret a non-zero value
	ret = i2c_reg_read_byte_dt(&spec_i2c0, chip_ID_reg, chip_ID);				//the single readout macro demands the node handle, the register and a buffer pointer
//	while(ret);																	//we can block until the macro is finished
																				//ret - the macro's return value - is "0" for success and a negative number for failure 

	if (ret < 0) {
		printk("BMP280 ID readout error ...\n\r");
		return -1;
	}

	printk(" \n\r");
	printk("BMP280 chip ID on 0x77 is 0x%x ...\n\r",chip_ID[0]);
	printk(" \n\r");

	while (1) {

		if(btn1_pushed == 1){

			LOG_WRN("BTN1 pushed! \n");

			printk("Quarter of a second delay\n");

			printk(" \n");

			k_timer_start(&timer_0, K_MSEC(250), K_MSEC(250));

			btn1_pushed = 0;

		} else {

			//do nothing

		} 		
		
		
		if(btn2_pushed == 1){

			LOG_WRN("BTN2 pushed! \n");

			printk("One second delay\n");

			printk(" \n");

			k_timer_start(&timer_0, K_MSEC(1000), K_MSEC(1000));

			btn2_pushed = 0;

		} else {

			//do nothing

		}

		k_msleep(1);						//to have the superloop do something when there is no activity

	}

	return 0;
}