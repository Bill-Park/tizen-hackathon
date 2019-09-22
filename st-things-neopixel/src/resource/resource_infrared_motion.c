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

#include <peripheral_io.h>
#include "resource/resource_infrared_motion.h"
#include "log.h"

enum {
	MOTION_HANDLE_ERROR_NONE = 0,                /**< Successful */
	MOTION_HANDLE_ERROR_NOT_OPEN,
	MOTION_HANDLE_ERROR_INVALID_PIN
} motion_handle_error_e;

struct resource_im_interrupted_data {
	resource_infrared_motion_interrupted_cb interrupted_cb;
	void *interrupted_cb_user_data;
	uint32_t motion_value;
};

static peripheral_gpio_h g_sensor_h = NULL;
static int g_pin_num = -1;
static struct resource_im_interrupted_data g_interrupted_data;

static int _resource_validate_infrared_motion(int pin_num)
{
	int ret = MOTION_HANDLE_ERROR_NONE;

	if (!g_sensor_h)
	{
		ret = MOTION_HANDLE_ERROR_NOT_OPEN;
	} else if (g_pin_num != pin_num) {
		ret = MOTION_HANDLE_ERROR_INVALID_PIN;
	}

	return ret;
}

static int resource_open_infrared_motion(int pin_num)
{
	peripheral_gpio_h temp = NULL;

	int ret = peripheral_gpio_open(pin_num, &temp);
	if (ret) {
		peripheral_gpio_close(temp);
		_E("peripheral_gpio_open failed.");
		return -1;
	}

	ret = peripheral_gpio_set_direction(temp, PERIPHERAL_GPIO_DIRECTION_IN);
	if (ret) {
		peripheral_gpio_close(temp);
		_E("peripheral_gpio_set_direction failed.");
		return -1;
	}

	g_sensor_h = temp;
	g_pin_num = pin_num;

	return 0;
}

int resource_read_infrared_motion(int pin_num, uint32_t *out_value)
{
	int ret = PERIPHERAL_ERROR_NONE;
	int ret_valid = MOTION_HANDLE_ERROR_NONE;

	ret_valid = _resource_validate_infrared_motion(pin_num);
	if (ret_valid != MOTION_HANDLE_ERROR_NONE) {
		if (ret_valid == MOTION_HANDLE_ERROR_NOT_OPEN) {
			ret = resource_open_infrared_motion(pin_num);
			retv_if(ret != PERIPHERAL_ERROR_NONE, -1);
		} else if (ret_valid == MOTION_HANDLE_ERROR_INVALID_PIN) {
			_E("Invalid pin number.");
			return -1;
		}
	}

	ret = peripheral_gpio_read(g_sensor_h, out_value);
	retv_if(ret != PERIPHERAL_ERROR_NONE, -1);

	return 0;
}

void resource_close_infrared_motion(void)
{
	if (!g_sensor_h) return;

	_I("Infrared Motion Sensor is finishing...");

	if (g_interrupted_data.interrupted_cb) {
		peripheral_gpio_unset_interrupted_cb(g_sensor_h);
		g_interrupted_data.interrupted_cb = NULL;
		g_interrupted_data.interrupted_cb_user_data = NULL;
		g_interrupted_data.motion_value = 0;
	}

	peripheral_gpio_close(g_sensor_h);

	g_sensor_h = NULL;
	g_pin_num = -1;
}

void _resoucre_motion_interrupted_cb (peripheral_gpio_h gpio, peripheral_error_e error, void *user_data)
{
	if (!g_sensor_h) return;

	g_interrupted_data.motion_value = !g_interrupted_data.motion_value;

	g_interrupted_data.interrupted_cb(g_interrupted_data.motion_value, g_interrupted_data.interrupted_cb_user_data);
}

int resource_set_interrupted_cb_infrared_motion(int pin_num, resource_infrared_motion_interrupted_cb interrupted_cb, void *user_data)
{
	int ret = PERIPHERAL_ERROR_NONE;
	int ret_valid = MOTION_HANDLE_ERROR_NONE;

	ret_valid = _resource_validate_infrared_motion(pin_num);
	if (ret_valid == MOTION_HANDLE_ERROR_NOT_OPEN) {
		ret = resource_open_infrared_motion(pin_num);
		retv_if(ret != PERIPHERAL_ERROR_NONE, -1);
	} else if (ret_valid == MOTION_HANDLE_ERROR_INVALID_PIN) {
		_E("Invalid pin number.");
		return -1;
	}

	//ret = peripheral_gpio_set_edge_mode(g_sensor_h, PERIPHERAL_GPIO_EDGE_BOTH);
	ret = peripheral_gpio_set_edge_mode(g_sensor_h, PERIPHERAL_GPIO_EDGE_FALLING);
	retv_if(ret != PERIPHERAL_ERROR_NONE, -1);

	g_interrupted_data.motion_value = 0;
	ret = peripheral_gpio_read(g_sensor_h, &g_interrupted_data.motion_value);
	retv_if(ret != PERIPHERAL_ERROR_NONE, -1);

	ret = peripheral_gpio_set_interrupted_cb(g_sensor_h, _resoucre_motion_interrupted_cb, &g_interrupted_data);
	goto_if(ret != PERIPHERAL_ERROR_NONE, ERROR);

	g_interrupted_data.interrupted_cb = interrupted_cb;
	g_interrupted_data.interrupted_cb_user_data = user_data;

	return 0;

ERROR:
	_E("failed to read gpio");
	peripheral_gpio_unset_interrupted_cb(g_sensor_h);
	g_interrupted_data.interrupted_cb = NULL;
	g_interrupted_data.interrupted_cb_user_data = NULL;
	g_interrupted_data.motion_value = 0;
	return -1;
}

int resource_unset_interrupted_cb_infrared_motion(int pin_num)
{
	int ret = PERIPHERAL_ERROR_NONE;
	int ret_valid = MOTION_HANDLE_ERROR_NONE;

	ret_valid = _resource_validate_infrared_motion(pin_num);
	if (ret_valid == MOTION_HANDLE_ERROR_NOT_OPEN) {
		_E("No open handle.");
		return -1;
	} else if (ret_valid == MOTION_HANDLE_ERROR_INVALID_PIN) {
		_E("Invalid pin number.");
		return -1;
	}

	ret = peripheral_gpio_unset_interrupted_cb(g_sensor_h);
	retv_if(ret != PERIPHERAL_ERROR_NONE, -1);

	g_interrupted_data.interrupted_cb = NULL;
	g_interrupted_data.interrupted_cb_user_data = NULL;
	g_interrupted_data.motion_value = 0;

	return 0;
}
