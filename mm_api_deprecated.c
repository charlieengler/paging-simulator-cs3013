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
	// TODO: Consider using 2 bits for this because it will be sufficient for this project.
	// 		 That being said, document this well if you go with that design decision.
	//		 It would just be nice because it would make this struct exactly 2 bytes.
	int pid; // The ID of process using this page
	uint8_t valid : 1; // Is the page entry in use
	uint8_t is_page_table : 1; // 0 = data table, 1 = page table
	int vpn : 4; // The VPN of this page entry in its PTE (signed because -1 would be invalid)
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
			for(int j = 0; j < (MM_PAGE_SIZE_BYTES + 1) * MM_NUM_PTES; j++)
				fputc(0, swp);
			

			processes[i].page_table_resident = 0;
			processes[i].page_table_exists = 0;
			processes[i].swap_file = swp;
		}

		// Initialize all physical page entries
		for(int i = 0; i < MM_PHYSICAL_PAGES; i++) {
			phys_pages[i].pid = -1;
			phys_pages[i].vpn = -1;
			phys_pages[i].valid = 0;
			phys_pages[i].is_page_table = 0;
		}
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

// TODO: Should return PPN on success and -1 on failure
int load_page(struct page_table_entry *pte, int ppn, int new_pid, uint8_t vpn) {
	if(swap_enabled && pte->valid) {
		// DEBUG("Old PID: %d\n", phys_pages[ppn].pid);
		// DEBUG("New PID: %d\n", new_pid);
		struct process *const proc = &processes[new_pid];
		// DEBUG("Swap file pointer: %p\n", (void*)proc->swap_file);
		FILE *swap_page = proc->swap_file;
		fseek(swap_page, vpn * MM_PAGE_SIZE_BYTES, SEEK_SET);
		uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn]);
		for(int i = 0; i < MM_PAGE_SIZE_BYTES; i++) {
			int c = fgetc(swap_page);

			if(c == EOF)
				c = 0;

			mem[i] = (uint8_t)c;
		}
	}

	phys_pages[ppn].pid = new_pid;
	phys_pages[ppn].valid = 1;
	phys_pages[ppn].is_page_table = 0;
	phys_pages[ppn].vpn = vpn;

	pte->ppn = ppn;
	pte->valid = 1;
	pte->present = 1;
	pte->dirty = 0;
	// Increment the accesses flag so the manager knows this page is important
	pte->accesses = pte->accesses < 3 ? pte->accesses + 1 : 3;

	return ppn;
}

// TODO: Should return old PPN of page table
// TODO: Should maybe eject all associated data tables, too
int eject_page_table(int ppn, int pid) {
	// TODO: Check to make sure PPN is valid and holding the data we think it is
	// TODO: Check to see if the page we're ejecting is a page table

	// DEBUG("Swapped PPN: %d\n", ppn);
	// DEBUG("Ejected PTE:\n");
	// print_pte(pte);

	// TODO: See if the page table is dirty or not

	struct process *proc = &processes[pid];
	FILE *swap_page = proc->swap_file;
	fseek(swap_page, MM_NUM_PTES * MM_PAGE_SIZE_BYTES, SEEK_SET);
	uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn]);
	for(int i = 0; i < MM_PAGE_SIZE_BYTES; i++)
		fprintf(swap_page, "%c", mem[i]);

	phys_pages[ppn].valid = 0;
	phys_pages[ppn].is_page_table = 0;

	// TODO: Find a place for these
	// phys_pages[ppn].pid = -1;
	// phys_pages[ppn].vpn = -1;

	return ppn;
}

// Returns the PPN that was ejected (-1 on failure)
// TODO: PID argument might be useless
int eject_page(int ppn) {
	if(!swap_enabled) {
		DEBUG("eject should not be called if swap is disabled\n");
		return -1;
	}

	struct process *const proc = &processes[phys_pages[ppn].pid];
	struct page_table_entry *pte = &(proc->page_table[phys_pages[ppn].vpn]);

	// TODO: Check to make sure PPN is valid and holding the data we think it is
	// TODO: Check to see if the page we're ejecting is a page table

	// If the data hasn't been modified, then it can just be ejected
	if(pte->dirty) {
		FILE *swap_page = proc->swap_file;
		fseek(swap_page, phys_pages[ppn].vpn * MM_PAGE_SIZE_BYTES, SEEK_SET);
		uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn]);
		for(int i = 0; i < MM_PAGE_SIZE_BYTES; i++)
			fprintf(swap_page, "%c", mem[i]);
	}

	// TODO: Find a place for these
	phys_pages[ppn].pid = -1;
	phys_pages[ppn].vpn = -1;
	phys_pages[ppn].valid = 0;
	phys_pages[ppn].is_page_table = 0;
	
	pte->ppn = -1;
	pte->present = 0;
	pte->dirty = 0;
	pte->accesses = 0;

	return ppn;
}

