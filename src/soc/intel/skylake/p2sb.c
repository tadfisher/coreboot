/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2018 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <console/console.h>
#include <intelblocks/p2sb.h>

void p2sb_soc_get_sb_mask(uint32_t *ep_mask, size_t count)
{
	uint32_t mask;

	if (count != P2SB_EP_MASK_MAX_REG) {
		printk(BIOS_ERR, "Unable to program EPMASK registers\n");
		return;
	}

	/*
	 * Set p2sb PCI offset EPMASK5 [29, 28, 27, 26] to disable Sideband
	 * access for PCI Root Bridge.
	 * Set p2sb PCI offset EPMASK5 [17, 16,10, 1] to disable Sideband
	 * access for MIPI controller.
	 */
	mask = (1 << 29) | (1 << 28) | (1 << 27) | (1 << 26) | (1 << 17) |
			 (1 << 16) | (1 << 10) | (1 << 1);

	ep_mask[P2SB_EP_MASK_5_REG] = mask;

	/*
	 * Set p2sb PCI offset EPMASK7 [6, 5] to disable Sideband
	 * access for XHCI controller.
	 */
	mask = (1 << 6) | (1 << 5);

	ep_mask[P2SB_EP_MASK_7_REG] = mask;
}
