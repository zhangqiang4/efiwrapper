#ifndef RDTSC_H
#define RDTSC_H

#include <stdint.h>

static uint64_t rdtsc(void)
{
	uint32_t lo, hi;

	asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
	return (uint64_t) hi << 32 | lo;
}

#endif
