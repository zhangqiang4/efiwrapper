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
#include <stdbool.h>
#include <ewlog.h>

#include "lpmemmap/lpmemmap.h"

static UINTN efimemmap_nb;
static EFI_MEMORY_DESCRIPTOR *efimemmap;

#define E820_RAM          1
#define E820_RESERVED     2
#define E820_ACPI         3
#define E820_NVS          4
#define E820_UNUSABLE     5

static EFI_STATUS e820_to_efi(unsigned int e820, UINT32 *efi)
{
	switch (e820) {
	case E820_RAM:
		*efi = EfiConventionalMemory;
		return EFI_SUCCESS;

	case E820_RESERVED:
		*efi = EfiReservedMemoryType;
		return EFI_SUCCESS;

	case E820_ACPI:
		*efi = EfiACPIReclaimMemory;
		return EFI_SUCCESS;

	case E820_NVS:
		*efi = EfiACPIMemoryNVS;
		return EFI_SUCCESS;

	case E820_UNUSABLE:
		*efi = EfiUnusableMemory;
		return EFI_SUCCESS;

	default:
		return EFI_NOT_FOUND;
	}
}

static int cmp_mem_descr(const void *a, const void *b)
{
	const EFI_MEMORY_DESCRIPTOR *m1 = a, *m2 = b;

	if (m1->PhysicalStart < m2->PhysicalStart)
		return -1;
	if (m1->PhysicalStart > m2->PhysicalStart)
		return 1;
	return 0;
}

static void free_efimemmap(void)
{
	free(efimemmap);
	efimemmap = NULL;
	efimemmap_nb = 0;
}

static EFI_STATUS lpmemmap_to_efimemmap(struct memrange *ranges, size_t nb)
{
	EFI_STATUS ret;
	size_t i;
	bool sorted = true;
	EFI_PHYSICAL_ADDRESS start;
	UINT64 size;

	efimemmap = malloc(nb * sizeof(*efimemmap));
	if (!efimemmap)
		return EFI_OUT_OF_RESOURCES;

	for (i = 0; i < nb; i++) {
		if (ranges[i].base % EFI_PAGE_SIZE ||
		    ranges[i].size % EFI_PAGE_SIZE) {
			ewerr("Memory ranges are not %d bytes aligned",
			      EFI_PAGE_SIZE);
			ret = EFI_INVALID_PARAMETER;
			goto err;
		}

		efimemmap[i].NumberOfPages = ranges[i].size / EFI_PAGE_SIZE;
		efimemmap[i].PhysicalStart = ranges[i].base;
		ret = e820_to_efi(ranges[i].type, &efimemmap[i].Type);
		if (EFI_ERROR(ret))
			goto err;

		if (i > 0 && cmp_mem_descr(&efimemmap[i - 1], &efimemmap[i]))
			sorted = false;
	}

	if (!sorted)
		qsort(efimemmap, nb, sizeof(*efimemmap), cmp_mem_descr);

	/* Sanity check: verify that ranges do not overlap */
	for (i = 0; i < nb - 1; i++) {
		start = efimemmap[i].PhysicalStart;
		size = efimemmap[i].NumberOfPages * EFI_PAGE_SIZE;
		if (start + size > efimemmap[i + 1].PhysicalStart) {
			ewerr("Memory ranges are overlapping");
			ret = EFI_INVALID_PARAMETER;
			goto err;
		}
	}

	efimemmap_nb = nb;
	return EFI_SUCCESS;

err:
	free_efimemmap();
	return ret;
}

static void set_mem_descr(EFI_MEMORY_DESCRIPTOR *descr,
			  EFI_PHYSICAL_ADDRESS start, EFI_PHYSICAL_ADDRESS end,
			  EFI_MEMORY_TYPE type)
{
	descr->PhysicalStart = start;
	descr->NumberOfPages = (end - start) / EFI_PAGE_SIZE;
	descr->Type = type;
}

static EFI_STATUS insert_mem_descr_at(EFI_PHYSICAL_ADDRESS start, EFI_PHYSICAL_ADDRESS end,
				      EFI_MEMORY_TYPE type, size_t pos)
{
	efimemmap = realloc(efimemmap, ++efimemmap_nb * sizeof(*efimemmap));
	if (!efimemmap)
		return EFI_OUT_OF_RESOURCES;

	memmove(&efimemmap[pos + 1],
		&efimemmap[pos],
		(efimemmap_nb - pos - 1) * sizeof(*efimemmap));
	set_mem_descr(&efimemmap[pos], start, end, type);

	return EFI_SUCCESS;
}

/* Insert START:END memory descriptor of type TYPE into the first
 * memory range of type EfiConventionalMemory that include START:END
 * memory region.  */
