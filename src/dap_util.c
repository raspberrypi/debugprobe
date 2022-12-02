/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
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

/**
 * This is originated from CMSIS DAP.c because the CMSIS developers weren't capable of
 * designing a protocol with contained length indication.
 * Therefore these functions are doing nothing else then collecting the expected length
 * of the _request_ from the already received data.  Length of generated response is
 * not of interest.
 * 
 * BUT... problem still persists, see e.g. DAP_SWD_Sequence()
 */

#include <string.h>
#include "DAP_config.h"
#include "DAP.h"

#include "dap_util.h"



static uint32_t DAP_Check_JTAG_Sequence(const uint8_t *request, uint32_t request_len)
{
    uint32_t sequence_info;
    uint32_t sequence_count;
    uint32_t request_count;
    uint32_t count;

    if (request_len < 1 + 1)
    {
        return DAP_CHECK_ABORT;
    }

    request_count = 2U;
    sequence_count = request[1];

    request += 2;
    while (sequence_count--)
    {
        if (request_count > request_len)
        {
            return DAP_CHECK_ABORT;
        }

        sequence_info = *request++;
        count = sequence_info & JTAG_SEQUENCE_TCK;
        if (count == 0U)
        {
            count = 64U;
        }
        count = (count + 7U) / 8U;
        request += count;
        request_count += count + 1U;
    }

    return request_count;
}   // DAP_Check_JTAG_Sequence



static uint32_t DAP_Check_SWD_Sequence(const uint8_t *request, uint32_t request_len) 
{
    uint32_t sequence_info;
    uint32_t sequence_count;
    uint32_t request_count;
    uint32_t count;

    if (request_len < 1 + 1)
    {
        return DAP_CHECK_ABORT;
    }

    request_count = 2U;
    sequence_count = request[1];

    request += 2;
    while (sequence_count--)
    {
        if (request_len < request_count)
        {
            return DAP_CHECK_ABORT;
        }

        sequence_info = *request++;
        count = sequence_info & SWD_SEQUENCE_CLK;
        if (count == 0U)
        {
            count = 64U;
        }
        count = (count + 7U) / 8U;
        if ((sequence_info & SWD_SEQUENCE_DIN) != 0U)
        {
            request_count++;
        }
        else
        {
            request += count;
            request_count += count + 1U;
        }
    }

    return request_count;
}   // DAP_Check_SWD_Sequence



static uint32_t DAP_Check_Transfer(const uint8_t *request, uint32_t request_len)
{
    const uint8_t *request_head;
    uint32_t transfer_count;
    uint32_t request_value;

    if (request_len < 1 + 1 + 1 + 1)
    {
        return DAP_CHECK_ABORT;
    }

    request_head = request;

    transfer_count = request[2];
    
    request += 3;
    for (; transfer_count != 0U; transfer_count--)
    {
        if (request_len < request - request_head)
        {
            return DAP_CHECK_ABORT;
        }

        // Process dummy requests
        request_value = *request++;
        if ((request_value & DAP_TRANSFER_RnW) != 0U)
        {
            // Read register
            if ((request_value & DAP_TRANSFER_MATCH_VALUE) != 0U)
            {
                // Read with value match
                request += 4;
            }
        }
        else
        {
            // Write register
            request += 4;
        }
    }

    return request - request_head;
}   // DAP_Check_Transfer



static uint32_t DAP_Check_TransferBlock(const uint8_t *request, uint32_t request_len) 
{
    uint32_t num;

    if (request_len < 1 + 1 + 2 + 1)
    {
        return DAP_CHECK_ABORT;
    }

    if ((request[4] & DAP_TRANSFER_RnW) != 0U)
    {
        // Read register block
        num = 5;
    }
    else
    {
        // Write register block
        num = 5 + 4 * ((uint32_t)request[2]  |  (uint32_t)(request[3] << 8));
    }

    return num;
}   // DAP_Check_TransferBlock



/**
 * Check the length of a vendor DAP request
 * request:     pointer to available request data
 * request_len: request data currently in buffer
 * return:   number of bytes required for complete request or DAP_CHECK_ABORT if  not enough information
 */
__WEAK uint32_t DAP_Check_ProcessVendorCommand(const uint8_t *request, uint32_t request_len) 
{
    // actually this should result in an assertion (thanks to the protocol definition without length spec)
    return 1;
}   // DAP_Check_ProcessVendorCommand