// Returns the PPN that the page was swapped into (-1 on failure)
int swap_page(int pid, struct page_table_entry *fresh_pte, uint8_t vpn) {
	// DEBUG("Swapped PTE:\n");
	// print_pte(fresh_pte);

	// The highest the accessed field can reach is 3 (max of 2 bits)
	int lowest_accessed = 4;
	// TODO: Rename me back to ejected_pte
	struct page_table_entry *preferred_pte = NULL;

	int ppn = -1;
	for(int i = 0; i < MM_PHYSICAL_PAGES; i++) {
		if(phys_pages[i].is_page_table)
			continue;

		struct process *const tmp_proc = &processes[phys_pages[i].pid];
		struct page_table_entry *tmp_pte = &(tmp_proc->page_table[phys_pages[i].vpn]);

		// Otherwise, pick the page with the least accesses
		if(tmp_pte->accesses < lowest_accessed) {
			lowest_accessed = tmp_pte->accesses;
			ppn = i;
		}
	}

	struct process *const proc = &processes[pid];

	if(proc->page_table == NULL)
		proc->page_table = &proc->ptes[0];

	struct page_table_entry *ptes = proc->page_table;

	if(ppn == -1) {
		DEBUG("Page Table: %d %d\n", phys_pages[ppn].pid, phys_pages[ppn].vpn);
		// Eject the first page table that isn't in use from memory
		for(int i = 0; i < MM_PHYSICAL_PAGES; i++) {
			if(phys_pages[i].pid != pid) {
				ppn = eject_page_table(i, phys_pages[i].pid);
				break;
			}
		}
		// TODO: Throw an error if none were ejected still, but we should never get there
	} else {
		ppn = eject_page(ppn);
	}

	ppn = load_page(fresh_pte, ppn, pid, vpn);

	// DEBUG("Ejected PPN: %d\n", ppn);

	if(ppn == -1) {
		DEBUG("unable to find viable ppn\n");
		return -1;
	}

	return ppn;
}

// TODO: Throw page faults on errors
// TODO: Maybe consolidate instances of proc using pointers
int reserve_ppn(int pid, struct page_table_entry *pte, uint8_t vpn) {
	// Checks physical pages to see if they're ununsed (not valid) or don't belong to
	// the current process
	
	for(int i = 0; i < MM_PHYSICAL_PAGES; i++)
		if(!phys_pages[i].valid)
			return i;

	// if(phys_pages[ppn].valid) {
	// 	DEBUG("attempting to access valid PPN (how did we get here?)\n");
	// 	return -1;
	// }

	// If a viable physical page couldn't be found and swap is enabled, then checking for
	// the best swap candidate is allowed
	int ppn = -1;
	if(swap_enabled)
		ppn = swap_page(pid, pte, vpn);

	// TODO: Check disk for the page we're trying to swap in. If it's supposed to be
	//		 valid and it's not, then throw an error. Otherwise, load it from disk into mem

	return ppn;
}

int reserve_page_table_ppn(int pid) {
	// The PPN should always be assigned here, so zero is a fine default
	int ppn = 0;

	// We would prefer to assign to a page that isn't already a page table, though
	for(int i = 0; i < MM_PHYSICAL_PAGES; i++) {
		if(!phys_pages[i].valid)
			return i;

		if(!phys_pages[i].is_page_table)
			ppn = i;
	}

	struct process *const proc = &processes[pid];

	if(proc->page_table == NULL)
		proc->page_table = &(proc->ptes[0]);

	// TODO: Eject page and/or page table
	if(phys_pages[ppn].is_page_table) {
		ppn = eject_page_table(ppn, pid);
	} else {
		struct page_table_entry *pte = &(proc->page_table[phys_pages[ppn].vpn]);
		ppn = eject_page(ppn);
	}

	return ppn;
}

