/*
 * Copyright (c) 2016, Intel Corporation
 * All rights reserved.
 *
 * Author: Jérémy Compostella <jeremy.compostella@intel.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <kconfig.h>
#include <libpayload-config.h>
#include <libpayload.h>
#include <ewarg.h>
#include <ewlog.h>
#include <hwconfig.h>
#include "s8250mem32.h"

#define SBL_SERIAL_BASEADDR "serail_baseaddr"
#define SBL_SERIAL_TYPE "serail_type"
#define SBL_SERIAL_REGWIDTH "serail_regwidth"

#ifndef SERIAL_BASEADDR
#include <pci/pci.h>
#define SERIAL_BASEADDR       GetPciUartBase(SERIAL_PCI_DID)
#define INTEL_VID             0x8086
#define MOS_VID               0x9710
static uint32_t GetPciUartBase(uint32_t pci_did)
{
	uint32_t addr;
	pcidev_t pci_dev;
	pci_find_device(INTEL_VID, pci_did, &pci_dev);
	addr = pci_read_config32(pci_dev, PCI_BASE_ADDRESS_0);
	addr = addr & ~0xf;
	return addr;
}
#endif

static EFI_STATUS s8250mem32_init(__attribute__((__unused__)) EFI_SYSTEM_TABLE *st)
{
	static struct cb_serial s;
	const char *val;

	val = ewarg_getval(SBL_SERIAL_BASEADDR);
	if (val) {
		s.baseaddr = (UINT32)strtoull(val, NULL, 16);
	} else {
		s.baseaddr = SERIAL_BASEADDR;
	}

	val = ewarg_getval(SBL_SERIAL_TYPE);
	if (val) {
		s.type = (UINT32)strtoull(val, NULL, 16);
	} else {
		s.type = HW_SERIAL_TYPE;
	}

	val = ewarg_getval(SBL_SERIAL_REGWIDTH);
	if (val) {
		s.regwidth = (UINT32)strtoull(val, NULL, 16);
	} else {
		s.regwidth = HW_SERIAL_REG_WIDTH;
	}

	lib_sysinfo.serial = &s;

	serial_console_init();

	return EFI_SUCCESS;
}

ewdrv_t s8250mem32_drv = {
	.name = "s8250mem32",
	.description = "Initialize the libpayload 8250 serial driver for iomem 32bits",
	.init = s8250mem32_init
};
