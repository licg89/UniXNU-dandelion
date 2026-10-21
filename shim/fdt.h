/*
 * FDT parsing for dandelion shim
 */
#ifndef _FDT_H_
#define _FDT_H_

#include <stdint.h>

int fdt_chosen_u64(uintptr_t dtb, const char *prop, uint64_t *out);

#endif /* _FDT_H_ */
