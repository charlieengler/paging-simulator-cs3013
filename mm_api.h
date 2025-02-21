#ifndef MM_API_H__
#define MM_API_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

// Helper macro for quick error checking.
#define CHECK(x)	do { if (!(x)) { fprintf(stderr, "%s:%d CHECK failed: %s, errno %d %s\n", __FILE__, __LINE__, #x, errno, strerror(errno)); abort(); } } while(0)

// Maximum processes allowed.
// Valid values of 'pid' arguments are 0, 1, 2, 3.
#define MM_MAX_PROCESSES			4

typedef uint8_t pte_page_t;
#define MM_PAGE_SIZE_BITS			4		// 16b pages (fits 8 * 2 byte PTEs)
#define MM_PHYSICAL_MEMORY_SIZE_SHIFT		(MM_PAGE_SIZE_BITS + 2)	// 4 pages physical mem
#define MM_PROCESS_VIRTUAL_MEMORY_SIZE_SHIFT	(MM_PHYSICAL_MEMORY_SIZE_SHIFT + 1)	// 8 pages virtual mem
#define MM_MAX_PTE_SIZE_BYTES			2		// Each page table entry is 1-2 bytes.


#define MM_PAGE_SIZE_BYTES			(1 << MM_PAGE_SIZE_BITS)
#define MM_PAGE_OFFSET_MASK			(MM_PAGE_SIZE_BYTES - 1)

#define MM_PHYSICAL_MEMORY_SIZE_BYTES		(1 << MM_PHYSICAL_MEMORY_SIZE_SHIFT)
#define MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES	(1 << MM_PROCESS_VIRTUAL_MEMORY_SIZE_SHIFT)
#define MM_PHYSICAL_PAGES			(MM_PHYSICAL_MEMORY_SIZE_BYTES / MM_PAGE_SIZE_BYTES)
#define MM_NUM_PTES				(MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES / MM_PAGE_SIZE_BYTES)
#define MM_PAGE_TABLE_SIZE_BYTES		(MM_NUM_PTES * MM_MAX_PTE_SIZE_BYTES)

#if MM_PAGE_TABLE_SIZE_BYTES > MM_PAGE_SIZE_BYTES
#error "Cannot fit page table in single page for assignment simplicity"
#endif

// Results of a MM_Map() function call.
struct MM_MapResult {
	int error;
	const char *message;	// Does not need to be freed.
	int new_mapping;
};

// Map a page of memory for the requested process.
// If 'writable' is non-zero, the page is mapped read/write. Otherwise, the
// page is mapped read-only. 'address' is the virtual address requested,
// not the page number. If the page corresponding to 'address' is unmapped,
// create a pagetable entry. If the page is already mapped, update the
// permission bits of the mapping to adhere to the new 'writable' setting.
struct MM_MapResult MM_Map(int pid, uint32_t address, int writable);

// Enable Swap Whether to enable Swap in the memory manager. This should
// open a file on the filesystem, and allow storage of virtual pages
// in the file backing.
void MM_SwapOn();

// Load a byte from the specified address.
// 0 is returned for a valid load operation. If the page is not mapped,
// and AutoMap is not enabled, return -1.
int MM_LoadByte(int pid, uint32_t address, uint8_t *value);

// Store a byte in the specified address. 
// 0 is returned for a valid store operation. If the page is not mapped,
// is mapped read-only, or AutoMap is not enabled, return -1.
// The memory should be modified ONLY if the return value is zero.
int MM_StoreByte(int pid, uint32_t address, uint8_t value);

// Turn on debug statements.
void Debug();

#ifdef __cplusplus
}
#endif

#endif	// MM_API_H__
