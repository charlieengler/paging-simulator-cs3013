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
		printf("%x ", phys_mem[i]);

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

// TODO: Should probably do error handling
void write_pte_to_mem(struct page_table_entry *pte, uint8_t *mem_addr) {
	uint8_t val = 0;

	val = val | pte->accesses << 6;
	val = val | pte->dirty << 5;
	val = val | pte->present << 4;
	val = val | pte->writeable << 3;
	val = val | pte->valid << 2;
	val = val | pte->ppn;

	*mem_addr = val;
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
	// TODO: Consider using fewer bits for these
	int pid;
	int vpn;
	uint8_t valid : 1; // Is the page entry in use
	uint8_t is_page_table : 1; // 0 = data table, 1 = page table
	// TODO: Consider adding a dirty bit for ejection
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
			phys_pages[i].pid = -1;
			phys_pages[i].vpn = -1;
			phys_pages[i].valid = 0;
			phys_pages[i].is_page_table = 0;
		}
	}

	swap_enabled = 1;
}

int eject_phys_page(int reserving_pid) {
	// TODO: This could just be renamed to ppn because it refers to the same thing whether it's the new data or not
	int ppn_to_eject = -1;
	for(int i = 0; i < MM_PHYSICAL_PAGES; i++) {
		if(phys_pages[i].pid != reserving_pid) {
			ppn_to_eject = i;
			break;
		}

		if(!phys_pages[i].is_page_table)
			ppn_to_eject = i;

		// TODO: There should be a backup if all physical pages are page tables
	}

	if(ppn_to_eject == -1) {
		DEBUG("couldn't eject a page (shouldn't get here)\n");
		return -1;
	}

	struct process *const proc = &processes[phys_pages[ppn_to_eject].pid];

	int vpn_to_eject = MM_NUM_PTES;

	int is_dirty = 1;

	if(!phys_pages[ppn_to_eject].is_page_table) {
		vpn_to_eject = phys_pages[ppn_to_eject].vpn;

		struct page_table_entry *pte_to_eject = &(proc->page_table[vpn_to_eject]);
		pte_to_eject->ppn = -1;
		pte_to_eject->present = 0;
		is_dirty = pte_to_eject->dirty;
		pte_to_eject->dirty = 0;
		pte_to_eject->accesses = 0;
	}

	// DEBUG("Ejecting PPN: %d\n", ppn_to_eject);
	// dump_mem(ppn_to_eject);

	if(is_dirty) {
		FILE *swap_file = proc->swap_file;
		fseek(swap_file, vpn_to_eject * MM_PAGE_SIZE_BYTES, SEEK_SET);

		uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn_to_eject]);

		for(int i = 0; i < MM_PAGE_SIZE_BYTES; i++)
			fprintf(swap_file, "%c", mem[i]);
	}

	phys_pages[ppn_to_eject].pid = -1;
	phys_pages[ppn_to_eject].vpn = -1;
	phys_pages[ppn_to_eject].valid = 0;
	phys_pages[ppn_to_eject].is_page_table = 0;

	return ppn_to_eject;
}

int reserve_ppn(int reserving_pid) {
	for(int i = 0; i < MM_PHYSICAL_PAGES; i++)
		if(!phys_pages[i].valid)
			return i;

	if(!swap_enabled) {
		DEBUG("pages full and swap disabled\n");
		return -1;
	} else {
		return eject_phys_page(reserving_pid);
	}
}

int create_page_table(int pid) {
	struct process *const proc = &processes[pid];

	if(proc == NULL) {
		DEBUG("process is NULL when creating page table\n");
		return -1;
	}

	int ppn = reserve_ppn(pid);

	if(ppn == -1) {
		DEBUG("unable to reserve PPN when creating page table\n");
		return -1;
	}

	uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn]);

	proc->page_table = (struct page_table_entry*)mem;
	proc->page_table_exists = 1;
	proc->page_table_resident = 1;

	for(int i = 0; i < MM_NUM_PTES; i++) {
		struct page_table_entry new_pte;
		new_pte.ppn = 0;
		new_pte.valid = 1;
		new_pte.writeable = 0;
		new_pte.present = 0;
		new_pte.dirty = 0;
		new_pte.accesses = 0;

		// TODO: Error checking in the future
		// TODO: No out of bounds checking on the mem + i, so that's pretty bad
		write_pte_to_mem(&new_pte, (uint8_t*)(mem + i));
	}

	phys_pages[ppn].pid = pid;
	phys_pages[ppn].vpn = -1;
	phys_pages[ppn].valid = 1;
	phys_pages[ppn].is_page_table = 1;

	return 0;
}

