#pragma once
#include <stdbool.h>
#include <stdint.h>

void LastWriterLoad(void *address, uint64_t bbID, uint64_t datasize);
void LastWriterStore(void *address, uint64_t bbID, uint64_t datasize);
void LastWriterIncrement(uint64_t a);
void LastWriterInitialization(uint64_t a);
void LastWriterDestroy();