static EFI_STATUS insert_mem_descr(EFI_PHYSICAL_ADDRESS start,
				   EFI_PHYSICAL_ADDRESS end,
				   EFI_MEMORY_TYPE type)
{
	EFI_STATUS ret;
	EFI_PHYSICAL_ADDRESS cur_start, cur_end;
	EFI_MEMORY_TYPE cur_type;
	size_t i;

	if (start >= end)
		return EFI_INVALID_PARAMETER;

	for (i = 0; i < efimemmap_nb; i++) {
		cur_start = efimemmap[i].PhysicalStart;
		cur_end = cur_start + efimemmap[i].NumberOfPages * EFI_PAGE_SIZE;
		cur_type = efimemmap[i].Type;

		if (cur_start > start || end > cur_end)
			continue;

		if (efimemmap[i].Type != EfiConventionalMemory)
			return EFI_INVALID_PARAMETER;

		if (start > cur_start) {
			ret = insert_mem_descr_at(cur_start, start,
						  cur_type, i++);
			if (EFI_ERROR(ret))
				return ret;
		}

		set_mem_descr(&efimemmap[i], start, end, type);

		if (end < cur_end)
			return insert_mem_descr_at(end, cur_end,
						   cur_type, i + 1);

		return EFI_SUCCESS;
	}

	return EFI_INVALID_PARAMETER;
}

static EFI_CALCULATE_CRC32 crc32;

static EFIAPI EFI_STATUS
get_memory_map(UINTN *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap,
	       UINTN *MapKey, UINTN *DescriptorSize, UINT32 *DescriptorVersion)
{
	EFI_STATUS ret;
	UINT32 key;
	UINTN size;

	if (!MemoryMapSize || !MemoryMap || !MapKey ||
	    !DescriptorSize || !DescriptorVersion)
		return EFI_INVALID_PARAMETER;

	if (!efimemmap_nb || !efimemmap)
		return EFI_UNSUPPORTED;

	size = efimemmap_nb * sizeof(*efimemmap);
	if (size > *MemoryMapSize) {
		*MemoryMapSize = size;
		return EFI_BUFFER_TOO_SMALL;
	}

	ret = uefi_call_wrapper(crc32, 3, efimemmap, size, &key);
	if (EFI_ERROR(ret))
		return ret;

	*MemoryMapSize = size;
	memcpy(MemoryMap, efimemmap, size);
	*MapKey = key;
	*DescriptorSize = sizeof(*efimemmap);
	*DescriptorVersion = EFI_MEMORY_DESCRIPTOR_VERSION;

	return EFI_SUCCESS;
}

/* Libpayload binary boundaries */
extern char _start[], _heap[], _end[];

static EFI_GET_MEMORY_MAP saved_memmap_bs;

static EFI_STATUS lpmemmap_init(EFI_SYSTEM_TABLE *st)
{
	EFI_STATUS ret;
	EFI_PHYSICAL_ADDRESS start, data, end;

	if (!st)
		return EFI_INVALID_PARAMETER;

	if (!lib_sysinfo.n_memranges)
		return EFI_NOT_FOUND;

	ret = lpmemmap_to_efimemmap(lib_sysinfo.memrange,
				    lib_sysinfo.n_memranges);
	if (EFI_ERROR(ret))
		return ret;

	start = ALIGN_DOWN((EFI_PHYSICAL_ADDRESS)(UINTN)_start, EFI_PAGE_SIZE);
	data = ALIGN_UP((EFI_PHYSICAL_ADDRESS)(UINTN)_heap, EFI_PAGE_SIZE);
	ret = insert_mem_descr(start, data, EfiLoaderCode);
	if (EFI_ERROR(ret))
		goto err;

	end = ALIGN_UP((EFI_PHYSICAL_ADDRESS)(UINTN)_end, EFI_PAGE_SIZE);
	ret = insert_mem_descr(data, end, EfiLoaderData);
	if (EFI_ERROR(ret))
		goto err;

	saved_memmap_bs = st->BootServices->GetMemoryMap;
	st->BootServices->GetMemoryMap = get_memory_map;
	crc32 = st->BootServices->CalculateCrc32;

	return EFI_SUCCESS;

err:
	free_efimemmap();
	return ret;
}

static EFI_STATUS lpmemmap_exit(EFI_SYSTEM_TABLE *st)
{
	if (!st)
		return EFI_INVALID_PARAMETER;

	if (efimemmap) {
		st->BootServices->GetMemoryMap = saved_memmap_bs;
		free_efimemmap();
	}

	return EFI_SUCCESS;
}

ewdrv_t lpmemmap_drv = {
	.name = "lpmemmap",
	.description = "Convert Libpayload sysinfo memory map to EFI memory map",
	.init = lpmemmap_init,
	.exit = lpmemmap_exit
};
