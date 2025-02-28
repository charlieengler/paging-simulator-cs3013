#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "mm_api.h"

// TODO: YOUR VARIABLE NAMES ARE TERRIBLE, FIX THEM!!!

int debug = 0;
void Debug() { debug = 1; }
// This is a helpful macro for adding debug prints through the code. Use it like printf.
// When running a full test suite this will be silent, but when running a single test
// Debug() will be called.
#define DEBUG(args...)	do { if (debug) { fprintf(stderr, "%s:%d: ", __FUNCTION__, __LINE__); fprintf(stderr, args); } } while(0)

///////////////////////////////////////////////////////////////////////////////
// All implementation goes in this file.                                     //
///////////////////////////////////////////////////////////////////////////////

uint8_t phys_mem[MM_PHYSICAL_MEMORY_SIZE_BYTES] = {0};

void dump_mem(int ppn) {
	for(int i = ppn * MM_PAGE_SIZE_BYTES; i < (ppn+1) * MM_PAGE_SIZE_BYTES; i++)
		printf("%d ", phys_mem[i]);

	printf("\n\n");
}

// A simple page table entry.
struct page_table_entry {
	// TODO: Check to make sure 2 bits is actually enough later in code
	uint8_t ppn : 2; // Physical page number

	// c bit field (one bit is placed in the struct)
	uint8_t valid : 1; 		// Is the data accessible, or not?
	uint8_t writeable : 1;	// Is the data read only, or writeable?
	uint8_t present : 1;	// Is the data in phys mem, or on disk?
	uint8_t dirty : 1;		// Has the data been modified in mem, or not?
	uint8_t accesses : 2;	// How many time has this page been accessed?
};

void print_pte(struct page_table_entry *pte) {
	DEBUG("PTE:\n");
	DEBUG("		PPN: 		%d\n", pte->ppn);
	DEBUG("		valid: 		%d\n", pte->valid);
	DEBUG("		writeable: 	%d\n", pte->writeable);
	DEBUG("		present: 	%d\n", pte->present);
	DEBUG("		dirty: 		%d\n", pte->dirty);
	DEBUG("		accesses: 	%d\n", pte->accesses);
}

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

	// Pointer to this process' page table, if resident in phys_mem.
	// This doesn't need to be used although is recommended.
	// TODO: Maybe convert this to a double pointer array
	struct page_table_entry *page_table;
};

void print_process(struct process *proc) {
	DEBUG("Process:\n");
	DEBUG("		Resident: 	%d\n", proc->page_table_resident);
	DEBUG("		Exists: 	%d\n", proc->page_table_exists);
	DEBUG("		Swap File: 	%p\n", (void*)proc->swap_file);
	DEBUG("		Page Table: %p\n", (void*)proc->page_table);
}

struct process processes[MM_MAX_PROCESSES];

int swap_enabled = 0;

// Per physical page -> virtual page mappings, such that we can choose what
// to eject.
struct phys_page_entry {
	// Information about what is in this physical page.
	uint8_t valid : 1; // Is the page entry in use
	uint8_t is_page_table : 1; // 0 = data table, 1 = page table
};
struct phys_page_entry phys_pages[MM_PHYSICAL_PAGES];

// Helper that returns the address in phys_mem that the phys_page metadata refers to.
void *phys_mem_addr_for_phys_page_entry(struct phys_page_entry *phys_page) {
	int page_no = phys_page - &phys_pages[0];
	return &phys_mem[page_no * MM_PAGE_SIZE_BYTES];
}

void MM_SwapOn() {
	if (!swap_enabled) {
		char swap_initial[] = {1};

		// Create swap files for each process
		for(int i = 0; i < MM_MAX_PROCESSES; i++) {
			char path[9] = {0};
			sprintf(path, "./%d.swp", i);
			FILE *swp = fopen(path, "w+");

			processes[i].page_table_resident = 0;
			processes[i].page_table_exists = 0;
			processes[i].swap_file = swp;
		}

		// Initialize all physical page entries
		for(int i = 0; i < MM_PHYSICAL_PAGES; i++) {
			phys_pages[i].valid = 0;
			phys_pages[i].is_page_table = 0;
		}
	}

	swap_enabled = 1;
}

struct MM_MapResult MM_Map(int pid, uint32_t address, int writeable) {	
	CHECK(sizeof(struct page_table_entry) <= MM_MAX_PTE_SIZE_BYTES);
	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	struct page_table_entry *pte = &proc->page_table[vpn];

	struct MM_MapResult ret = {0};
	static char message[128];

	sprintf(message, "success");
	ret.message = message;
	ret.error = 0;

	return ret;
}

int MM_LoadByte(int pid, uint32_t address, uint8_t *value) {
	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	struct process *const proc = &processes[pid];

	if(proc->page_table == NULL)
		proc->page_table = &proc->ptes[0];

	// Use vpn as index to find PTE for this page
	struct page_table_entry *pte = &proc->page_table[vpn];

	// Phyical pointer reassembled from PPN and offset
	uint32_t physical_address = ((uint32_t)ppn << MM_PAGE_SIZE_BITS) | offset;

	// Now we can get values from physical memory
	*value = phys_mem[physical_address];

	// TODO: Check for the following errors (should be helper function):
		// pid out of range, complain
		// address out of range, complain
		// offset out of range, complain
		// physical page is invalid, complain

	return 0;
}

int MM_StoreByte(int pid, uint32_t address, uint8_t value) {
	// TODO: Maybe make a macro for vpn and offset
	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	struct process *const proc = &processes[pid];

	// Use vpn as index to find PTE for this page
	struct page_table_entry *pte = &proc->page_table[vpn];

	// Phyical pointer reassembled from PPN and offset
	uint32_t physical_address = ((uint32_t)ppn << MM_PAGE_SIZE_BITS) | offset;

	phys_mem[physical_address] = value;

	return 0;
}