/*
 * MMU definitions for dandelion shim
 */
#ifndef _MMU_H_
#define _MMU_H_

#include <stdint.h>

void mmu_map_payload(uint64_t phys_base, uint64_t mem_size);
void mmu_enable(void);
void mmu_disable(void);

#endif /* _MMU_H_ */
