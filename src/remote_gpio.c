/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Raspberry Pi Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>
#include "hardware/gpio.h"
#include "probe_config.h"
#include "remote_gpio.h"

bool gpio_remote_req(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
	/* tinyUSB gives you multiple callbacks, one per stage */
	static uint32_t data = 0xffffffff;
	/* Is the host paying attention ? */
	if (request->bmRequestType_bit.recipient != TUSB_REQ_RCPT_DEVICE)
		return false;
	probe_info("remote_req wValue=0x%02x wIndex=0x%02x wLength=0x%02x dir=%d\n", request->wValue, request->wIndex, request->wLength, request->bmRequestType_bit.direction);
	if (request->wIndex >= NUM_BANK0_GPIOS)
		return false;
	switch (request->bmRequestType_bit.direction)
	{
	case TUSB_DIR_IN:
	{
		if(stage != CONTROL_STAGE_SETUP)
			return true;
		if (request->wLength != 4)
			return false;
		switch (request->wValue) {
		case GPIO_GET_FUNCTION:
			data = gpio_get_function(request->wIndex);
			break;
		case GPIO_GET_PULLS:
			data  = (gpio_is_pulled_up(request->wIndex) ? 0x1 : 0);
			data |= (gpio_is_pulled_down(request->wIndex) ? 0x2 : 0);
			break;
		case GPIO_GET_INPUT_HYST_ENABLED:
			data = gpio_is_input_hysteresis_enabled(request->wIndex);
			break;
		case GPIO_GET_SLEW_RATE:
			data = gpio_get_slew_rate(request->wIndex);
			break;
		case GPIO_GET_DRIVE_STRENGTH:
			data = gpio_get_drive_strength(request->wIndex);
			break;
		case GPIO_GET:
			data = gpio_get(request->wIndex);
			break;
		case GPIO_GET_ALL:
			data = gpio_get_all();
			break;
		case GPIO_GET_OUT_LEVEL:
			data = gpio_get_out_level(request->wIndex);
			break;
		case GPIO_GET_DIR:
			data = gpio_get_dir(request->wIndex);
			break;
		default:
			return false;
			break;
		}
		return tud_control_xfer(rhport, request, &data, sizeof(data));
	}
	case TUSB_DIR_OUT:
	{
		/* Data phase should "always" happen */
		if (stage == CONTROL_STAGE_SETUP) {
			return tud_control_xfer(rhport, request, &data, sizeof(data));
		} else if (stage == CONTROL_STAGE_DATA) {
			probe_info("data stage2 data= %08lx\n", data);
			switch (request->wValue) {
			case GPIO_SET_FUNCTION:
				gpio_set_function(request->wIndex, data);
				break;
			case GPIO_SET_PULLS:
				gpio_set_pulls(request->wIndex, !!(data & 0x1), !!(data & 0x2));
				break;
			case GPIO_SET_INPUT_ENABLED:
				gpio_set_input_enabled(request->wIndex, data);
				break;
			case GPIO_SET_INPUT_HYST_ENABLED:
				gpio_set_input_hysteresis_enabled(request->wIndex, data);
				break;
			case GPIO_SET_SLEW_RATE:
				gpio_set_slew_rate(request->wIndex, data);
				break;
			case GPIO_SET_DRIVE_STRENGTH:
				gpio_set_drive_strength(request->wIndex, data);
				break;
			case GPIO_PUT:
				gpio_put(request->wIndex, !!data);
				break;
			case GPIO_PUT_ALL:
				gpio_put_all(data);
				break;
			case GPIO_SET_MASK:
				gpio_set_mask(data);
				break;
			case GPIO_CLR_MASK:
				gpio_clr_mask(data);
				break;
			case GPIO_XOR_MASK:
				gpio_xor_mask(data);
				break;
			case GPIO_SET_DIR_OUT_MASKED:
				gpio_set_dir_out_masked(data);
				break;
			case GPIO_SET_DIR_IN_MASKED:
				gpio_set_dir_in_masked(data);
				break;
			case GPIO_SET_DIR_ALL_BITS:
				gpio_set_dir_all_bits(data);
				break;
			case GPIO_SET_DIR:
				gpio_set_dir(request->wIndex, !!data);
				break;
			case GPIO_INIT:
				gpio_init(request->wIndex);
				break;
			case GPIO_INIT_MASK:
				gpio_init_mask(data);
				break;
			case GPIO_DEINIT:
				gpio_deinit(request->wIndex);
				break;
			default:
				return false;
			}
			return true;
		} else {
			return true;
		}
	}
	}
	return false;
}