int load_page(struct page_table_entry *pte, int pid, int vpn) {
	if(!pte->valid) {
		DEBUG("attempted to load invalid PTE\n");
		return -1;
	}

	if(phys_pages[pte->ppn].valid && !swap_enabled) {
		DEBUG("attempted to load page into valid physical page with swap disabled\n");
		return -1;
	}

	if(pte->present) {
		DEBUG("PTE is already present in memory\n");
		return 0;
	}

	if(vpn == -1) {
		DEBUG("attempted to load from an invalid VPN\n");
		return -1;
	}

	if(swap_enabled) {
		if(phys_pages[pte->ppn].valid) {
			DEBUG("attempted to load page into valid physical page with swap enabled\n");
			return -1;
		}

		struct process *const proc = &processes[pid];
	
		// TODO: Maybe add file read/write helper functions
		FILE *swap_file = proc->swap_file;
		fseek(swap_file, vpn * MM_PAGE_SIZE_BYTES, SEEK_SET);

		uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[pte->ppn]);
		for(int i = 0; i < MM_PAGE_SIZE_BYTES; i++) {
			int c = fgetc(swap_file);

			if(c == EOF)
				c = 0;

			mem[i] = (uint8_t)c;
		}
	} 

	phys_pages[pte->ppn].pid = pid;
	phys_pages[pte->ppn].valid = 1;
	phys_pages[pte->ppn].is_page_table = 0;
	phys_pages[pte->ppn].vpn = vpn;

	pte->present = 1;
	pte->dirty = 0;
	pte->accesses = 0;

	return 0;
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

int load_page_table(int pid) {
	struct process *const proc = &processes[pid];
	
	if(proc == NULL) {
		DEBUG("process is NULL when loading page table\n");
		return -1;
	}

	if(proc->page_table_exists) {
		DEBUG("attempting to load a page table that does not exist\n");
		return -1;
	}

	if(proc->page_table_resident) {
		DEBUG("page table already resident in memory\n");
		return 0;
	}

	int ppn = reserve_ppn(pid);

	if(ppn == -1) {
		DEBUG("unable to reserve PPN when loading page table\n");
		return -1;
	}

	FILE *swap_file = proc->swap_file;
	fseek(swap_file, MM_NUM_PTES * MM_PAGE_SIZE_BYTES, SEEK_SET);

	uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn]);
	for(int i = 0; i < MM_PAGE_SIZE_BYTES; i++) {
		int c = fgetc(swap_file);

		if(c == EOF)
			c = 0;

		mem[i] = (uint8_t)c;
	}

	proc->page_table_resident = 1;

	phys_pages[ppn].pid = pid;
	phys_pages[ppn].vpn = -1;
	phys_pages[ppn].valid = 1;
	phys_pages[ppn].is_page_table = 1;

	return 0;
}

struct MM_MapResult MM_Map(int pid, uint32_t address, int writeable) {	
	CHECK(sizeof(struct page_table_entry) <= MM_MAX_PTE_SIZE_BYTES);

	struct MM_MapResult ret = {0};
	static char message[128];

	int err;
	if((err = check_mem_info(pid, address, message))) {
		ret.error = err;
		return ret;
	}
	
	ret.message = message;

	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	struct process *const proc = &processes[pid];

	if(!proc->page_table_exists) {
		if(create_page_table(pid)) {
			sprintf(message, "unable to create page table");
			ret.error = 1;

			return ret;
		}
	}

	// TODO: Implement load_page_table
	if(!proc->page_table_resident) {
		if(load_page_table(pid)) {
			sprintf(message, "unable to load page table");
			ret.error = 1;

			return ret;
		}
	}

	struct page_table_entry *pte = &(proc->page_table[vpn]);

	if(!pte->valid) {
		sprintf(message, "attempted to map invalid PTE");
		ret.error = -1;
		
		return ret;
	}

	if(!pte->present) {
		int ppn = reserve_ppn(pid);

		if(ppn == -1) {
			sprintf(message, "unable to reserve PPN for mapped PTE");
			ret.error = -1;
			
			return ret;
		}

		pte->ppn = ppn;

		// TODO: Error checking
		if(load_page(pte, pid, vpn)) {
			sprintf(message, "unable to load page");
			ret.error = -1;
			
			return ret;
		}
	}

	pte->writeable = writeable;
	pte->accesses = pte->accesses < 3 ? pte->accesses + 1 : 3;

	sprintf(message, "success");
	ret.error = 0;

	return ret;
}

