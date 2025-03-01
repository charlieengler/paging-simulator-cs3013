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

// A simple function to print a PTE for debugging purposes
void print_pte(struct page_table_entry *pte) {
	DEBUG("PTE:\n");
	DEBUG("		PPN: 		%d\n", pte->ppn);
	DEBUG("		valid: 		%d\n", pte->valid);
	DEBUG("		writeable: 	%d\n", pte->writeable);
	DEBUG("		present: 	%d\n", pte->present);
	DEBUG("		dirty: 		%d\n", pte->dirty);
	DEBUG("		accesses: 	%d\n", pte->accesses);
}

// I found through testing that sometimes is was unreliable to directly write the pte to memory,
// so I made this helper. That being said, I never ended up using it
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
	struct page_table_entry *page_table;
};

// Simple helper used to print process attributes for debugging
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
	int pid; // I found it useful to store the PID in the page entry
	int vpn; // ...and the VPN of the installed page
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

		// Create swap files for each process and initialize the processes to default values
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

// Ejects a physical page taking in a PID that the new process will be saved to
int eject_phys_page(int reserving_pid) {
	int ppn_to_eject = -1;
	// Scans the physical pages looking for candidates to eject
	// TODO: You have a number of accesses saved in the PTE, so you should probably use it to optimize
	for(int i = 0; i < MM_PHYSICAL_PAGES; i++) {
		// An ideal candidate is a data table that isn't part of the current process
		if(phys_pages[i].pid != reserving_pid && !phys_pages[i].is_page_table) {
			ppn_to_eject = i;
			break;
		}

		// A good secondary candidate is a data table from the current process
		if(!phys_pages[i].is_page_table)
			ppn_to_eject = i;
	}

	// If none of these conditions could be met (all pages in memory are full of page tables),
	// then we pick the first page table that isn't from the current process
	if(ppn_to_eject == -1) {
		for(int i = 0; i < MM_PHYSICAL_PAGES; i++) {
			if(phys_pages[i].pid != reserving_pid) {
				ppn_to_eject = i;
				break;
			}
		}
	}

	// We should never reach this, but it's always good to check in case there's a bug
	if(ppn_to_eject == -1) {
		DEBUG("couldn't eject a page (shouldn't get here)\n");
		return -1;
	}

	// The process we're interested in is the one attached to the page we chose to eject
	struct process *const proc = &processes[phys_pages[ppn_to_eject].pid];

	// We set the VPN to the default for page tables to start
	int vpn_to_eject = MM_NUM_PTES;

	// We also assume that the data is dirty by default because we don't check if page tables
	// are dirty (at least not in this version)
	int is_dirty = 1;

	// If the page isn't a page table, then we eject it by its PTE
	if(!phys_pages[ppn_to_eject].is_page_table) {
		vpn_to_eject = phys_pages[ppn_to_eject].vpn;

		// This involves resetting the PTE to its unallocated values while preserving its
		// dirty bit to check if it needs to be saved to the swap file
		struct page_table_entry *pte_to_eject = &(proc->page_table[vpn_to_eject]);
		pte_to_eject->ppn = 0;
		pte_to_eject->present = 0;
		is_dirty = pte_to_eject->dirty;
		pte_to_eject->dirty = 0;
		pte_to_eject->accesses = 0;
	} else {
		// If it's a page table, we're really only interested in resetting the resident flag
		proc->page_table_resident = 0;
	}

	// If the data is dirty (page tables should always be dirty), then we eject the data into the
	// swap file for the relevant process
	if(is_dirty) {
		FILE *swap_file = proc->swap_file;
		// We seek the beginning of the block holding the data we're interested in
		fseek(swap_file, vpn_to_eject * MM_PAGE_SIZE_BYTES, SEEK_SET);

		// We get the pointer to the memory holding the data we wish to eject
		uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn_to_eject]);

		// Then we write the data from memory into the swap file while subsequently zeroing out
		// the data
		for(int i = 0; i < MM_PAGE_SIZE_BYTES; i++) {
			fputc(mem[i], swap_file);
			mem[i] = 0;
		}
	}

	// We have to reset the physical page flags, but their defaults are the same between
	// page table and data table ejection
	phys_pages[ppn_to_eject].pid = -1;
	phys_pages[ppn_to_eject].vpn = -1;
	phys_pages[ppn_to_eject].valid = 0;
	phys_pages[ppn_to_eject].is_page_table = 0;

	// Finally, we return the PPN that we chose to eject
	return ppn_to_eject;
}