/**
 * Check the length of a DAP request
 * request:     pointer to available request data
 * request_len: request data currently in buffer
 * return:   number of bytes required for complete request or DAP_CHECK_ABORT if  not enough information
 */
static uint32_t DAP_Check_ProcessCommand(const uint8_t *request, uint32_t request_len) 
{
    uint32_t num;

    if (request[0] >= ID_DAP_Vendor0  &&  request[0] <= ID_DAP_Vendor31)
    {
        return DAP_Check_ProcessVendorCommand(request, request_len);
    }

    switch (request[0])
    {
        case ID_DAP_Info:
            num = 1 + 1;
            break;

        case ID_DAP_HostStatus:
            num = 1 + 1 + 1;
            break;

        case ID_DAP_Connect:
            num = 1 + 1;
            break;

        case ID_DAP_Disconnect:
            num = 1;
            break;

        case ID_DAP_Delay:
            num = 1 + 2;
            break;

        case ID_DAP_ResetTarget:
            num = 1;
            break;

        case ID_DAP_SWJ_Pins:
            num = 1 + 1 + 1 + 4;
            break;

        case ID_DAP_SWJ_Clock:
            num = 1 + 4;
            break;

        case ID_DAP_SWJ_Sequence:
            if (request_len < 3)
            {
                num = DAP_CHECK_ABORT;
            }
            else
            {
                num = 1 + 1 + ((request[1] == 0 ? 256 : request[1]) + 7) / 8;
            }
            break;

        case ID_DAP_SWD_Configure:
            num = 1 + 1;
            break;

        case ID_DAP_SWD_Sequence:
            num = DAP_Check_SWD_Sequence(request, request_len);
            break;

        case ID_DAP_JTAG_Sequence:
            num = DAP_Check_JTAG_Sequence(request, request_len);
            break;

        case ID_DAP_JTAG_Configure:
            num = 1 + 1 + 1;
            break;

        case ID_DAP_JTAG_IDCODE:
            num = 1 + 1;
            break;

        case ID_DAP_TransferConfigure:
            num = 1 + 1 + 2 + 2;
            break;

        case ID_DAP_Transfer:
            num = DAP_Check_Transfer(request, request_len);
            break;

        case ID_DAP_TransferBlock:
            num = DAP_Check_TransferBlock(request, request_len);
            break;

        case ID_DAP_TransferAbort:
            num = 1;
            break;

        case ID_DAP_WriteABORT:
            num = 2 + 4;
            break;

#if ((SWO_UART != 0) || (SWO_MANCHESTER != 0))
        case ID_DAP_SWO_Transport:
            num = 1 + 1;
            break;

        case ID_DAP_SWO_Mode:
            num = 1 + 1;
            break;

        case ID_DAP_SWO_Baudrate:
            num = 1 + 4;
            break;

        case ID_DAP_SWO_Control:
            num = 1 + 1;
            break;

        case ID_DAP_SWO_Status:
            num = 1;
            break;

        case ID_DAP_SWO_ExtendedStatus:
            num = 1 + 1;
            break;

        case ID_DAP_SWO_Data:
            num = 1 + 2;
            break;
#endif

        default:
            num = 1;
            break;
    }

    return num;
}   // DAP_Check_ProcessCommand



// Check the length of a DAP request
//   request:     pointer to available request data
//   request_len: request data currently in buffer
//   return:   number of bytes required for complete request or DAP_CHECK_ABORT if not enough information
uint32_t DAP_Check_ExecuteCommand(const uint8_t *request, uint32_t request_len)
{
    uint32_t num_cmd, num, n;

    if (*request == ID_DAP_ExecuteCommands)
    {
        num_cmd = request[1];
        num = 2;
        request += 2;
        for (uint32_t c = 0;  c < num_cmd;  ++c)
        {
            if (num > request_len)
            {
                num = DAP_CHECK_ABORT;
                break;
            }
            request_len -= num;
            n = DAP_Check_ProcessCommand(request, request_len);
            num += n;
            request += n;
        }
        return (num > DAP_CHECK_ABORT) ? DAP_CHECK_ABORT : num;
    }

    return DAP_Check_ProcessCommand(request, request_len);
}   // DAP_Check_ExecuteCommand
