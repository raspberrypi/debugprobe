/**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tusb_edpt_handler.h"
#include "DAP.h"
#include "semphr.h"


static uint8_t itf_num;
static uint8_t _rhport;


static uint8_t _out_ep_addr;
static uint8_t _in_ep_addr;

static buffer_t USBRequestBuffer;
static buffer_t USBResponseBuffer;

static SemaphoreHandle_t edpt_spoon;

#define WR_IDX(x) (x.wptr % DAP_PACKET_COUNT)
#define RD_IDX(x) (x.rptr % DAP_PACKET_COUNT)

#define WR_SLOT_PTR(x) &(x.data[WR_IDX(x)][0])
#define RD_SLOT_PTR(x) &(x.data[RD_IDX(x)][0])

bool buffer_full(buffer_t *buffer)
{
	return (buffer->wptr + 1) % DAP_PACKET_COUNT == buffer->rptr % DAP_PACKET_COUNT;
}

bool buffer_empty(buffer_t *buffer)
{
	return buffer->wptr == buffer->rptr;
}

// Defer setup to .reset() / .open()
void dap_edpt_init(void) {
	edpt_spoon = xSemaphoreCreateMutex();
	xSemaphoreGive(edpt_spoon);
}

// This only gets called if the SOF watchdog times out
bool dap_edpt_deinit(void)
{
	if (edpt_spoon != NULL)
		vSemaphoreDelete(edpt_spoon);
	edpt_spoon = NULL;
	return true;
}

void dap_edpt_reset(uint8_t __unused rhport)
{
	probe_info("dap_edpt_reset\n");
	memset(&USBRequestBuffer, 0, sizeof(USBRequestBuffer));
	memset(&USBResponseBuffer, 0, sizeof(USBResponseBuffer));

	//  Initialise circular buffer indices
	USBResponseBuffer.wptr = 0;
	USBResponseBuffer.rptr = 0;
	USBRequestBuffer.wptr = 0;
	USBRequestBuffer.rptr = 0;

	// Initialse full/empty flags
	USBResponseBuffer.wasFull = false;
	USBResponseBuffer.wasEmpty = true;
	USBRequestBuffer.wasFull = false;
	USBRequestBuffer.wasEmpty = true;
	// Linux resets us twice in succession

	itf_num = 0;
}

char * dap_cmd_string[] = {
	[ID_DAP_Info               ] = "DAP_Info",
	[ID_DAP_HostStatus         ] = "DAP_HostStatus",
	[ID_DAP_Connect            ] = "DAP_Connect",
	[ID_DAP_Disconnect         ] = "DAP_Disconnect",
	[ID_DAP_TransferConfigure  ] = "DAP_TransferConfigure",
	[ID_DAP_Transfer           ] = "DAP_Transfer",
	[ID_DAP_TransferBlock      ] = "DAP_TransferBlock",
	[ID_DAP_TransferAbort      ] = "DAP_TransferAbort",
	[ID_DAP_WriteABORT         ] = "DAP_WriteABORT",
	[ID_DAP_Delay              ] = "DAP_Delay",
	[ID_DAP_ResetTarget        ] = "DAP_ResetTarget",
	[ID_DAP_SWJ_Pins           ] = "DAP_SWJ_Pins",
	[ID_DAP_SWJ_Clock          ] = "DAP_SWJ_Clock",
	[ID_DAP_SWJ_Sequence       ] = "DAP_SWJ_Sequence",
	[ID_DAP_SWD_Configure      ] = "DAP_SWD_Configure",
	[ID_DAP_SWD_Sequence       ] = "DAP_SWD_Sequence",
	[ID_DAP_JTAG_Sequence      ] = "DAP_JTAG_Sequence",
	[ID_DAP_JTAG_Configure     ] = "DAP_JTAG_Configure",
	[ID_DAP_JTAG_IDCODE        ] = "DAP_JTAG_IDCODE",
	[ID_DAP_SWO_Transport      ] = "DAP_SWO_Transport",
	[ID_DAP_SWO_Mode           ] = "DAP_SWO_Mode",
	[ID_DAP_SWO_Baudrate       ] = "DAP_SWO_Baudrate",
	[ID_DAP_SWO_Control        ] = "DAP_SWO_Control",
	[ID_DAP_SWO_Status         ] = "DAP_SWO_Status",
	[ID_DAP_SWO_ExtendedStatus ] = "DAP_SWO_ExtendedStatus",
	[ID_DAP_SWO_Data           ] = "DAP_SWO_Data",
	[ID_DAP_QueueCommands      ] = "DAP_QueueCommands",
	[ID_DAP_ExecuteCommands    ] = "DAP_ExecuteCommands",
};


uint16_t dap_edpt_open(uint8_t __unused rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len)
{
	// This has an *implicit return* if fails. .open() is called for each interface on the device on usb SET_CONFIGURATION(nr)
	TU_VERIFY(TUSB_CLASS_VENDOR_SPECIFIC == itf_desc->bInterfaceClass &&
			DAP_INTERFACE_SUBCLASS == itf_desc->bInterfaceSubClass &&
			DAP_INTERFACE_PROTOCOL == itf_desc->bInterfaceProtocol, 0);

	uint16_t const drv_len = sizeof(tusb_desc_interface_t) + (itf_desc->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
	TU_VERIFY(max_len >= drv_len, 0);
	itf_num = itf_desc->bInterfaceNumber;

	// Initialising the OUT endpoint

	tusb_desc_endpoint_t *edpt_desc = (tusb_desc_endpoint_t *) (itf_desc + 1);
	uint8_t ep_addr = edpt_desc->bEndpointAddress;

	_out_ep_addr = ep_addr;

	// The OUT endpoint requires a call to usbd_edpt_xfer to initialise the endpoint, giving tinyUSB a buffer to consume when a transfer occurs at the endpoint
	usbd_edpt_open(rhport, edpt_desc);
	usbd_edpt_xfer(rhport, ep_addr, WR_SLOT_PTR(USBRequestBuffer), DAP_PACKET_SIZE);

	// Initiliasing the IN endpoint

	edpt_desc++;
	ep_addr = edpt_desc->bEndpointAddress;

	_in_ep_addr = ep_addr;

	// The IN endpoint doesn't need a transfer to initialise it, as this will be done by the main loop of dap_thread
	usbd_edpt_open(rhport, edpt_desc);

	// Spawn DAP thread?

	return drv_len;

}

bool dap_edpt_control_xfer_cb(uint8_t __unused rhport, uint8_t stage, tusb_control_request_t const *request)
{
	return false;
}

// Manage USBResponseBuffer (request) write and USBRequestBuffer (response) read indices
bool dap_edpt_xfer_cb(uint8_t __unused rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
	const uint8_t ep_dir = tu_edpt_dir(ep_addr);

	if(ep_dir == TUSB_DIR_IN)
	{
		if(xferred_bytes >= 0u && xferred_bytes <= DAP_PACKET_SIZE)
		{
			xSemaphoreTake(edpt_spoon, portMAX_DELAY);
			USBResponseBuffer.rptr++;
			// This checks that the buffer was not empty in DAP thread, which means the next buffer was not queued up for the in endpoint callback
			// So, queue up the buffer at the new read index, since we expect read to catch up to write at this point.
			// It is possible for the read index to be multiple spaces behind the write index (if the USB callbacks are lagging behind dap thread),
			// so we account for this by only setting wasEmpty to true if the next callback will empty the buffer
			if(!USBResponseBuffer.wasEmpty)
			{
				usbd_edpt_xfer(rhport, ep_addr, RD_SLOT_PTR(USBResponseBuffer), USBResponseBuffer.data_len[RD_IDX(USBResponseBuffer)]);
				USBResponseBuffer.wasEmpty = (USBResponseBuffer.rptr + 1) == USBResponseBuffer.wptr;
			}
			xSemaphoreGive(edpt_spoon);
			//  Wake up DAP thread after processing the callback
			xTaskNotify(dap_taskhandle, 0, eSetValueWithOverwrite);
			return true;
		}
		return false;

	} else if(ep_dir == TUSB_DIR_OUT) {

		if(xferred_bytes >= 0u && xferred_bytes <= DAP_PACKET_SIZE)
		{
			xSemaphoreTake(edpt_spoon, portMAX_DELAY);
			// Only queue the next buffer in the out callback if the buffer is not full
			// If full, we set the wasFull flag, which will be checked by dap thread
			if(!buffer_full(&USBRequestBuffer))
			{
				USBRequestBuffer.wptr++;
				usbd_edpt_xfer(rhport, ep_addr, WR_SLOT_PTR(USBRequestBuffer), DAP_PACKET_SIZE);
				USBRequestBuffer.wasFull = false;
			}
			else {
				USBRequestBuffer.wasFull = true;
			}
			xSemaphoreGive(edpt_spoon);
			//  Wake up DAP thread after processing the callback
			xTaskNotify(dap_taskhandle, 0, eSetValueWithOverwrite);
			return true;
		}
		return false;
	}
	return false;
}

void dap_thread(void *ptr)
{
	uint32_t n;
	uint32_t cmd;
	uint16_t resp_len;
	do
	{
		// Wait for usb CB wake
		xTaskNotifyWait(0, 0xFFFFFFFFu, &cmd, 1);

		while(USBRequestBuffer.rptr != USBRequestBuffer.wptr)
		{
			/*
			 * Atomic command support - buffer QueueCommands, but don't process them
			 * until a non-QueueCommands packet is seen.
			 */
			n = USBRequestBuffer.rptr;
			while (USBRequestBuffer.data[n % DAP_PACKET_COUNT][0] == ID_DAP_QueueCommands) {
				probe_info("%lu %lu DAP queued cmd %s len %02x\n",
					       USBRequestBuffer.wptr, USBRequestBuffer.rptr,
					       dap_cmd_string[USBRequestBuffer.data[n % DAP_PACKET_COUNT][0]], USBRequestBuffer.data[n % DAP_PACKET_COUNT][1]);
				USBRequestBuffer.data[n % DAP_PACKET_COUNT][0] = ID_DAP_ExecuteCommands;
				n++;
				while (n == USBRequestBuffer.wptr) {
					/* Need yield in a loop here, as IN callbacks will also wake the thread */
					probe_info("DAP wait\n");
					vTaskSuspend(dap_taskhandle);
				}
			}
			// Read a single packet from the USB buffer into the DAP Request buffer
			probe_info("%lu %lu DAP cmd %s len %02x\n",
					   USBRequestBuffer.wptr, USBRequestBuffer.rptr,
					   dap_cmd_string[*RD_SLOT_PTR(USBRequestBuffer)], *(RD_SLOT_PTR(USBRequestBuffer)+1));

			// If the buffer was full in the out callback, we need to queue up another buffer for the endpoint to consume, now that we know there is space in the buffer.
			xSemaphoreTake(edpt_spoon, portMAX_DELAY); // Suspend the scheduler to safely update the write index
			if(USBRequestBuffer.wasFull)
			{
				USBRequestBuffer.wptr++;
				usbd_edpt_xfer(_rhport, _out_ep_addr, WR_SLOT_PTR(USBRequestBuffer), DAP_PACKET_SIZE);
				USBRequestBuffer.wasFull = false;
			}
			xSemaphoreGive(edpt_spoon);

			resp_len = DAP_ExecuteCommand(RD_SLOT_PTR(USBRequestBuffer), WR_SLOT_PTR(USBResponseBuffer)) & 0xffff;
			USBRequestBuffer.rptr++;
			probe_info("%lu %lu DAP resp %s len %u\n",
					   USBResponseBuffer.wptr, USBResponseBuffer.rptr,
					   dap_cmd_string[*WR_SLOT_PTR(USBResponseBuffer)], resp_len);

			USBResponseBuffer.data_len[WR_IDX(USBResponseBuffer)] = resp_len;
			//  Suspend the scheduler to avoid stale values/race conditions between threads
			xSemaphoreTake(edpt_spoon, portMAX_DELAY);

			if(buffer_empty(&USBResponseBuffer))
			{
				USBResponseBuffer.wptr++;

				usbd_edpt_xfer(_rhport, _in_ep_addr, RD_SLOT_PTR(USBResponseBuffer), USBResponseBuffer.data_len[RD_IDX(USBResponseBuffer)]);
			} else {

				USBResponseBuffer.wptr++;

				// The In callback needs to check this flag to know when to queue up the next buffer.
				USBResponseBuffer.wasEmpty = false;
			}
			xSemaphoreGive(edpt_spoon);
		}
	} while (1);
}

usbd_class_driver_t const _dap_edpt_driver =
{
		.init = dap_edpt_init,
		.deinit = dap_edpt_deinit,
		.reset = dap_edpt_reset,
		.open = dap_edpt_open,
		.control_xfer_cb = dap_edpt_control_xfer_cb,
		.xfer_cb = dap_edpt_xfer_cb,
		.sof = NULL,
#if CFG_TUSB_DEBUG >= 2
		.name = "DAP ENDPOINT"
#endif
};

// Add the custom driver to the tinyUSB stack
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
	*driver_count = 1;
	return &_dap_edpt_driver;
}