// Reserves a PPN to make space in physical memory for a new page given a PID
int reserve_ppn(int reserving_pid) {
	// Ideally, we find a page that's empty (invalid) and we can just use it
	for(int i = 0; i < MM_PHYSICAL_PAGES; i++)
		if(!phys_pages[i].valid)
			return i;

	if(!swap_enabled) {
		// If we don't find an empty page, and swap is disabled, then we get an error
		DEBUG("pages full and swap disabled\n");
		return -1;
	} else {
		// Otherwise, we return the best PPN found by ejecting a page
		return eject_phys_page(reserving_pid);
	}
}

// Initializes a page table for a given process identified by its PID
int create_page_table(int pid) {
	struct process *const proc = &processes[pid];

	if(proc == NULL) {
		DEBUG("process is NULL when creating page table\n");
		return -1;
	}

	// We reserve a PPN to use for this page table
	int ppn = reserve_ppn(pid);

	// We throw an error if the PPN is -1 because we cannot continue from here
	if(ppn == -1) {
		DEBUG("unable to reserve PPN when creating page table\n");
		return -1;
	}

	// A pointer to the memory located within the newly allocated physical page is acquired
	uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn]);

	// The page table pointer and flags within the process are set
	proc->page_table = (struct page_table_entry*)mem;
	proc->page_table_exists = 1;
	proc->page_table_resident = 1;

	// Just to be safe, the PTE's are all initialized as valid, but not yet accessible
	for(int i = 0; i < MM_NUM_PTES; i++) {
		struct page_table_entry new_pte;
		new_pte.ppn = 0;
		new_pte.valid = 1;
		new_pte.writeable = 0;
		new_pte.present = 0;
		new_pte.dirty = 0;
		new_pte.accesses = 0;

		// No out of bounds checking on the mem + i, so that's pretty bad
		*(struct page_table_entry*)(mem + i) = new_pte;
	}

	// The physical page flags need to be set for a page table as well. This involves everything
	// apart from the VPN, which is unused in PTE's
	phys_pages[ppn].pid = pid;
	phys_pages[ppn].vpn = -1;
	phys_pages[ppn].valid = 1;
	phys_pages[ppn].is_page_table = 1;

	return 0;
}

// Loads data pages into memory from a swap file taking in the PTE of the entry, PID, and VPN
int load_page(struct page_table_entry *pte, int pid, int vpn) {
	// The PTE has to be valid to load the page
	if(!pte->valid) {
		DEBUG("attempted to load invalid PTE\n");
		return -1;
	}

	// The physical page can't be valid to load the page
	if(phys_pages[pte->ppn].valid) {
		DEBUG("attempted to load page into valid physical page\n");
		return -1;
	}

	// If the PTE is already present, then we have nothing to do
	if(pte->present) {
		DEBUG("PTE is already present in memory\n");
		return 0;
	}

	// If the VPN is negative, then what are we even loading in?
	if(vpn == -1) {
		DEBUG("attempted to load from an invalid VPN\n");
		return -1;
	}

	// You can only load a swap file if swap is enabled. This function is also used to initialize
	// a physical page when swap is enabled or disabled
	if(swap_enabled) {
		struct process *const proc = &processes[pid];
	
		// TODO: Maybe add file read/write helper functions
		FILE *swap_file = proc->swap_file;
		// We navigate to the VPN entry within the swap file...
		fseek(swap_file, vpn * MM_PAGE_SIZE_BYTES, SEEK_SET);

		// ...get the pointer to its memory...
		uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[pte->ppn]);
		for(int i = 0; i < MM_PAGE_SIZE_BYTES; i++) {
			int c = fgetc(swap_file);

			if(c == EOF)
				c = 0;

			// ...and read it into physical memory while zeroing out end of file codes as they should
			// be considered zeroes, but they load in as EOF's cause that's what an EOF is
			mem[i] = (uint8_t)c;
		}
	}

	// The physical page flags are set for a data table
	phys_pages[pte->ppn].pid = pid;
	phys_pages[pte->ppn].valid = 1;
	phys_pages[pte->ppn].is_page_table = 0;
	phys_pages[pte->ppn].vpn = vpn;

	// The PTE flags are set to show that the data has been loaded and is fresh
	pte->present = 1;
	pte->dirty = 0;
	pte->accesses = 0;

	return 0;
}

// A simple helper used to check simple memory info and throw an error back out to the MM_Map message
// if one is found
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

