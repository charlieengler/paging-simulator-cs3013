#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "mm_api.h"

int debug = 0;
void Debug() { debug = 1; }
// This is a helpful macro for adding debug prints through the code. Use it like printf.
// When running a full test suite this will be silent, but when running a single test
// Debug() will be called.
#define DEBUG(args...)	do { if (debug) { fprintf(stderr, "%s:%d: ", __FUNCTION__, __LINE__); fprintf(stderr, args); } } while(0)

///////////////////////////////////////////////////////////////////////////////
// All implementation goes in this file.                                     //
///////////////////////////////////////////////////////////////////////////////

uint8_t phys_mem[MM_PHYSICAL_MEMORY_SIZE_BYTES];

// A simple page table entry.
struct page_table_entry {
	// We need to track:
	// - physical page number
	// - permissions
	// - if this is valid
	// - if this is swapped
};

// Per-process metadata.
struct process {
	// If implementing page tables in phys_mem, this is 1 if this processes
	// page table is currently resident in phys_mem.
	uint8_t page_table_resident : 1;

	// Has a page table for this process been allocated at all?
	uint8_t page_table_exists : 1;


	// Swap file for this process.
	// You may also have a single unified swap file, but this is likely simpler.
	FILE *swap_file;

	// For simplicity, the page table for this process can be kept in this structure.
	// However, this won't achieve a perfect grade; ideal implementations are aware
	// of page tables stored in the memory itself and can handle swapping out page tables.
	// struct page_table_entry ptes[MM_NUM_PTES];

	// Pointer to this processes page table, if resident in phys_mem.
	// This doesn't need to be used although is recommended.
	struct page_table_entry *page_table;
};

struct process processes[MM_MAX_PROCESSES];

int swap_enabled = 0;

// Per physical page -> virtual page mappings, such that we can choose what
// to eject.
struct phys_page_entry {
	// Information about what is in this physical page.
	uint8_t valud : 1;
};
struct phys_page_entry phys_pages[MM_PHYSICAL_PAGES];

// Helper that returns the address in phys_mem that the phys_page metadata refers to.
void *phys_mem_addr_for_phys_page_entry(struct phys_page_entry *phys_page) {
	int page_no = phys_page - &phys_pages[0];
	return &phys_mem[page_no * MM_PAGE_SIZE_BYTES];
}

void MM_SwapOn() {
	if (!swap_enabled) {
		// Initialize swap files.
	}

	swap_enabled = 1;
}

struct MM_MapResult MM_Map(int pid, uint32_t address, int writable) {	
	CHECK(sizeof(struct page_table_entry) <= MM_MAX_PTE_SIZE_BYTES);
	uint32_t virtual_page = address >> MM_PAGE_SIZE_BITS;
	struct process *const proc = &processes[pid];

	struct MM_MapResult ret = {0};
	static char message[128];

	sprintf(message, "unimplemented");
	ret.message = message;
	ret.error = 1;
	return ret;
}

int MM_LoadByte(int pid, uint32_t address, uint8_t *value) {
	return -1;
}

int MM_StoreByte(int pid, uint32_t address, uint8_t value) {
	return -1;
}	