// TODO: Should return -1 on failure, otherwise should return valid PPN
int load_page_table(int pid) {
	struct process *proc = &processes[pid];
	int ppn = reserve_page_table_ppn(pid);

	// TODO: Error checking
	if(ppn == -1) {
		return -1;
	}

	// TODO: Error checking
	if(proc == NULL) {
		return -1;
	}

	FILE *swap_page = proc->swap_file;
	// Seek to the end of the normal PTE entries in the swap file
	fseek(swap_page, MM_NUM_PTES * MM_PAGE_SIZE_BYTES, SEEK_SET);
	uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn]);
	for(int i = 0; i < MM_PAGE_SIZE_BYTES; i++) {
		int c = fgetc(swap_page);

		if(c == EOF)
			c = 0;

		mem[i] = (uint8_t)c;
	}

	phys_pages[ppn].pid = pid;
	phys_pages[ppn].valid = 1;
	phys_pages[ppn].is_page_table = 1;
	phys_pages[ppn].vpn = -1;

	proc->page_table = (struct page_table_entry*)mem;
	proc->page_table_exists = 1;
	proc->page_table_resident = 1;

	return ppn;
}

int create_page_table(int pid) {
	struct process *proc = &processes[pid];
	int ppn = reserve_page_table_ppn(pid);

	// TODO: Error checking
	if(ppn == -1) {
		DEBUG("ppn is -1");
		return -1;
	}

	// TODO: Error checking
	if(proc == NULL) {
		DEBUG("proc is NULL");
		return -1;
	}

	uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn]);

	phys_pages[ppn].pid = pid;
	phys_pages[ppn].valid = 1;
	phys_pages[ppn].is_page_table = 1;
	phys_pages[ppn].vpn = -1;

	proc->page_table = (struct page_table_entry*)mem;
	proc->page_table_exists = 1;
	proc->page_table_resident = 1;

	for(int i = 0; i < MM_NUM_PTES; i++) {
		struct page_table_entry tmp_pte = {0};

		proc->page_table[i] = tmp_pte;
	}

	return ppn;
}

struct MM_MapResult MM_Map(int pid, uint32_t address, int writeable) {	
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
		ret.message = message;

		DEBUG("check mem info issue\n");

		return ret;
	}

	struct process *const proc = &processes[pid];

	struct page_table_entry *pte = &(proc->page_table[vpn]);

	// Check if process' page table is resident in memory or exists just in case
	// TODO: Clean up this if statement
	if(!proc->page_table_resident) {
		if(!proc->page_table_exists) {
			// TODO: Error checking
			create_page_table(pid);
		} else {
			// TODO: Error checking
			load_page_table(pid);
		}

		pte = &(proc->page_table[vpn]);
	}

	// TODO: Error handling
	int ppn = -1;
	if(pte->present) {
		ppn = pte->ppn;
	} else {
		ppn = reserve_ppn(pid, pte, vpn);
	}

	if(!swap_enabled && ppn == -1) {
		sprintf(message, "swap disabled and pages full");
		ret.error = 1;
	}

	// TODO: Error checking
	load_page(pte, ppn, pid, vpn);

	pte->writeable = writeable;

	ret.message = message;

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

	if(!pte->present) {
		// TODO: Error checking
		MM_Map(pid, address, 0);
	}

	if(!pte->valid) {
		return -1;
	}

	int ppn = pte->ppn;

	// TODO: Better error checking
	if(ppn == -1) {
		return -1;
	}

	// Phyical pointer reassembled from PPN and offset
	uint32_t physical_address = ((uint32_t)ppn << MM_PAGE_SIZE_BITS) | offset;

	// Now we can get values from physical memory
	*value = phys_mem[physical_address];

	// printf("Read PID %d, VPN %d\n", pid, vpn);
	// dump_mem(ppn);

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

	if(proc->page_table == NULL)
		proc->page_table = &proc->ptes[0];

	// Use vpn as index to find PTE for this page
	struct page_table_entry *pte = &proc->page_table[vpn];

	if(!pte->present) {
		// TODO: Error checking
		MM_Map(pid, address, 1);
	}

	// Throw an error if the entry is readonly
	if(!pte->writeable) {
		DEBUG("attempt to write to readonly\n");
		return -1;
	}

	int ppn = pte->ppn;	

	// TODO: Better error checking
	if(ppn == -1) {
		return -1;
	}

	// Data in this pte has been modified, so it needs to be rewritten to disk
	pte->dirty = 1;

	// Phyical pointer reassembled from PPN and offset
	uint32_t physical_address = ((uint32_t)ppn << MM_PAGE_SIZE_BITS) | offset;

	phys_mem[physical_address] = value;

	// printf("Store PID %d, VPN %d:\n", pid, vpn);
	// dump_mem(ppn);
	
	return 0;
}