int MM_LoadByte(int pid, uint32_t address, uint8_t *value) {
	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	struct process *const proc = &processes[pid];

	// TODO: Make sure all errors are in the past tense throughout this entire file
	// TODO: Also try to standardize all errors
	if(!proc->page_table_exists) {
		DEBUG("attempted to read from a page table that does not exist\n");
		return -1;
	}

	// TODO: Implement load_page_table
	if(!proc->page_table_resident) {
		if(load_page_table(pid)) {
			DEBUG("unable to load page table when attempting to read data\n");
			return -1;
		}
	}

	// Use vpn as index to find PTE for this page
	struct page_table_entry *pte = &proc->page_table[vpn];

	if(!pte->valid) {
		DEBUG("attempting to read from invalid PTE\n");
		return -1;
	}

	if(!pte->present) {
		int ppn = reserve_ppn(pid);

		if(ppn == -1) {
			DEBUG("unable to reserve PPN to read data\n");
			
			return -1;
		}

		pte->ppn = ppn;
		
		// TODO: Error checking
		if(load_page(pte, pid, vpn)) {
			DEBUG("unable to load page to read data\n");
			return -1;
		}
	}

	// TODO: A lot of this could probably go in a helper function
	if(phys_pages[pte->ppn].pid != pid) {
		DEBUG("phys page and load call PID's do not match when reading\n");
		return -1;
	}

	if(phys_pages[pte->ppn].vpn != vpn) {
		DEBUG("phys page and load call VPN's do not match when reading\n");
		return -1;
	}

	if(!phys_pages[pte->ppn].valid) {
		DEBUG("attempting to read from invalid phys page\n");
		return -1;
	}

	if(phys_pages[pte->ppn].is_page_table) {
		DEBUG("attempting to read memory from a page table\n");
		return -1;
	}

	// Phyical pointer reassembled from PPN and offset
	uint32_t physical_address = ((uint32_t)pte->ppn << MM_PAGE_SIZE_BITS) | offset;

	// Now we can get values from physical memory
	*value = phys_mem[physical_address];

	// DEBUG("Read:\n PID: %d\n VPN: %d\n PPN: %d\n Val: %x\n", pid, vpn, pte->ppn, *value);

	// TODO: Check for the following errors (should be helper function):
		// pid out of range, complain
		// address out of range, complain
		// offset out of range, complain
		// physical page is invalid, complain

	return 0;
}

int MM_StoreByte(int pid, uint32_t address, uint8_t value) {
	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	struct process *const proc = &processes[pid];

	// TODO: Make sure all errors are in the past tense throughout this entire file
	// TODO: Also try to standardize all errors
	if(!proc->page_table_exists) {
		DEBUG("attempted to write to a page table that does not exist\n");
		return -1;
	}

	// TODO: Implement load_page_table
	if(!proc->page_table_resident) {
		if(load_page_table(pid)) {
			DEBUG("unable to load page table when attempting to read data\n");
			return -1;
		}
	}

	// Use vpn as index to find PTE for this page
	struct page_table_entry *pte = &proc->page_table[vpn];

	if(!pte->valid) {
		DEBUG("attempting to write to invalid PTE\n");
		return -1;
	}

	if(!pte->writeable) {
		DEBUG("attempting to write to a read only PTE\n");
		return -1;
	}

	if(!pte->present) {
		int ppn = reserve_ppn(pid);

		if(ppn == -1) {
			DEBUG("unable to reserve PPN to write data\n");
			
			return -1;
		}

		pte->ppn = ppn;
		
		// TODO: Error checking
		if(load_page(pte, pid, vpn)) {
			DEBUG("unable to load page to write data\n");
			return -1;
		}
	}

	// TODO: A lot of this could probably go in a helper function
	if(phys_pages[pte->ppn].pid != pid) {
		DEBUG("phys page and load call PID's do not match when writing\n");
		return -1;
	}

	if(phys_pages[pte->ppn].vpn != vpn) {
		DEBUG("phys page and load call VPN's do not match when writing\n");
		return -1;
	}

	if(!phys_pages[pte->ppn].valid) {
		DEBUG("attempting to write to invalid phys page\n");
		return -1;
	}

	if(phys_pages[pte->ppn].is_page_table) {
		DEBUG("attempting to write memory to a page table\n");
		return -1;
	}

	// // Phyical pointer reassembled from PPN and offset
	uint32_t physical_address = ((uint32_t)pte->ppn << MM_PAGE_SIZE_BITS) | offset;

	phys_mem[physical_address] = value;

	pte->dirty = 1;

	// DEBUG("Write:\n PID: %d\n VPN: %d\n PPN: %d\n Val: %x\n", pid, vpn, pte->ppn, value);

	return 0;
}