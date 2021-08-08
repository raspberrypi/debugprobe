/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Jaroslav Kysela <perex@perex.cz>
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

#ifndef CDC_SUMP_H
#define CDC_SUMP_H

/*
 *  Short SUMP commands
 */
#define SUMP_CMD_RESET			0x00
#define SUMP_CMD_ARM			0x01
#define SUMP_CMD_ID			0x02
#define SUMP_CMD_META			0x04
#define SUMP_CMD_FINISH			0x05	/* break RLE encodings */
#define SUMP_CMD_QUERY_INPUT		0x06	/* return input bits now */
#define SUMP_CMD_QUERY_STATE		0x07
#define SUMP_CMD_RETURN_DATA		0x08
#define SUMP_CMD_ADVANCED_ARM		0x0f
#define SUMP_CMD_XON			0x11
#define SUMP_CMD_XOFF			0x13

#define SUMP_CMD_IS_SHORT(cmd0) (((cmd0) & 0x80) == 0)

/*
 *  Long SUMP commands
 */
#define SUMP_CMD_SET_SAMPLE_RATE	0x80
#define SUMP_CMD_SET_COUNTS		0x81
#define SUMP_CMD_SET_FLAGS		0x82
#define SUMP_CMD_SET_ADV_TRG_SELECT	0x9e	/* advanced trigger select */
#define SUMP_CMD_SET_ADV_TRG_DATA	0x9f	/* advanced trigger data */
#define SUMP_CMD_SET_BTRG0_MASK		0xc0	/* basic trigger */
#define SUMP_CMD_SET_BTRG0_VALUE	0xc1	/* basic trigger */
#define SUMP_CMD_SET_BTRG0_CONFIG	0xc2	/* basic trigger */
#define SUMP_CMD_SET_BTRG1_MASK		0xc4	/* basic trigger */
#define SUMP_CMD_SET_BTRG1_VALUE	0xc5	/* basic trigger */
#define SUMP_CMD_SET_BTRG1_CONFIG	0xc6	/* basic trigger */
#define SUMP_CMD_SET_BTRG2_MASK		0xc8	/* basic trigger */
#define SUMP_CMD_SET_BTRG2_VALUE	0xc9	/* basic trigger */
#define SUMP_CMD_SET_BTRG2_CONFIG	0xca	/* basic trigger */
#define SUMP_CMD_SET_BTRG3_MASK		0xcc	/* basic trigger */
#define SUMP_CMD_SET_BTRG3_VALUE	0xcd	/* basic trigger */
#define SUMP_CMD_SET_BTRG3_CONFIG	0xce	/* basic trigger */

#define SUMP_CMD_IS_LONG(cmd0) (((cmd0) & 0x80) != 0)

/*
 *  META tags
 */
#define SUMP_META_END			0x00
#define SUMP_META_NAME			0x01
#define SUMP_META_FPGA_VERSION		0x02
#define SUMP_META_CPU_VERSION		0x03
#define SUMP_META_PROBES_DW		0x20
#define SUMP_META_SAMPLE_RAM		0x21
#define SUMP_META_DYNAMIC_RAM		0x22
#define SUMP_META_SAMPLE_RATE		0x23
#define SUMP_META_PROTOCOL		0x24
#define SUMP_META_CAPABILITIES		0x25
#define SUMP_META_PROBES_B		0x40
#define SUMP_META_PROTOCOL_B		0x41

/*
 *  Flag defines
 */
#define SUMP_FLAG1_DDR			0x0001	/* demux mode */
#define SUMP_FLAG1_NOISE_FILTER		0x0002
#define SUMP_FLAG1_GR0_DISABLE		0x0004
#define SUMP_FLAG1_GR1_DISABLE		0x0008
#define SUMP_FLAG1_GR2_DISABLE		0x0010
#define SUMP_FLAG1_GR3_DISABLE		0x0020
#define SUMP_FLAG1_GR_MASK		0x003C
#define SUMP_FLAG1_GR_SHIFT		2
#define SUMP_FLAG1_GR_DISABLE(x)	(1 << (x+SUMP_FLAG1_GR_SHIFT))
#define SUMP_FLAG1_EXT_CLOCK		0x0040
#define SUMP_FLAG1_INV_EXT_CLOCK	0x0080	/* capture on falling edge */
#define SUMP_FLAG1_ENABLE_RLE		0x0100
#define SUMP_FLAG1_SWAP16		0x0200	/* swap upper/lower 16 bits */
#define SUMP_FLAG1_EXT_TEST		0x0400	/* output pattern on bits 31:16 */
#define SUMP_FLAG1_INT_TEST		0x0800	/* internal test pattern */
#define SUMP_FLAG1_RLE_MODE_MASK	0xc000
#define SUMP_FLAG1_RLE_MODE0		0x0000	/* issue <value> & <rle-count> as pairs */
#define SUMP_FLAG1_RLE_MODE1		0x4000	/* issue <value> & <rle-count> as pairs */	
#define SUMP_FLAG1_RLE_MODE2		0x8000	/* <values> reissued approximately every 256 <rle-count> fields */
#define SUMP_FLAG1_RLE_MODE3		0xc000	/* <values> can be followed by unlimited numbers of <rle-counts> */

void cdc_sump_init(void);
void cdc_sump_task(void);
void cdc_sump_line_coding(cdc_line_coding_t const* line_coding);

#endif /* SUMP_H_ */

