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

	phys_pages[ppn].pid = new_pid;
	phys_pages[ppn].valid = 1;
	phys_pages[ppn].is_page_table = 0;
	phys_pages[ppn].vpn = vpn;

	pte->ppn = ppn;
	pte->valid = 1;
	pte->present = 1;
	// Increment the accesses flag so the manager knows this page is important
	pte->accesses = pte->accesses < 3 ? pte->accesses + 1 : 3;

	return ppn;
}

// Returns the PPN that was ejected (-1 on failure)
// TODO: PID argument might be useless
int eject_page(struct page_table_entry *pte, int pid) {
	int ppn = pte->ppn;

	// TODO: Check to make sure PPN is valid and holding the data we think it is
	// TODO: Check to see if the page we're ejecting is a page table

	// DEBUG("Swapped PPN: %d\n", ppn);
	// DEBUG("Ejected PTE:\n");
	// print_pte(pte);

	// If the data hasn't been modified, then it can just be ejected
	if(pte->dirty) {
		struct process *const proc = &processes[phys_pages[ppn].pid];
		FILE *swap_page = proc->swap_file;
		fseek(swap_page, phys_pages[ppn].vpn * MM_PAGE_SIZE_BYTES, SEEK_SET);
		uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn]);
		for(int i = 0; i < MM_PAGE_SIZE_BYTES; i++)
			fprintf(swap_page, "%c", mem[i]);
	}

	// TODO: Find a place for these
	// phys_pages[ppn].pid = -1;
	// phys_pages[ppn].vpn = -1;
	phys_pages[ppn].valid = 0;
	phys_pages[ppn].is_page_table = 0;
	
	pte->ppn = -1;
	pte->present = 0;
	pte->dirty = 0;
	pte->accesses = 0;

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

// Returns the PPN that the page was swapped into (-1 on failure)
int swap_page(int pid, struct page_table_entry *fresh_pte, uint8_t vpn) {
	// DEBUG("Swapped PTE:\n");
	// print_pte(fresh_pte);

	struct process *const proc = &processes[pid];

	if(proc->page_table == NULL)
		proc->page_table = &proc->ptes[0];

	struct page_table_entry *ptes = proc->page_table;

	// The highest the accessed field can reach is 3 (max of 2 bits)
	int lowest_accessed = 4;
	struct page_table_entry *ejected_pte = NULL;
	// By the time we get here, all of the physical pages should belong to the
	// same process, so we don't have to check the PID. We reserve the first PTE
	// for the page table itself
	for(int i = 0; i < MM_NUM_PTES; i++) {
		// If the PTE isn't in memory, then we continue
		if(!(&ptes[i])->present)
			continue;

		// If the PTE is the page table, then we shouldn't swap it
		if(phys_pages[(&ptes[i])->ppn].is_page_table)
			continue;

		// Otherwise, pick the page with the least accesses
		if((&ptes[i])->accesses < lowest_accessed) {
			ejected_pte = &ptes[i];
			lowest_accessed = ejected_pte->accesses;
		}
	}

	int ppn = -1;
	if(ejected_pte == NULL) {
		for(int i = 0; i < MM_PHYSICAL_PAGES; i++) {
			if(phys_pages[i].pid != pid) {
				ppn = eject_page_table(i, phys_pages[i].pid);
				break;
			}
		}
		
		ppn = load_page(fresh_pte, ppn, pid, vpn);
	} else {
		ppn = eject_page(ejected_pte, pid);
		ppn = load_page(fresh_pte, ppn, pid, vpn);
	}

	// DEBUG("Ejected PPN: %d\n", ppn);

	if(ppn == -1) {
		DEBUG("unable to find viable ppn\n");
		return -1;
	}

	return ppn;
}

int get_page_table_ppn(int pid) {
	// The PPN should always be assigned here, so zero is a fine default
	int ppn = 0;

	// We would prefer to assign to a page that isn't already a page table, though
	for(int i = 0; i < MM_PHYSICAL_PAGES; i++) {
		if(!phys_pages[i].is_page_table && !phys_pages[i].valid) {
			ppn = i;
			break;
		}
	}

	struct process *const proc = &processes[pid];

	if(proc->page_table == NULL)
		proc->page_table = &(proc->ptes[0]);

	if(!phys_pages[ppn].valid)
		return ppn;

	// TODO: Eject page and/or page table
	if(phys_pages[ppn].is_page_table) {
		ppn = eject_page_table(ppn, pid);
		
	} else {
		struct page_table_entry *pte = &(proc->page_table[phys_pages[ppn].vpn]);
		ppn = eject_page(pte, pid);
	}

	return ppn;
}

