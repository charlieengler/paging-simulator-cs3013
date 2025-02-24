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
	uint8_t ppn : 2; // Physical page number

	// c bit field (one bit is placed in the struct)
	uint8_t valid : 1; 		// Is the data accessible, or not?
	uint8_t writable : 1;	// Is the data read only, or writable?
	uint8_t present : 1;	// Is the data in phys mem, or on disk?
	uint8_t dirty : 1;		// Has the data been modified in mem, or not?
	uint8_t accessed : 1;	// Has page been accessed yet, or not?
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

	// Pointer to this process' page table, if resident in phys_mem.
	// This doesn't need to be used although is recommended.
	struct page_table_entry *page_table;
};

struct process processes[MM_MAX_PROCESSES];

int swap_enabled = 0;

// Per physical page -> virtual page mappings, such that we can choose what
// to eject.
struct phys_page_entry {
	// Information about what is in this physical page.
	int pid; // The ID of process using this page
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
		// Create swap files for each process
		for(int i = 0; i < MM_MAX_PROCESSES; i++) {
			char path[9] = {0};
			sprintf(path, "./%d.swp", i);
			FILE *swp = fopen(path, "w+");

			processes[i].swap_file = swp;
		}

		// Initialize all physical page entry PIDs to 1
		for(int i = 0; i < MM_PHYSICAL_PAGES; i++)
			phys_pages[i].pid = -1;
	}

	swap_enabled = 1;
}

int check_mem_info(int pid, uint32_t address, char message[128]) {
	// PID out of range
	if(pid >= MM_MAX_PROCESSES || pid < 0) {
		sprintf(message, "pid out of range");
		return 1;
	}

	// Address out of range
	if(address > MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES) {
		sprintf(message, "address out of range");
		return 1;
	}

	return 0;
}

// Returns the PPN that the page was swapped into
int swap_page(int pid, uint8_t vpn) {
	return -1;
}

struct MM_MapResult MM_Map(int pid, uint32_t address, int writable) {	
	CHECK(sizeof(struct page_table_entry) <= MM_MAX_PTE_SIZE_BYTES);
	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	struct MM_MapResult ret = {0};
	static char message[128];

	sprintf(message, "success");
	ret.error = 0;

	// Check for errors in the memory
	if(check_mem_info(pid, address, message)) {
		ret.error = 1;
		goto finish_map;
	}

	// Checks physical pages to see if they're ununsed (not valid) or don't belong to
	// the current process. Reserve physical page zero for page table if swap is disabled
	int ppn = -1;
	for(int i = !(swap_enabled & 0x1); i < MM_PHYSICAL_PAGES; i++) {
		if(phys_pages[i].valid && phys_pages[i].pid == pid)
			continue;

		ppn = i;
		break;
	}

	// If a viable physical page couldn't be found and swap is enabled, then checking for
	// the best swap candidate is allowed
	if(ppn == -1 && swap_enabled) {
		// TODO: Be careful of the following:
			// We have 4 page tables filled with page table zero, data table zero,
			// page table 2, and data table 2. We want to write to data table
			// one. We need to evict data pages preferrentially over page tables.
			// There is a struct that tracks what pages hold in memory (data or
			// page tables). TLDR; we never want to evict page tables which have
			// data tables that are resident in memory.

		ppn = swap_page(pid, vpn);
	}

	// If a physical page number couldn't be found, throw an error
	if(ppn == -1) {
		sprintf(message, "unable to find available page");
		ret.error = 1;
		goto finish_map;
	}

	phys_pages[ppn].valid = 1;
	phys_pages[ppn].pid = pid;

	struct page_table_entry pte = {0};

	pte.ppn = ppn;
	pte.valid = 1;
	pte.writable = writable;
	pte.present = 1;
	pte.dirty = 0;
	pte.accessed = 0;

	// TODO: This is the temporary array for storing PTEs and should be switched for
	//		 a reference to a dedicated page holding this processes page table
	struct process *const proc = &processes[pid];
	proc->ptes[vpn] = pte;
	proc->page_table = proc->ptes;

finish_map:
	ret.message = message;

	return ret;
}

int MM_GetPPN(int pid, uint8_t vpn, int writeable) {
	// Can modify the contents of the struct, but not the pointer to the struct
	struct process *const proc = &processes[pid];

	if(proc->page_table == NULL)
		proc->page_table = &proc->ptes[0];

	// Use vpn as index to find PTE for this page
	struct page_table_entry *pte = &proc->page_table[vpn];

	// Throw an error if the entry is invalid
	if(pte->valid == 0) {
		DEBUG("invalid PTE entry\n");
		return -1;
	}

	// Throw an error if the entry is readonly
	if(pte->writable == 0 && writeable) {
		DEBUG("attempt to write to readonly\n");
		return -1;
	}

	// The physical page number is fetched from the PTE
	uint8_t ppn = pte->ppn;

	return ppn;
}

int MM_LoadByte(int pid, uint32_t address, uint8_t *value) {
	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	// This is an example of the debug macro run when you pick a specific test to run
	// DEBUG("Virtual Address is 0x%x\n", address);

	// Simple way to convert VPN to physical page number
	// Find PTE in process' page table, then extract physical page number

	int ppn = MM_GetPPN(pid, vpn, 0);
	if(ppn == -1)
		return -1;

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

	int ppn = MM_GetPPN(pid, vpn, 1);	
	if(ppn == -1)
		return -1;

	// Phyical pointer reassembled from PPN and offset
	uint32_t physical_address = ((uint32_t)ppn << MM_PAGE_SIZE_BITS) | offset;

	phys_mem[physical_address] = value;
	
	return 0;
}