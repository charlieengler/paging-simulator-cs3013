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

	// TODO: Check to make sure 2 bits is actually enough later in code
	uint8_t physical_page : 2;

	// c bit field (one bit is placed in the struct for swapped, valid, writeable)
	uint8_t valid : 1;
	uint8_t writable : 1;
	uint8_t swapped : 1;
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
	// TODO: This is the simple way, but should be changed for the final submission
	struct page_table_entry ptes[MM_NUM_PTES];

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
	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	// This is an example of the debug macro run when you pick a specific test to run
	// DEBUG("Virtual Address is 0x%x\n", address);

	// Simple way to convert VPN to physical page number
	// Find PTE in process' page table, then extract physical page number

	// TODO: Might want to put the following block in its own helper function
	// -------------------------------------------------------------------------------

		// Can modify the contents of the struct, but not the pointer to the struct
		struct process *const proc = &processes[pid];

		if(proc->page_table == NULL)
			proc->page_table = &proc->ptes[0];

		// Use vpn as index to find PTE for this page
		struct page_table_entry *pte = &proc->page_table[vpn];
		uint32_t physical_page_num = pte->physical_page;

	// -------------------------------------------------------------------------------

	// Phyical pointer reassembled from PPN and offset
	uint32_t physical_address = (physical_page_num << NUM_PAGE_SIZE_BITS) | offset;

	// Now we can get values from physical memory
	// *value = phys_mem[physical_address];

	// TODO: Check for the following errors:
		// pid out of range, complain
		// address out of range, complain
		// offset out of range, complain

	// TODO: Be careful of the following:
		// We have 4 page tables filled with page table zero, data table zero,
		// page table 2, and data table 2. We want to write to data table
		// one. We need to evict data pages preferrentially over page tables.
		// There is a struct that tracks what pages hold in memory (data or
		// page tables). TLDR; we never want to evict page tables which have
		// data tables that are resident in memory.

	return 0;
}

int MM_StoreByte(int pid, uint32_t address, uint8_t value) {
	return -1;
}	