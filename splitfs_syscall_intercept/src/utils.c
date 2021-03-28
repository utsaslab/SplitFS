#include "utils.h"

size_t align_next_page(size_t address) {
	return ((address + PAGE_SIZE) >> PAGE_SHIFT) << PAGE_SHIFT;
}

size_t align_cur_page(size_t address) {
	return (address >> PAGE_SHIFT) << PAGE_SHIFT;
}