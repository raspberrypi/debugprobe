/**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tusb_edpt_handler.h"
#include "DAP.h"

static uint8_t itf_num;
static uint8_t _rhport;

volatile uint32_t _resp_len;

uint8_t _out_ep_addr;
uint8_t _in_ep_addr;

buffer_t USBRequestBuffer; 
buffer_t USBResponseBuffer;

void dap_edpt_init(void) {

}
    
void dap_edpt_reset(uint8_t __unused rhport)
{
    itf_num = 0;
}

uint16_t dap_edpt_open(uint8_t __unused rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len)
{   
          
    TU_VERIFY(TUSB_CLASS_VENDOR_SPECIFIC == itf_desc->bInterfaceClass &&
              PICOPROBE_INTERFACE_SUBCLASS == itf_desc->bInterfaceSubClass &&
              PICOPROBE_INTERFACE_PROTOCOL == itf_desc->bInterfaceProtocol, 0);

    //  Initialise circular buffer indices
    USBResponseBuffer.packet_wr_idx = 0;
    USBResponseBuffer.packet_rd_idx = 0;
    USBRequestBuffer.packet_wr_idx = 0;
    USBRequestBuffer.packet_rd_idx = 0;

    // Initialse full/empty flags
    USBResponseBuffer.wasFull = false;
    USBResponseBuffer.wasEmpty = true;
    USBRequestBuffer.wasFull = false;
    USBRequestBuffer.wasEmpty = true;

    uint16_t const drv_len = sizeof(tusb_desc_interface_t) + (itf_desc->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
    TU_VERIFY(max_len >= drv_len, 0);
    itf_num = itf_desc->bInterfaceNumber;

    // Initialising the OUT endpoint

    tusb_desc_endpoint_t *edpt_desc = (tusb_desc_endpoint_t *) (itf_desc + 1);
    uint8_t ep_addr = edpt_desc->bEndpointAddress;

    _out_ep_addr = ep_addr;

    // The OUT endpoint requires a call to usbd_edpt_xfer to initialise the endpoint, giving tinyUSB a buffer to consume when a transfer occurs at the endpoint
    usbd_edpt_open(rhport, edpt_desc);
    usbd_edpt_xfer(rhport, ep_addr, &(USBRequestBuffer.data[USBRequestBuffer.packet_wr_idx][0]), DAP_PACKET_SIZE);

    // Initiliasing the IN endpoint

    edpt_desc++;
    ep_addr = edpt_desc->bEndpointAddress;

    _in_ep_addr = ep_addr;

    // The IN endpoint doesn't need a transfer to initialise it, as this will be done by the main loop of dap_thread
    usbd_edpt_open(rhport, edpt_desc);

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
            USBResponseBuffer.packet_rd_idx = (USBResponseBuffer.packet_rd_idx + 1) % DAP_PACKET_COUNT;

            // This checks that the buffer was not empty in DAP thread, which means the next buffer was not queued up for the in endpoint callback
            // So, queue up the buffer at the new read index, since we expect read to catch up to write at this point.
            // It is possible for the read index to be multiple spaces behind the write index (if the USB callbacks are lagging behind dap thread), 
            // so we account for this by only setting wasEmpty to true if the next callback will empty the buffer
            if(!USBResponseBuffer.wasEmpty)
            {   
                usbd_edpt_xfer(rhport, ep_addr, &(USBResponseBuffer.data[USBResponseBuffer.packet_rd_idx][0]), (uint16_t) _resp_len);
                USBResponseBuffer.wasEmpty = ((USBResponseBuffer.packet_rd_idx + 1) % DAP_PACKET_COUNT == USBResponseBuffer.packet_wr_idx);
            }        

            //  Wake up DAP thread after processing the callback
            vTaskResume(dap_taskhandle);
            return true;
        }
        
        return false;

    } else if(ep_dir == TUSB_DIR_OUT)    {

        if(xferred_bytes >= 0u && xferred_bytes <= DAP_PACKET_SIZE)
        {   
            // Only queue the next buffer in the out callback if the buffer is not full
            // If full, we set the wasFull flag, which will be checked by dap thread                       
            if(!buffer_full(&USBRequestBuffer))
            {   
                USBRequestBuffer.packet_wr_idx = (USBRequestBuffer.packet_wr_idx + 1) % DAP_PACKET_COUNT; 
                usbd_edpt_xfer(rhport, ep_addr, &(USBRequestBuffer.data[USBRequestBuffer.packet_wr_idx][0]), DAP_PACKET_SIZE);
                USBRequestBuffer.wasFull = false; 
            }
            else {
                USBRequestBuffer.wasFull = true;
            }

            //  Wake up DAP thread after processing the callback
            vTaskResume(dap_taskhandle);
            return true;
        }

        return false;
    }
    else return false;
}

void dap_thread(void *ptr)
{
    uint8_t DAPRequestBuffer[DAP_PACKET_SIZE];
    uint8_t DAPResponseBuffer[DAP_PACKET_SIZE];

    do
    {   
        while(USBRequestBuffer.packet_rd_idx != USBRequestBuffer.packet_wr_idx)
        {   
            // Read a single packet from the USB buffer into the DAP Request buffer
            memcpy(DAPRequestBuffer, &(USBRequestBuffer.data[USBRequestBuffer.packet_rd_idx]), DAP_PACKET_SIZE);
            USBRequestBuffer.packet_rd_idx = (USBRequestBuffer.packet_rd_idx + 1) % DAP_PACKET_COUNT; 

            // If the buffer was full in the out callback, we need to queue up another buffer for the endpoint to consume, now that we know there is space in the buffer.
            if(USBRequestBuffer.wasFull)
            {   
                vTaskSuspendAll(); // Suspend the scheduler to safely update the write index
                USBRequestBuffer.packet_wr_idx = (USBRequestBuffer.packet_wr_idx + 1) % DAP_PACKET_COUNT;
                usbd_edpt_xfer(_rhport, _out_ep_addr, &(USBRequestBuffer.data[USBRequestBuffer.packet_wr_idx][0]), DAP_PACKET_SIZE);
                USBRequestBuffer.wasFull = false;
                xTaskResumeAll();
            }

            _resp_len = DAP_ProcessCommand(DAPRequestBuffer, DAPResponseBuffer);


            //  Suspend the scheduler to avoid stale values/race conditions between threads
            vTaskSuspendAll();

            if(buffer_empty(&USBResponseBuffer))
            {
                memcpy(&(USBResponseBuffer.data[USBResponseBuffer.packet_wr_idx]), DAPResponseBuffer, (uint16_t) _resp_len); 
                USBResponseBuffer.packet_wr_idx = (USBResponseBuffer.packet_wr_idx + 1) % DAP_PACKET_COUNT; 

                usbd_edpt_xfer(_rhport, _in_ep_addr, &(USBResponseBuffer.data[USBResponseBuffer.packet_rd_idx][0]), (uint16_t) _resp_len);
            } else {

                memcpy(&(USBResponseBuffer.data[USBResponseBuffer.packet_wr_idx]), DAPResponseBuffer, (uint16_t) _resp_len); 
                USBResponseBuffer.packet_wr_idx = (USBResponseBuffer.packet_wr_idx + 1) % DAP_PACKET_COUNT; 
                
                // The In callback needs to check this flag to know when to queue up the next buffer.
                USBResponseBuffer.wasEmpty = false;
            }
            xTaskResumeAll();
        }
        
        // Suspend DAP thread until it is awoken by a USB thread callback
        vTaskSuspend(dap_taskhandle);

    } while (1);
    
}

usbd_class_driver_t const _dap_edpt_driver =
{
    .init = dap_edpt_init,
    .reset = dap_edpt_reset,
    .open = dap_edpt_open,
    .control_xfer_cb = dap_edpt_control_xfer_cb,
    .xfer_cb = dap_edpt_xfer_cb,
    .sof = NULL,
    #if CFG_TUSB_DEBUG >= 2
        .name = "PICOPROBE ENDPOINT"
    #endif
};

// Add the custom driver to the tinyUSB stack
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = 1;
    return &_dap_edpt_driver;
}

bool buffer_full(buffer_t *buffer)
{
    return ((buffer->packet_wr_idx + 1) % DAP_PACKET_COUNT == buffer->packet_rd_idx);
}

bool buffer_empty(buffer_t *buffer)
{
    return (buffer->packet_wr_idx == buffer->packet_rd_idx);
}
