/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Federico Zuccardi Merli
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

#include <get_config.h>
#include <stdint.h>
#include "pico.h"
#include "pico/unique_id.h"

#include "minIni/minIni.h"


#define _STR(S)  #S
#define STR(S)   _STR(S)


/**
 * Serial number in UCS-2 for global access: TinyUSB, DAP
 */
char usb_serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

#if OPT_NET
    /**
     * Network MAC address for global access: lwIP, TinyUSB
     *
     * \note
     *    TinyUSB 0.15.0 which has been released with the Pico SDK 1.5.1 declares \a tud_network_mac_address as
     *    "const".  We define it here as non-const.  The linker silently ignores the "const" in the declaration
     *    so that the below definition counts.
     */
    uint8_t tud_network_mac_address[6];
#endif


void get_config_init(void)
/**
 * Fills \a usb_serial with the flash unique id, \a tud_network_mac_address similar.
 */
{
    pico_unique_board_id_t uID;

    pico_get_unique_board_id(&uID);

#if OPT_NET
    tud_network_mac_address[0] = 0xfe;     // 0xfe is allowed for local use, never use odd numbers here (group/multicast)
    for (int i = 1;  i < sizeof(tud_network_mac_address);  ++i)
    {
        tud_network_mac_address[i] = uID.id[i + (PICO_UNIQUE_BOARD_ID_SIZE_BYTES - sizeof(tud_network_mac_address))];
    }
#endif

    ini_gets(MININI_SECTION, "nick", "", usb_serial, sizeof(usb_serial), MININI_FILENAME);
    if (usb_serial[0] == '\0')
    {
        for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2; i++)
        {
            /* Byte index inside the uid array */
            int bi = i / 2;
            /* Use high nibble first to keep memory order (just cosmetics) */
            uint8_t nibble = (uID.id[bi] >> 4) & 0x0F;
            uID.id[bi] <<= 4;
            /* Binary to hex digit */
            usb_serial[i] = nibble < 10 ? nibble + '0' : nibble + 'A' - 10;
        }
    }
}   // get_config_init
