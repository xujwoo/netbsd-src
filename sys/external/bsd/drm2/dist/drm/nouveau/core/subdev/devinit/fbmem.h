/*
 * Copyright (C) 2010 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <core/device.h>

#define NV04_PFB_BOOT_0						0x00100000
#	define NV04_PFB_BOOT_0_RAM_AMOUNT			0x00000003
#	define NV04_PFB_BOOT_0_RAM_AMOUNT_32MB			0x00000000
#	define NV04_PFB_BOOT_0_RAM_AMOUNT_4MB			0x00000001
#	define NV04_PFB_BOOT_0_RAM_AMOUNT_8MB			0x00000002
#	define NV04_PFB_BOOT_0_RAM_AMOUNT_16MB			0x00000003
#	define NV04_PFB_BOOT_0_RAM_WIDTH_128			0x00000004
#	define NV04_PFB_BOOT_0_RAM_TYPE				0x00000028
#	define NV04_PFB_BOOT_0_RAM_TYPE_SGRAM_8MBIT		0x00000000
#	define NV04_PFB_BOOT_0_RAM_TYPE_SGRAM_16MBIT		0x00000008
#	define NV04_PFB_BOOT_0_RAM_TYPE_SGRAM_16MBIT_4BANK	0x00000010
#	define NV04_PFB_BOOT_0_RAM_TYPE_SDRAM_16MBIT		0x00000018
#	define NV04_PFB_BOOT_0_RAM_TYPE_SDRAM_64MBIT		0x00000020
#	define NV04_PFB_BOOT_0_RAM_TYPE_SDRAM_64MBITX16		0x00000028
#	define NV04_PFB_BOOT_0_UMA_ENABLE			0x00000100
#	define NV04_PFB_BOOT_0_UMA_SIZE				0x0000f000
#define NV04_PFB_DEBUG_0					0x00100080
#	define NV04_PFB_DEBUG_0_PAGE_MODE			0x00000001
#	define NV04_PFB_DEBUG_0_REFRESH_OFF			0x00000010
#	define NV04_PFB_DEBUG_0_REFRESH_COUNTX64		0x00003f00
#	define NV04_PFB_DEBUG_0_REFRESH_SLOW_CLK		0x00004000
#	define NV04_PFB_DEBUG_0_SAFE_MODE			0x00008000
#	define NV04_PFB_DEBUG_0_ALOM_ENABLE			0x00010000
#	define NV04_PFB_DEBUG_0_CASOE				0x00100000
#	define NV04_PFB_DEBUG_0_CKE_INVERT			0x10000000
#	define NV04_PFB_DEBUG_0_REFINC				0x20000000
#	define NV04_PFB_DEBUG_0_SAVE_POWER_OFF			0x40000000
#define NV04_PFB_CFG0						0x00100200
#	define NV04_PFB_CFG0_SCRAMBLE				0x20000000
#define NV04_PFB_CFG1						0x00100204
#define NV04_PFB_SCRAMBLE(i)                         (0x00100400 + 4 * (i))

#define NV10_PFB_REFCTRL					0x00100210
#	define NV10_PFB_REFCTRL_VALID_1				(1 << 31)

static inline struct io_mapping *
fbmem_init(struct nouveau_device *dev)
{
#ifdef __NetBSD__
	return bus_space_io_mapping_create_wc(nv_device_resource_tag(dev, 1),
	    nv_device_resource_start(dev, 1),
	    nv_device_resource_len(dev, 1));
#else
	return io_mapping_create_wc(nv_device_resource_start(dev, 1),
				    nv_device_resource_len(dev, 1));
#endif
}

static inline void
fbmem_fini(struct io_mapping *fb)
{
	io_mapping_free(fb);
}

#ifdef __NetBSD__
/*
 * XXX Consider using bus_space_reserve/map instead.  Don't want to use
 * bus_space_map because presumably that will eat too much KVA.
 */

#  define	__iomem		volatile
#  define	ioread32	fake_ioread32
#  define	iowrite32	fake_iowrite32

static inline uint32_t
fake_ioread32(const void __iomem *p)
{
	const uint32_t v = *(const uint32_t __iomem *)p;

	membar_consumer();

	return v;
}

static inline void
fake_iowrite32(uint32_t v, void __iomem *p)
{

	membar_producer();
	*(uint32_t __iomem *)p = v;
}
#endif

static inline u32
fbmem_peek(struct io_mapping *fb, u32 off)
{
	u8 __iomem *p = io_mapping_map_atomic_wc(fb, off & PAGE_MASK);
	u32 val = ioread32(p + (off & ~PAGE_MASK));
#ifdef __NetBSD__
	io_mapping_unmap_atomic(fb, __UNVOLATILE(p));
#else
	io_mapping_unmap_atomic(p);
#endif
	return val;
}

static inline void
fbmem_poke(struct io_mapping *fb, u32 off, u32 val)
{
	u8 __iomem *p = io_mapping_map_atomic_wc(fb, off & PAGE_MASK);
	iowrite32(val, p + (off & ~PAGE_MASK));
#ifdef __NetBSD__
	membar_producer();
	io_mapping_unmap_atomic(fb, __UNVOLATILE(p));
#else
	wmb();
	io_mapping_unmap_atomic(p);
#endif
}

static inline bool
fbmem_readback(struct io_mapping *fb, u32 off, u32 val)
{
	fbmem_poke(fb, off, val);
	return val == fbmem_peek(fb, off);
}

#ifdef __NetBSD__
#  undef	__iomem
#  undef	ioread32
#  undef	iowrite32
#endif