// Similar to loading a data page, but instead it's a page table
// TODO: This can almost certainly be combined with the other load function somehow
int load_page_table(int pid) {
	struct process *const proc = &processes[pid];
	
	// Can't load if there's no process to load for
	if(proc == NULL) {
		DEBUG("process is NULL when loading page table\n");
		return -1;
	}

	// Also can't load if the page table doesn't exist
	if(!proc->page_table_exists) {
		DEBUG("attempting to load a page table that does not exist\n");
		return -1;
	}

	// If the page table is already resident in memory, then we have nothing to do
	if(proc->page_table_resident) {
		DEBUG("page table already resident in memory\n");
		return 0;
	}

	// Just like a data page, we need to reserve a PPN
	int ppn = reserve_ppn(pid);

	// Can't continue if the PPN is invalid
	if(ppn == -1) {
		DEBUG("unable to reserve PPN when loading page table\n");
		return -1;
	}

	FILE *swap_file = proc->swap_file;
	// Loads in the swap file from the page table entry. This is all the way at the end of the file
	fseek(swap_file, MM_NUM_PTES * MM_PAGE_SIZE_BYTES, SEEK_SET);

	// A pointer to the memory we want to load into is acquired
	uint8_t *mem = (uint8_t*)phys_mem_addr_for_phys_page_entry(&phys_pages[ppn]);
	for(int i = 0; i < MM_PAGE_SIZE_BYTES; i++) {
		int c = fgetc(swap_file);

		if(c == EOF)
			c = 0;

		// The content of the swap file is read into physical memory, once again zeroing out EOF's
		mem[i] = (uint8_t)c;
	}

	// We need to set the process' page table as resident
	proc->page_table_resident = 1;

	// The flags of the physical page also need to be set accordingly for a page table
	phys_pages[ppn].pid = pid;
	phys_pages[ppn].vpn = -1;
	phys_pages[ppn].valid = 1;
	phys_pages[ppn].is_page_table = 1;

	return 0;
}

// Maps a virtual address to a physical address (I only use this to initilize data and only really
// map through helpers)
struct MM_MapResult MM_Map(int pid, uint32_t address, int writeable) {	
	CHECK(sizeof(struct page_table_entry) <= MM_MAX_PTE_SIZE_BYTES);

	struct MM_MapResult ret = {0};
	static char message[128];

	ret.message = message;

	// Error out if the memory info is invalid
	int err;
	if((err = check_mem_info(pid, address, message))) {
		ret.error = err;
		return ret;
	}

	// The VPN and offset of the data are extracted from the virtual address
	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	struct process *const proc = &processes[pid];

	// If the page table doesn't exist on the process, then we need to create it
	if(!proc->page_table_exists) {
		if(create_page_table(pid)) {
			sprintf(message, "unable to create page table");
			ret.error = 1;

			return ret;
		}
	}

	// If the page table exists, but isn't resident in memory, then we just need to load it
	if(!proc->page_table_resident) {
		if(load_page_table(pid)) {
			sprintf(message, "unable to load page table");
			ret.error = 1;

			return ret;
		}
	}

	// The appropriate PTE can now be found
	struct page_table_entry *pte = &(proc->page_table[vpn]);

	// We can't map it if it isn't valid, though
	if(!pte->valid) {
		sprintf(message, "attempted to map invalid PTE");
		ret.error = -1;
		
		return ret;
	}

	// If the PTE isn't present in memory, then we need to load it in
	// TODO: Code passes almost all tests if this is commented out lol (because this map function is
	// lowkey useless in my implementation besides being used to initialize data and set permissions)
	if(!pte->present) {
		// We reserve a PPN to load into
		int ppn = reserve_ppn(pid);

		if(ppn == -1) {
			sprintf(message, "unable to reserve PPN for mapped PTE");
			ret.error = -1;
			
			return ret;
		}

		// The PPN of the PTE is set with the acquired PPN
		pte->ppn = ppn;

		// The page is then loaded into physical memory (if swap is disabled, then this just initializes it)
		if(load_page(pte, pid, vpn)) {
			sprintf(message, "unable to load page");
			ret.error = -1;
			
			return ret;
		}
	}

	// The PTE flags, including the number of times it was accessed are set to be used for writing and
	// choosing a candidate for swapping
	pte->writeable = writeable;
	pte->accesses = pte->accesses < 3 ? pte->accesses + 1 : 3;

	sprintf(message, "success");
	ret.error = 0;

	return ret;
}

