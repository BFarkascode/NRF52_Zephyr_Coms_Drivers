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
#include <zephyr/drivers/spi.h>

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

//uart1
const struct device *uart_ext = DEVICE_DT_GET(DT_NODELABEL(uart1));					//get uart1 device pointer

static uint8_t uart_ext_rx_buf[500] = {0};											//rx buffer

static int message_length  = 0;														//Rx len

static int message_received  = 0;													//message received flag

static int message_sent  = 0;														//message sent flag

static void uart_ext_cb(const struct device *dev, struct uart_event *evt, void *user_data)	//callback function
{
	switch (evt->type) {
	
	//below are all the events that could occur within the uart driver
	//we will fill up only the ones that ar relevant to us

	case UART_TX_DONE:

		//TX buffer content has been sent over
		message_sent = 1;

		break;

	case UART_TX_ABORTED:
		// not used
		break;
		
	case UART_RX_RDY:

		//triggered if buffer is ready or when the timeout has occurred
		message_length = evt->data.rx.len;

		message_received = 1;

		break;

	case UART_RX_BUF_REQUEST:
		// not used
		break;

	case UART_RX_BUF_RELEASED:
		// not used
		break;
		
	case UART_RX_DISABLED:
		//triggered if and when the receiver is stopped or the buffer is full, or if uart is disabled

		uart_rx_enable(uart_ext, uart_ext_rx_buf, sizeof(uart_ext_rx_buf), 10000);			//restart uart rx

		// do something
		break;

	case UART_RX_STOPPED:
		
		// not used
		break;
		
	default:
		break;
	}
}

//spi1
#define SPI1_NODE_BMP280 DT_NODELABEL(bmp280_spi)
static const struct spi_dt_spec spi1_spec = SPI_DT_SPEC_GET(SPI1_NODE_BMP280, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);


//spi read (transceive) function
static int spi_read_from_bmp_reg( struct spi_dt_spec spi_node_struct, uint8_t reg, uint8_t *data, uint8_t size, uint8_t readout_count)
{
	int err;

	uint8_t tx_buffer = reg;
	struct spi_buf tx_spi_buf			= {.buf = (void *)&tx_buffer, .len = 1};
	struct spi_buf_set tx_spi_buf_set 	= {.buffers = &tx_spi_buf, .count = 1};
	struct spi_buf rx_spi_bufs 			= {.buf = data, .len = size};
	struct spi_buf_set rx_spi_buf_set	= {.buffers = &rx_spi_bufs, .count = readout_count};

	err = spi_transceive_dt(&spi_node_struct, &tx_spi_buf_set, &rx_spi_buf_set);					//we send over the register, followed by readout. data[0] will be dummy.
	if (err < 0) {
		LOG_ERR("spi_transceive_dt() failed, err: %d", err);
		return err;
	}

	return 0;
}

//write spi
static int spi_write_byte_to_bmp_reg(struct spi_dt_spec spi_node_struct, uint8_t reg, uint8_t value)
{
	int err;

	uint8_t tx_buf[] = {(reg & 0x7F), value};										//writing is register address + MSB = 0 for the BMP280
	struct spi_buf 		tx_spi_buf 		= {.buf = tx_buf, .len = sizeof(tx_buf)};
	struct spi_buf_set 	tx_spi_buf_set	= {.buffers = &tx_spi_buf, .count = 1};

	err = spi_write_dt(&spi_node_struct, &tx_spi_buf_set);
	if (err < 0) {
		LOG_ERR("spi_write_dt() failed, err %d", err);
		return err;
	}

	return 0;
}


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
	uint8_t chip_ID_reg = 0xd0;
	uint8_t chip_ID[1] = {0x0};

	ret = 1;																	//we give ret a non-zero value
	ret = i2c_reg_read_byte_dt(&spec_i2c0, chip_ID_reg, chip_ID);				//the single readout macro demands the node handle, the register and a buffer pointer
//	while(ret);																	//we can block until the macro is finished
																				//ret - the macro's return value - is "0" for success and a negative number for failure 
	if (ret < 0) {
		printk("BMP280 ID readout error ...\n\r");
//		return -1;																//removed the blocking
	}

	printk(" \n\r");
	printk("BMP280 chip ID on i2c 0x77 is 0x%x ...\n\r",chip_ID[0]);
	printk(" \n\r");

	//uart1 setup
	if (!device_is_ready(uart_ext)) {
		printk("Uart1 not ready... \r\n");
    	return -1;
	}

	ret = uart_callback_set(uart_ext, uart_ext_cb, NULL);							//we connect the callback to uart1
	if (ret) {
		return -1;
	}

	uart_rx_enable(uart_ext, uart_ext_rx_buf, sizeof(uart_ext_rx_buf), 10000);		//we enable uart1 with a timeout period of 100 ms
																					//timeout starts after at least a first byte has been received
	
	//spi1 device check
//	ret = spi_is_ready_dt(&spi1_spec);
	if (!device_is_ready(spi1_spec.bus)) {											//test device
		LOG_ERR("Error: SPI device is not ready... ");
		return -1;
	}

	uint8_t chip_ID_spi[2] = {0x0,0x0};										//readout is always n+1 due to the register being sent over taking up a dummy

	spi_read_from_bmp_reg(spi1_spec, chip_ID_reg, chip_ID_spi, 2, 1);

	printk(" \n\r");
	printk("BMP280 chip ID on SPI CS P0.30 is 0x%x ...\n\r", chip_ID_spi[1]);		//[0] will be a dummy 0xFF
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

		if(message_received == 1){

			printk("Incomign message length is %i: ", message_length);

			printk(uart_ext_rx_buf);													//we print out the character array/string buffer

			printk(" \n\r");

			message_length = 0 ;

			//reset uart rx and buffers
			uart_rx_disable(uart_ext);													//disable uart rx and relase rx buffer(s)
																						//disable should lead to an auto-restart through uart events and callback
																						//mind, this DOES NOT wipe the buffer!

			//send back an ACK to the other device
			uint8_t uart_ext_tx_buf[] =  {"Message received \n\r"};

			ret = uart_tx(uart_ext, uart_ext_tx_buf, sizeof(uart_ext_tx_buf), SYS_FOREVER_US);	//send an ACK
		
			if (ret) {
				printk("Serial Tx error ...\n\r");
				return -1;
			}		

			message_received = 0;

		} else {

			//do nothing

		}

		if(message_sent == 1){

			message_sent = 0;
			printk("Reply sent ...\n\r");			//print a feedback message

		} else {

			//do nothing

		}

		k_msleep(1);						//to have the superloop do something when there is no activity

	}

	return 0;
}