#ifndef _REMOTE_GPIO_H_
#define _REMOTE_GPIO_H_

#ifdef PICO_SDK_VERSION_MAJOR
#include "tusb.h"

bool gpio_remote_req(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request);

#endif

/* Control interface definitions for exposing pico-sdk's GPIO API.
 * bmRequestType.direction - SET calls are Host to Device (0), GET calls are Device to Host (1)
 * bmRequestType.type = Vendor (2)
 * bmRequestType.recipient = Device (0)
 * (i.e. bmRequestType = 0x40 or 0xc0.)
 * bRequest = CTRL_REMOTE_GPIO_REQ (0x02)
 * wValue - GPIO API function indexed by enum here. Note that there are fewer get_ calls defined than set_ calls.
 * wIndex - for functions operating on a single GPIO, the GPIO number. For functions operating on all GPIOs in bulk (e.g gpio_get_all or gpio_dir_out_masked), set to zero.
 * wLength = 4. For SET calls, provide a le32 word of either the 2nd argument to a function call for a specific GPIO, or a mask of GPIOs to operate on in bulk.
 *              Note: for GPIO_INIT and GPIO_DEINIT, the data stage still happens but is ignored.
 *              For GET calls, returns a le32 of either the function call result, or the results of a bulk operation as a bitmask.
 **/

#define CTRL_REMOTE_GPIO_REQ 0x2
#define BMREQUEST_GPIO_SET 0x40
#define BMREQUEST_GPIO_GET 0xc0

typedef enum {
	GPIO_GET_FUNCTION,
	GPIO_GET_PULLS,
	GPIO_GET_INPUT_ENABLED,
	GPIO_GET_INPUT_HYST_ENABLED,
	GPIO_GET_SLEW_RATE,
	GPIO_GET_DRIVE_STRENGTH,
	GPIO_GET,
	GPIO_GET_ALL,
	GPIO_GET_OUT_LEVEL,
	GPIO_GET_DIR,
	GPIO_GET_MAX,
} gpio_get_fns;

typedef enum {
	GPIO_SET_FUNCTION,
	GPIO_SET_PULLS,
	GPIO_SET_INPUT_ENABLED,
	GPIO_SET_INPUT_HYST_ENABLED,
	GPIO_SET_SLEW_RATE,
	GPIO_SET_DRIVE_STRENGTH,
	GPIO_PUT, // No idea why this isn't called gpio_set in the sdk
	GPIO_PUT_ALL,
	GPIO_SET_MASK,
	GPIO_CLR_MASK,
	GPIO_XOR_MASK,
	GPIO_SET_DIR_OUT_MASKED,
	GPIO_SET_DIR_IN_MASKED,
	GPIO_SET_DIR_ALL_BITS,
	GPIO_SET_DIR,
	GPIO_INIT,
	GPIO_INIT_MASK,
	GPIO_DEINIT,
	GPIO_SET_MAX,
} gpio_set_fns;

#endif /* _REMOTE_GPIO_H_ */
