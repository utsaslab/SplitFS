#include "utils.h"

size_t align_next_page(size_t address) {
	return ((address + PAGE_SIZE) >> PAGE_SHIFT) << PAGE_SHIFT;
}

size_t align_cur_page(size_t address) {
	return (address >> PAGE_SHIFT) << PAGE_SHIFT;
}

off_t align_page_offset(off_t cur_offset, off_t target_offset)
{
  off_t offset_in_page = target_offset % PAGE_SIZE;
  off_t cur_offset_in_page = cur_offset % PAGE_SIZE;
  if (cur_offset_in_page != 0) {
    cur_offset += (PAGE_SIZE - cur_offset_in_page);
  }
  cur_offset += offset_in_page;
  return cur_offset;
}
