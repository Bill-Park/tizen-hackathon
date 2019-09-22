/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <tizen.h>
#include <service_app.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <Ecore.h>

#include "log.h"
#include "resource/resource_led.h"

#include <peripheral_io.h>

// Motion sensor information
#define SENSOR_MOTION_GPIO_NUMBER (35)

// internal rgb led
#define pin_num1 (32)
#define pin_num2 (33)

// Digital Button
#define button_1 (39)
#define button_2 (36)
#define button_3 (37)

#define buf_num(hundred, ten, one) (hundred - '0') * 100 + (ten - '0') * 10 + (one - '0')

// LED sensor information
#define SENSOR_LED_GPIO_NUMBER (32)

struct resource_interrupted_data {
	uint32_t motion_value;
	uint32_t btn_num;
};

struct led_brightness_data {
	uint32_t led1 ;
	uint32_t led2 ;
	uint32_t led3 ;
} ;

long period = 1 * 1000 * 1000 ;

peripheral_gpio_h led_h1 = NULL;
peripheral_gpio_h led_h2 = NULL;

peripheral_gpio_h btn_h1 = NULL;
peripheral_gpio_h btn_h2 = NULL;
peripheral_gpio_h btn_h3 = NULL;

struct resource_interrupted_data btn_data1 ;
struct resource_interrupted_data btn_data2 ;
struct resource_interrupted_data btn_data3 ;

peripheral_pwm_h pwm_h1 = NULL ;
peripheral_pwm_h pwm_h2 = NULL ;

peripheral_i2c_h i2c_h = NULL ;

peripheral_uart_h uart_h;
unsigned char buf[10] = { 0, };
unsigned char buf_read[14] = { 0, };

struct led_brightness_data led_brightness ;

static void _init_uart(int port, peripheral_uart_h *uart_h)
{
	peripheral_uart_open(port, uart_h);

	peripheral_uart_set_baud_rate(*uart_h, PERIPHERAL_UART_BAUD_RATE_9600) ;
	peripheral_uart_set_byte_size(*uart_h, PERIPHERAL_UART_BYTE_SIZE_8BIT) ;
	peripheral_uart_set_parity(*uart_h, PERIPHERAL_UART_PARITY_NONE) ;
	peripheral_uart_set_stop_bits(*uart_h, PERIPHERAL_UART_STOP_BITS_1BIT) ;
	peripheral_uart_set_flow_control(*uart_h, PERIPHERAL_UART_SOFTWARE_FLOW_CONTROL_NONE, PERIPHERAL_UART_HARDWARE_FLOW_CONTROL_NONE) ;
}

static int _init_pwm_state(int chip, int pin, peripheral_pwm_h *pwm_h)
{
	int ret = 0;

	ret = peripheral_pwm_open(chip, pin, pwm_h) ;
	if (ret != PERIPHERAL_ERROR_NONE) {
		// 에러처리
		_E("failed to open : %s", get_error_message(ret));
		return ret ;
	}

	// 주기 설정, 10ms
	ret = peripheral_pwm_set_period(*pwm_h, period);
	if (ret != PERIPHERAL_ERROR_NONE) {
		// 에러처리
		_E("failed to open : %s", get_error_message(ret));
		return ret ;
	}

	peripheral_pwm_set_duty_cycle(*pwm_h, 0) ;

	ret = peripheral_pwm_set_polarity(*pwm_h, PERIPHERAL_PWM_POLARITY_ACTIVE_HIGH);
	if (ret != PERIPHERAL_ERROR_NONE) {
		// 에러처리
		_E("failed to set : %s", get_error_message(ret));
		return ret ;
	}

	ret = peripheral_pwm_set_enabled(*pwm_h, true);
	if (ret != PERIPHERAL_ERROR_NONE) {
		_E("failed to enable : %s", get_error_message(ret));
		return ret ;
	}
	return 0 ;
}

static int set_led_pwm(struct led_brightness_data *led_brightness)
{
	_D("set led pwm 1 : %d 2 : %d 3 : %d", led_brightness->led1, led_brightness->led2, led_brightness->led3) ;
	int ret = 0 ;
	peripheral_pwm_set_duty_cycle(pwm_h1, period * led_brightness->led1 / 100) ;
	peripheral_pwm_set_duty_cycle(pwm_h2, period * led_brightness->led2 / 100) ;
	ret = peripheral_pwm_set_enabled(pwm_h1, true);
	if (ret != PERIPHERAL_ERROR_NONE) {
		_E("failed to enable pwm1 : %s", get_error_message(ret));
		return ret ;
	}
	ret = peripheral_pwm_set_enabled(pwm_h2, true);
	if (ret != PERIPHERAL_ERROR_NONE) {
		_E("failed to enable pwm2 : %s", get_error_message(ret));
		return ret ;
	}

	buf[0] = 0x40 ;
	buf[1] = led_brightness->led3 * 255 / 100 ;

	ret = peripheral_i2c_write(i2c_h, (uint8_t *)buf, 2);
	if (ret < 0) {
		// 에러처리
		_E("failed to write i2c: %s", get_error_message(ret));
	}

	return 0 ;
}

static int _change_led_state(int pin_num, int state)
{
	int ret = 0 ;
	// Write state to LED light
	ret = resource_write_led(pin_num, state);
	if (ret != 0) {
		_E("cannot write led data");
		return -1;
	}

	_I("LED State : %s", state ? "ON":"OFF");

	return 0;
}