int create_page_table(int pid) {
	struct process *proc = &processes[pid];
	int ppn = get_page_table_ppn(pid);

	// TODO: Error checking
	if(ppn == -1) {
		return -1;
	}

	// TODO: Error checking
	if(proc == NULL) {
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

	return ppn;
}

// TODO: Should return -1 on failure, otherwise should return valid PPN
int load_page_table(int pid) {
	struct process *proc = &processes[pid];
	int ppn = get_page_table_ppn(pid);

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

// TODO: Throw page faults on errors
// TODO: Maybe consolidate instances of proc using pointers
int get_ppn(int pid, struct page_table_entry *pte, uint8_t vpn, int writeable) {
	struct process *const proc = &processes[pid];
	
	// Check if process' page table is resident in memory or exists just in case
	if(!proc->page_table_resident || !proc->page_table_exists) {
		// Check if process has page table allocated
		if(proc->page_table_exists) {
			// TODO: Error checking
			load_page_table(pid);
		} else {
			// TODO: Error checking
			create_page_table(pid);
		}
	}

	// Throw an error if the entry is invalid
	// if(pte->valid == 0) {
	// 	DEBUG("invalid PTE entry\n");
	// 	return -1;
	// }

	// if(pte->present) {
	// 	DEBUG("Get:\n");
	// 	print_pte(pte);
	// 	DEBUG("%p\n", (void*)pte);
	// 	DEBUG("%d\n", pid);
	// 	DEBUG("%d\n", vpn);
	// 	DEBUG("\n");
	// }

	// If the page is already present in memory, then just return it's PPN
	if(pte->present)
		return pte->ppn;

	// Checks physical pages to see if they're ununsed (not valid) or don't belong to
	// the current process. If swap is disabled, reserve index 0 for page table
	int ppn = -1;
	for(int i = 0; i < MM_PHYSICAL_PAGES; i++) {
		if(!phys_pages[i].valid) {
			ppn = i;
			break;
		}

		// if(!swap_enabled)
		// 	continue;

		// if(phys_pages[i].pid == pid)
		// 	continue;

		// // Ejecting page tables is handled differently
		// if(phys_pages[i].is_page_table) {
		// 	ppn = eject_page_table(ppn, pid);
		// 	break;
		// }

		// struct process *const old_proc = &processes[phys_pages[i].pid];
		// if(old_proc->page_table == NULL)
		// 	old_proc->page_table = &old_proc->ptes[0];

		// struct page_table_entry *old_pte = &(old_proc->page_table[phys_pages[i].vpn]);

		// // ppn = eject_page(old_pte, pid);
		// // ppn = load_page(pte, ppn, pid, vpn);
		// // DEBUG("Different PID Ejected PPN: %d\n", ppn);
		// // DEBUG("Different PID i value PPN: %d\n", i);
		// break;
	}

	if(phys_pages[ppn].valid) {
		DEBUG("attempting to access valid PPN (how did we get here?)\n");
		return -1;
	}

	// If a viable physical page couldn't be found and swap is enabled, then checking for
	// the best swap candidate is allowed
	if(ppn == -1 && swap_enabled)
		ppn = swap_page(pid, pte, vpn);

	// TODO: Check disk for the page we're trying to swap in. If it's supposed to be
	//		 valid and it's not, then throw an error. Otherwise, load it from disk into mem

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

		return ret;
	}

	struct page_table_entry temp_pte = {0};
	struct page_table_entry *pte = &temp_pte;

	pte->valid = 1;
	pte->writeable = writeable;
	pte->present = 0;
	pte->dirty = 0;
	pte->accesses = 0;

	int ppn = get_ppn(pid, pte, vpn, writeable);

	if(ppn > -1 && ppn < MM_PHYSICAL_PAGES) {
		phys_pages[ppn].pid = pid;
		phys_pages[ppn].valid = 1;
		phys_pages[ppn].is_page_table = 0;
		phys_pages[ppn].vpn = vpn;

		pte->ppn = ppn;
		pte->present = 1;
	}

	struct process *const proc = &processes[pid];
	proc->page_table[vpn] = *pte;

	if(!swap_enabled && ppn == -1) {
		sprintf(message, "swap disabled and pages full");
		ret.error = 1;
	}

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

	MM_Map(pid, address, 0);
	int ppn = get_ppn(pid, pte, vpn, 0);
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

	struct process *const proc = &processes[pid];

	if(proc->page_table == NULL)
		proc->page_table = &proc->ptes[0];

	// Use vpn as index to find PTE for this page
	struct page_table_entry *pte = &proc->page_table[vpn];

	// Throw an error if the entry is readonly
	if(!pte->writeable) {
		DEBUG("attempt to write to readonly\n");
		return -1;
	}

	// DEBUG("Store:\n");
	// print_pte(pte);
	// DEBUG("%p\n", (void*)pte);
	// DEBUG("%d\n", pid);
	// DEBUG("%d\n", vpn);
	// DEBUG("\n");

	MM_Map(pid, address, 1);
	int ppn = get_ppn(pid, pte, vpn, 1);	
	if(ppn == -1)
		return -1;

	// Data in this pte has been modified, so it needs to be rewritten to disk
	pte->dirty = 1;

	// Phyical pointer reassembled from PPN and offset
	uint32_t physical_address = ((uint32_t)ppn << MM_PAGE_SIZE_BITS) | offset;

	phys_mem[physical_address] = value;
	
	return 0;
}