// Loads data from a virtual memory address
int MM_LoadByte(int pid, uint32_t address, uint8_t *value) {
	// The VPN and offset of the data are extracted from the virtual address
	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	struct process *const proc = &processes[pid];

	// The page table must exist in order to load data from it
	// TODO: Make sure all errors are in the past tense throughout this entire file
	// TODO: Also try to standardize all errors
	if(!proc->page_table_exists) {
		DEBUG("attempted to read from a page table that does not exist\n");
		return -1;
	}

	// If it isn't resident, then we can just simply load it
	// TODO: Implement load_page_table
	if(!proc->page_table_resident) {
		if(load_page_table(pid)) {
			DEBUG("unable to load page table when attempting to read data\n");
			return -1;
		}
	}

	// Use VPN as index to find PTE for this page
	struct page_table_entry *pte = &proc->page_table[vpn];

	// The PTE must be valid to read data from it
	if(!pte->valid) {
		DEBUG("attempting to read from invalid PTE\n");
		return -1;
	}

	// If the PTE isn't present in physical memory, then it needs to be loaded in
	if(!pte->present) {
		// A PPN is reserved
		int ppn = reserve_ppn(pid);

		if(ppn == -1) {
			DEBUG("unable to reserve PPN to read data\n");
			
			return -1;
		}

		// The PTE's PPN is assigned to this reserved PPN
		pte->ppn = ppn;
		
		// The data is loaded into the page
		if(load_page(pte, pid, vpn)) {
			DEBUG("unable to load page to read data\n");
			return -1;
		}
	}

	// TODO: A lot of this could probably go in a helper function
	// The PID's of the physical page and function call need to match
	if(phys_pages[pte->ppn].pid != pid) {
		DEBUG("phys page and load call PID's do not match when reading\n");
		return -1;
	}

	// So do the VPN's
	if(phys_pages[pte->ppn].vpn != vpn) {
		DEBUG("phys page and load call VPN's do not match when reading\n");
		return -1;
	}

	// The physical page must be valid to read data from it
	if(!phys_pages[pte->ppn].valid) {
		DEBUG("attempting to read from invalid phys page\n");
		return -1;
	}

	// We cannot read memory from a page table as this makes it feel uncomfortable
	if(phys_pages[pte->ppn].is_page_table) {
		DEBUG("attempting to read memory from a page table\n");
		return -1;
	}

	// Phyical pointer reassembled from PPN and offset
	uint32_t physical_address = ((uint32_t)pte->ppn << MM_PAGE_SIZE_BITS) | offset;

	// Now we can get values from physical memory
	*value = phys_mem[physical_address];

	return 0;
}

// Stores data into a virtual memory address
int MM_StoreByte(int pid, uint32_t address, uint8_t value) {
	// The VPN and offset are extracted from the virtual address
	uint8_t vpn = (uint8_t)(address >> MM_PAGE_SIZE_BITS);
	uint8_t offset = (uint8_t)(address & MM_PAGE_OFFSET_MASK);

	struct process *const proc = &processes[pid];

	// The page table must exist on the process in order to write to it
	// TODO: Make sure all errors are in the past tense throughout this entire file
	// TODO: Also try to standardize all errors
	if(!proc->page_table_exists) {
		DEBUG("attempted to write to a page table that does not exist\n");
		return -1;
	}

	// If the page table isn't resident, then it can simply be loaded
	if(!proc->page_table_resident) {
		if(load_page_table(pid)) {
			DEBUG("unable to load page table when attempting to read data\n");
			return -1;
		}
	}

	// Use vpn as index to find PTE for this page
	struct page_table_entry *pte = &proc->page_table[vpn];

	// You can't write to an invalid PTE
	if(!pte->valid) {
		DEBUG("attempting to write to invalid PTE\n");
		return -1;
	}

	// You can't write to the PTE if it isn't writeable
	if(!pte->writeable) {
		DEBUG("attempting to write to a read only PTE\n");
		return -1;
	}

	// If the PTE isn't present, then it must be loaded before it can be written to
	if(!pte->present) {
		// A PPN is reserved
		int ppn = reserve_ppn(pid);

		if(ppn == -1) {
			DEBUG("unable to reserve PPN to write data\n");
			
			return -1;
		}

		// The PTE's PPN is set to the reserved PPN
		pte->ppn = ppn;

		// The page is then loaded into physical memory
		if(load_page(pte, pid, vpn)) {
			DEBUG("unable to load page to write data\n");
			return -1;
		}
	}

	// TODO: A lot of this could probably go in a helper function
	// The PID's must match between physical memory and the function call
	if(phys_pages[pte->ppn].pid != pid) {
		DEBUG("phys page and load call PID's do not match when writing\n");
		return -1;
	}

	// So do the VPN's
	if(phys_pages[pte->ppn].vpn != vpn) {
		DEBUG("phys page and load call VPN's do not match when writing\n");
		return -1;
	}

	// The physical page must be valid to write to it
	if(!phys_pages[pte->ppn].valid) {
		DEBUG("attempting to write to invalid phys page\n");
		return -1;
	}

	// Writing to a page table is highly unethical as it causes irreparable damage and requires
	// years of emotionally and financially taxxing rehabilitation to repare.
	if(phys_pages[pte->ppn].is_page_table) {
		DEBUG("attempting to write memory to a page table\n");
		return -1;
	}

	// Phyical pointer reassembled from PPN and offset
	uint32_t physical_address = ((uint32_t)pte->ppn << MM_PAGE_SIZE_BITS) | offset;

	// The value at the physical address is set
	phys_mem[physical_address] = value;

	// Finally, the PTE is marked as dirty so it gets ejected properly
	pte->dirty = 1;

	return 0;
}