Eina_Bool timer_serial_cb(void *data)
{
	int ret = 0 ;

	char *buf_led = malloc(20 * sizeof(char)); ;
	sprintf(buf_led, "[led%3d%3d%3d\r\n", \
			((struct led_brightness_data *)data)->led1, \
			((struct led_brightness_data *)data)->led2, \
			((struct led_brightness_data *)data)->led3) ;

	peripheral_uart_write(uart_h, buf_led, 15) ;
	free(buf_led) ;

	ret = peripheral_uart_read(uart_h, buf_read, 14) ;
	if (ret != PERIPHERAL_ERROR_NONE && ret != -11) {
		_E("failed to read uart : %s, %d", get_error_message(ret), ret);
		return ECORE_CALLBACK_CANCEL ;
	} else if (ret != -11) {
		//not Resource temporarily unavailable
		_D("read uart %s", buf_read) ;
		((struct led_brightness_data *)data)->led1 = buf_num(buf_read[4], buf_read[5], buf_read[6]) ;
		((struct led_brightness_data *)data)->led2 = buf_num(buf_read[7], buf_read[8], buf_read[9]) ;
		((struct led_brightness_data *)data)->led3 = buf_num(buf_read[10], buf_read[11], buf_read[12]) ;
		set_led_pwm(((struct led_brightness_data *)data)) ;
	}

    return ECORE_CALLBACK_RENEW;
}

static void button_cb(peripheral_gpio_h temp, peripheral_error_e error, void *user_data)
{
	_D("button cb %d and %d", ((struct resource_interrupted_data *)user_data)->btn_num, ((struct resource_interrupted_data *)user_data)->motion_value) ;

	if (error != 0) {
		_E("cb error %d", error) ;
	}
	char *buf_btn = malloc(20 * sizeof(char)); ;
	sprintf(buf_btn, "[btn%d\r\n", ((struct resource_interrupted_data *)user_data)->btn_num) ;

	peripheral_uart_write(uart_h, buf_btn, 8) ;
	free(buf_btn) ;

	return;
}

static int _init_button_cb(peripheral_gpio_h btn_h, struct resource_interrupted_data *btn_data)
{
	int ret = 0 ;
	ret = peripheral_gpio_set_edge_mode(btn_h, PERIPHERAL_GPIO_EDGE_FALLING);
	if (ret < 0) { // 에러처리
		_E("failed to set edge : %s", get_error_message(ret));
		return ret ;
	}

	ret = peripheral_gpio_read(btn_h, &btn_data->motion_value);
	if (ret < 0) { // 에러처리
		_E("failed to set gpio read : %s", get_error_message(ret));
		return ret ;
	}

	ret = peripheral_gpio_set_interrupted_cb(btn_h, button_cb, btn_data);
	if (ret < 0) { // 에러처리
		_E("failed to set interrupt : %s", get_error_message(ret));
		return ret ;
	}
	return 0 ;
}


static bool service_app_create(void *user_data)
{
	return true;
}

static void service_app_control(app_control_h app_control, void *user_data)
{
	int ret = 0;
	_D("test") ;

	_init_led_state(pin_num1, &led_h1) ;
	_init_led_state(pin_num2, &led_h2) ;

	//init pwm
	_init_pwm_state(1, 0, &pwm_h1) ;
	_init_pwm_state(0, 0, &pwm_h2) ;


	// i2c init
	int dac_address = 0x48 ;

	ret = peripheral_i2c_open(1, dac_address, &i2c_h) ;
	if (ret != PERIPHERAL_ERROR_NONE) {
		// 에러처리
		_E("failed to open i2c: %s", get_error_message(ret));
	}

	/*
	buf[0] = 0x40 ;
	buf[1] = 255 ;
	ret = peripheral_i2c_write(i2c_h, (uint8_t *)buf, 2);
	if (ret < 0) {
		// 에러처리
		_E("failed to write i2c: %s", get_error_message(ret));
	}
	*/

	int ttymxc = 5;

	_init_uart(ttymxc, &uart_h) ;

	led_brightness.led1 = 30 ;
	led_brightness.led2 = 30 ;
	led_brightness.led3 = 30 ;
	set_led_pwm(&led_brightness) ;

	_init_button(button_1, &btn_h1) ;
	_init_button(button_2, &btn_h2) ;
	_init_button(button_3, &btn_h3) ;

	btn_data1.btn_num = 1 ;
	btn_data1.motion_value = 0 ;
	ret = _init_button_cb(btn_h1, &btn_data1) ;
	if (ret < 0) {
		_E("failed to init cb %d", btn_data1.btn_num) ;
	}

	btn_data2.btn_num = 2 ;
	btn_data2.motion_value = 0 ;
	ret = _init_button_cb(btn_h2, &btn_data2) ;
	if (ret < 0) {
		_E("failed to init cb %d", btn_data2.btn_num) ;
	}

	btn_data3.btn_num = 3 ;
	btn_data3.motion_value = 0 ;
	ret = _init_button_cb(btn_h3, &btn_data3) ;
	if (ret < 0) {
		_E("failed to init cb %d", btn_data3.btn_num) ;
	}

	ecore_timer_add(1, timer_serial_cb, &led_brightness) ;
}

static void service_app_terminate(void *user_data)
{
	// Turn off LED light with __set_led()
	_change_led_state(SENSOR_LED_GPIO_NUMBER, 0);

	// Close LED resources
	resource_close_led();

	// close pwm
	peripheral_pwm_close(pwm_h1);
	peripheral_pwm_close(pwm_h2);

	//close i2c
	peripheral_i2c_close(i2c_h);

	//serial
	peripheral_uart_close(uart_h);

	//callback cancel

	FN_END;
}

int main(int argc, char *argv[])
{
	service_app_lifecycle_callback_s event_callback;

	event_callback.create = service_app_create;
	event_callback.terminate = service_app_terminate;
	event_callback.app_control = service_app_control;

	return service_app_main(argc, argv, &event_callback, NULL);
}
