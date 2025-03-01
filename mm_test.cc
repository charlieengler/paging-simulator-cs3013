
#include <iostream>

#include <string>
#include <map>
#include <tuple>
#include <vector>
#include <functional>
#include <thread>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mm_api.h"

#define FAIL_IF(x)		do { if ((x)) { std::cout << __FILE__ << ":" << __LINE__ << ": " << #x << std::endl; exit(1); } } while(0)
#define FAIL_UNLESS_EQ(x, y)	do { auto xval = (x); auto yval = (y); if ((xval) != (yval)) { std::cout << __FILE__ << ":" << __LINE__ << ": " << #x << " (" << ((uint32_t)xval) << ") != " << #y << " (" << ((uint32_t)yval) << ")" << std::endl; exit(1); } } while(0)
#define FAIL_UNLESS_NE(x, y)	do { auto xval = (x); auto yval = (y); if ((xval) == (yval)) { std::cout << __FILE__ << ":" << __LINE__ << ": " << #x << " (" << ((uint32_t)xval) << ") == " << #y << " (" << ((uint32_t)yval) << ")" << std::endl; exit(1); } } while(0)

struct test {
	std::string name;
	int points;
	std::function<bool(void)> runtest;
};

struct section_tests {
	std::string name;
	std::vector<test> tests;
};

static void print_mapresult(const struct MM_MapResult &mr) {
	if (mr.error) {
		std::cout << "error: " << mr.message << std::endl;
	}
}

uint32_t addrN(int page, int offset = 0) { return page * MM_PAGE_SIZE_BYTES + offset; }
const int step = std::max(1, MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES / (32 * 1024));

static std::vector<section_tests> section_tests = {
	{
		.name = "Section 1: (20 pts) Correct implementation of the map, load, and store commands.",
		.tests = {
			{
				.name = "Map only, r/o",
				.points = 1,
				.runtest = [](){
					// map the first page read only
					struct MM_MapResult mr = MM_Map(0, 0, 0);
					print_mapresult(mr);
					FAIL_UNLESS_EQ(mr.error, 0);
					return true;
				},
			},
			{
				.name = "Map only, r/w",
				.points = 1,
				.runtest = [](){
					// map the first page read only
					struct MM_MapResult mr = MM_Map(0, 0, 1);
					print_mapresult(mr);
					FAIL_UNLESS_EQ(mr.error, 0);
					return true;
				},
			},
			{
				.name = "Map 3 pages pid0 r/o",
				.points = 1,
				.runtest = [](){
					// map the first page read only
					struct MM_MapResult mr1 = MM_Map(0, addrN(0), 0);
					print_mapresult(mr1);
					struct MM_MapResult mr2 = MM_Map(0, addrN(1), 0);
					print_mapresult(mr2);
					struct MM_MapResult mr3 = MM_Map(0, addrN(2), 0);
					print_mapresult(mr3);
					FAIL_UNLESS_EQ(mr1.error, 0);
					FAIL_UNLESS_EQ(mr2.error, 0);
					FAIL_UNLESS_EQ(mr3.error, 0);
					return true;
				},
			},
			{
				.name = "Map 3 pages pid0 r/w",
				.points = 1,
				.runtest = [](){
					// map the first page read only
					struct MM_MapResult mr1 = MM_Map(0, addrN(3), 1);
					print_mapresult(mr1);
					struct MM_MapResult mr2 = MM_Map(0, addrN(5), 1);
					print_mapresult(mr2);
					struct MM_MapResult mr3 = MM_Map(0, addrN(7), 1);
					print_mapresult(mr3);
					FAIL_UNLESS_EQ(mr1.error, 0);
					FAIL_UNLESS_EQ(mr2.error, 0);
					FAIL_UNLESS_EQ(mr3.error, 0);
					return true;
				},
			},
			{
				.name = "Map 4 pages, 4th should fail",
				.points = 1,
				.runtest = [](){
					// map the first page read only
					struct MM_MapResult mr1 = MM_Map(0, addrN(0), 0);
					print_mapresult(mr1);
					struct MM_MapResult mr2 = MM_Map(0, addrN(1), 1);
					print_mapresult(mr2);
					struct MM_MapResult mr3 = MM_Map(0, addrN(2), 0);
					print_mapresult(mr3);
					struct MM_MapResult mr4 = MM_Map(0, addrN(3), 1);
					print_mapresult(mr4);
					FAIL_UNLESS_EQ(mr1.error, 0);
					FAIL_UNLESS_EQ(mr2.error, 0);
					FAIL_UNLESS_EQ(mr3.error, 0);
					FAIL_UNLESS_NE(mr4.error, 0);
					return true;
				},
			},
			{
				.name = "Map a page and load from it",
				.points = 1,
				.runtest = [](){
					// map the first page read only
					struct MM_MapResult mr1 = MM_Map(0, addrN(2), 0);
					print_mapresult(mr1);
					uint8_t value;
					FAIL_IF(MM_LoadByte(0, addrN(2,2), &value) != 0);
					return true;
				},
			},
			{
				.name = "Map a page and write to it",
				.points = 1,
				.runtest = [](){
					struct MM_MapResult mr1 = MM_Map(0, addrN(2,0), 1);
					print_mapresult(mr1);
					uint8_t value = 0xaa;
					FAIL_IF(MM_StoreByte(0, addrN(2,2), value) != 0);
					return true;
				},
			},
			{
				.name = "Invalid Map address should fail",
				.points = 1,
				.runtest = [](){
					struct MM_MapResult mr1 = MM_Map(0, 0xFFF000, 1);
					print_mapresult(mr1);
					FAIL_IF(!mr1.error);
					return true;
				},
			},
			{
				.name = "Read from an unmapped page should fail",
				.points = 1,
				.runtest = [](){
					uint8_t value = 0xaa;
					FAIL_IF(MM_LoadByte(0, addrN(2,2), &value) == 0);
					return true;
				},
			},
			{
				.name = "Write to unmapped page should fail",
				.points = 1,
				.runtest = [](){
					uint8_t value = 0xaa;
					FAIL_IF(MM_StoreByte(0, addrN(2,2), value) == 0);
					return true;
				},
			},
			{
				.name = "Write to a read-only page should fail",
				.points = 1,
				.runtest = [](){
					struct MM_MapResult mr1 = MM_Map(0, addrN(2,0), 0);
					print_mapresult(mr1);
					uint8_t value = 0xaa;
					FAIL_IF(MM_StoreByte(0, addrN(2,2), value) == 0);
					return true;
				},
			},
			{
				.name = "Write to a read-write page should retain value",
				.points = 1,
				.runtest = [](){
					struct MM_MapResult mr1 = MM_Map(0, addrN(2,0), 1);
					print_mapresult(mr1);
					uint8_t value = 0xaa;
					int rc = MM_StoreByte(0, addrN(2,2), value);
					FAIL_IF(rc != 0);
					value = 0;
					FAIL_IF(MM_LoadByte(0, addrN(2,2), &value) != 0);
					FAIL_UNLESS_EQ(value, 0xaa);
					return true;
				},
			},
			{
				.name = "Remap a read-only page to read-write should fail then succeed",
				.points = 2,
				.runtest = [](){
					struct MM_MapResult mr1 = MM_Map(0, addrN(2,0), 0);
					print_mapresult(mr1);
					uint8_t value = 0xaa;
					FAIL_IF(MM_StoreByte(0, addrN(2,2), value) == 0);
					struct MM_MapResult mr2 = MM_Map(0, addrN(2,0), 1);
					print_mapresult(mr2);
					FAIL_IF(MM_StoreByte(0, addrN(2,2), value) != 0);
					value = 0;
					FAIL_IF(MM_LoadByte(0, addrN(2,2), &value) != 0);
					FAIL_UNLESS_EQ(value, 0xaa);
					return true;
				},
			},
			{
				.name = "Writing every byte in a page should retain values",
				.points = 2,
				.runtest = [](){
					struct MM_MapResult mr1 = MM_Map(0, addrN(2,0), 1);
					print_mapresult(mr1);
					std::vector<uint8_t> writes;
					for (int offset = 0; offset < MM_PAGE_SIZE_BYTES; offset++) {
						uint8_t value = rand() % 256;
						FAIL_IF(MM_StoreByte(0, addrN(2,offset), value) != 0);
						writes.push_back(value);
					}
					for (int offset = 0; offset < MM_PAGE_SIZE_BYTES; offset++) {
						uint8_t value;
						FAIL_IF(MM_LoadByte(0, addrN(2,offset), &value) != 0);
						FAIL_UNLESS_EQ(value, writes[offset]);
					}
					return true;
				},
			},
			{
				.name = "Values written to 3 different pages should retain value",
				.points = 2,
				.runtest = [](){
					struct MM_MapResult mr1 = MM_Map(0, addrN(2,0), 1);
					print_mapresult(mr1);
					struct MM_MapResult mr2 = MM_Map(0, addrN(5,0), 1);
					print_mapresult(mr2);
					struct MM_MapResult mr3 = MM_Map(0, addrN(3,0), 1);
					print_mapresult(mr3);
					const uint8_t value1 = 0xab;
					const uint8_t value2 = 0xcd;
					const uint8_t value3 = 0xef;
					FAIL_IF(MM_StoreByte(0, addrN(2,2), value1) != 0);
					FAIL_IF(MM_StoreByte(0, addrN(5,2), value2) != 0);
					FAIL_IF(MM_StoreByte(0, addrN(3,2), value3) != 0);
					uint8_t value = 0;
					FAIL_IF(MM_LoadByte(0, addrN(2,2), &value) != 0);
					FAIL_UNLESS_EQ(value, value1);
					FAIL_IF(MM_LoadByte(0, addrN(5,2), &value) != 0);
					FAIL_UNLESS_EQ(value, value2);
					FAIL_IF(MM_LoadByte(0, addrN(3,2), &value) != 0);
					FAIL_UNLESS_EQ(value, value3);
					return true;
				},
			},
			{
				.name = "Writes across 2 contiguous pages should retain values",
				.points = 2,
				.runtest = [](){
					struct MM_MapResult mr1 = MM_Map(0, addrN(3,0), 1);
					print_mapresult(mr1);
					struct MM_MapResult mr2 = MM_Map(0, addrN(4,0), 1);
					print_mapresult(mr2);
					std::vector<uint8_t> writes;
					for (int offset = 0; offset < MM_PAGE_SIZE_BYTES * 2; offset++) {
						uint8_t value = rand() % 256;
						FAIL_IF(MM_StoreByte(0, addrN(3,offset), value) != 0);
						writes.push_back(value);
					}
					for (int offset = 0; offset < MM_PAGE_SIZE_BYTES * 2; offset++) {
						uint8_t value;
						FAIL_IF(MM_LoadByte(0, addrN(3,offset), &value) != 0);
						FAIL_UNLESS_EQ(value, writes[offset]);
					}
					return true;
				},
			},
#if 0
			{
				.name = "sleep test",
				.points = 1,
				.runtest = [](){
					sleep(8);
					return true;
				},
			},
#endif
		},
	},
	{
		.name = "Section 2: (20 pts) Pages are correctly swapped to and from disk.",
		.tests = {
			{
				.name = "Writes and reads to 4 pages of a single pid should return correct values",
				.points = 8,
				.runtest = [](){
					MM_SwapOn();
					struct MM_MapResult mr1 = MM_Map(0, addrN(1,0), 1);
					print_mapresult(mr1);
					struct MM_MapResult mr2 = MM_Map(0, addrN(2,0), 1);
					print_mapresult(mr2);
					struct MM_MapResult mr3 = MM_Map(0, addrN(3,0), 1);
					print_mapresult(mr3);
					struct MM_MapResult mr4 = MM_Map(0, addrN(4,0), 1);
					print_mapresult(mr4);

					const uint8_t values[4] = {
						(uint8_t)(rand() % 256),
						(uint8_t)(rand() % 256),
						(uint8_t)(rand() % 256),
						(uint8_t)(rand() % 256),
					};
					FAIL_IF(MM_StoreByte(0, addrN(1,5), values[0]) != 0);
					FAIL_IF(MM_StoreByte(0, addrN(2,6), values[1]) != 0);
					FAIL_IF(MM_StoreByte(0, addrN(3,7), values[2]) != 0);
					FAIL_IF(MM_StoreByte(0, addrN(4,8), values[3]) != 0);

					uint8_t value;
					FAIL_IF(MM_LoadByte(0, addrN(1,5), &value) != 0);
					FAIL_UNLESS_EQ(value, values[0]);
					FAIL_IF(MM_LoadByte(0, addrN(2,6), &value) != 0);
					FAIL_UNLESS_EQ(value, values[1]);
					FAIL_IF(MM_LoadByte(0, addrN(3,7), &value) != 0);
					FAIL_UNLESS_EQ(value, values[2]);
					FAIL_IF(MM_LoadByte(0, addrN(4,8), &value) != 0);
					FAIL_UNLESS_EQ(value, values[3]);

					return true;
				},
			},
			{
				.name = "Writes and reads to 6 pages of a single pid should return correct values",
				.points = 4,
				.runtest = [](){
					MM_SwapOn();
					struct MM_MapResult mr1 = MM_Map(0, addrN(1,0), 1);
					print_mapresult(mr1);
					struct MM_MapResult mr2 = MM_Map(0, addrN(2,0), 1);
					print_mapresult(mr2);
					struct MM_MapResult mr3 = MM_Map(0, addrN(3,0), 1);
					print_mapresult(mr3);
					struct MM_MapResult mr4 = MM_Map(0, addrN(4,0), 1);
					print_mapresult(mr4);
					struct MM_MapResult mr5 = MM_Map(0, addrN(5,0), 1);
					print_mapresult(mr5);
					struct MM_MapResult mr6 = MM_Map(0, addrN(6,0), 1);
					print_mapresult(mr6);

					const uint8_t values[] = {
						(uint8_t)(rand() % 256),
						(uint8_t)(rand() % 256),
						(uint8_t)(rand() % 256),
						(uint8_t)(rand() % 256),
						(uint8_t)(rand() % 256),
						(uint8_t)(rand() % 256),
					};
					FAIL_IF(MM_StoreByte(0, addrN(1,5), values[0]) != 0);
					FAIL_IF(MM_StoreByte(0, addrN(2,6), values[1]) != 0);
					FAIL_IF(MM_StoreByte(0, addrN(3,7), values[2]) != 0);
					FAIL_IF(MM_StoreByte(0, addrN(4,8), values[3]) != 0);
					FAIL_IF(MM_StoreByte(0, addrN(5,9), values[4]) != 0);
					FAIL_IF(MM_StoreByte(0, addrN(6,10), values[5]) != 0);

					uint8_t value;
					FAIL_IF(MM_LoadByte(0, addrN(1,5), &value) != 0);
					FAIL_UNLESS_EQ(value, values[0]);
					FAIL_IF(MM_LoadByte(0, addrN(2,6), &value) != 0);
					FAIL_UNLESS_EQ(value, values[1]);
					FAIL_IF(MM_LoadByte(0, addrN(3,7), &value) != 0);
					FAIL_UNLESS_EQ(value, values[2]);
					FAIL_IF(MM_LoadByte(0, addrN(4,8), &value) != 0);
					FAIL_UNLESS_EQ(value, values[3]);
					FAIL_IF(MM_LoadByte(0, addrN(5,9), &value) != 0);
					FAIL_UNLESS_EQ(value, values[4]);
					FAIL_IF(MM_LoadByte(0, addrN(6,10), &value) != 0);
					FAIL_UNLESS_EQ(value, values[5]);

					return true;
				},
			},
			{
				.name = "Writes through a lot of 1 pid should all come back",
				.points = 4,
				.runtest = [](){
					MM_SwapOn();
					for (int i = 0; i < MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES; i += MM_PAGE_SIZE_BYTES) {
						struct MM_MapResult mr1 = MM_Map(0, i, 1);
						print_mapresult(mr1);
					}
					std::map<uint32_t, uint8_t> writes;
					for (int addr = 0; addr < MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES; addr += step) {
						uint8_t value = rand() % 256;
						FAIL_IF(MM_StoreByte(0, addr, value) != 0);
						writes[addr] = value;
					}
					for (int addr = 0; addr < MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES; addr += step) {
						uint8_t value;
						FAIL_IF(MM_LoadByte(0, addr, &value) != 0);
						FAIL_UNLESS_EQ(value, writes[addr]);
					}
					return true;
				},
			},
			{
				.name = "STRESS TEST",
				.points = 4,
				.runtest = [](){
					MM_SwapOn();
					for (int i = 0; i < MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES; i += MM_PAGE_SIZE_BYTES) {
						struct MM_MapResult mr1 = MM_Map(0, i, 1);
						print_mapresult(mr1);
					}
					std::map<uint32_t, uint8_t> writes;
					const size_t limit = 10000;
					srand(1337);
					for (size_t i = 0; i < limit; i++) {
						uint32_t addr = rand() % MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES;
						uint8_t value = rand() % 256;
						FAIL_IF(MM_StoreByte(0, addr, value) != 0);
						writes[addr] = value;
					}
					srand(1337);
					for (size_t i = 0; i < limit; i++) {
						uint32_t addr = rand() % MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES;
						uint8_t value;;
						FAIL_IF(MM_LoadByte(0, addr, &value) != 0);
						FAIL_UNLESS_EQ(value, writes[addr]);
					}
					return true;
				},
			},
		},
	},
	{
		.name = "Section 2: (15 pts) Memory manager supports requests from multiple processes residing in memory concurrently.",
		.tests = {
			{
				.name = "Values written to two different pids should retain value",
				.points = 10,
				.runtest = [](){
					struct MM_MapResult mr1 = MM_Map(0, addrN(5,0), 1);
					print_mapresult(mr1);
					struct MM_MapResult mr2 = MM_Map(1, addrN(5,0), 1);
					print_mapresult(mr2);
					const uint8_t value0 = 0xab;
					const uint8_t value1 = 0xcd;
					FAIL_IF(MM_StoreByte(0, addrN(5,2), value0) != 0);
					FAIL_IF(MM_StoreByte(1, addrN(5,2), value1) != 0);
					uint8_t value = 0;
					FAIL_IF(MM_LoadByte(0, addrN(5,2), &value) != 0);
					FAIL_UNLESS_EQ(value, value0);
					FAIL_IF(MM_LoadByte(1, addrN(5,2), &value) != 0);
					FAIL_UNLESS_EQ(value, value1);
					return true;
				},
			},
			{
				.name = "Writes through a lot of many pids should all come back",
				.points = 3,
				.runtest = [](){
					MM_SwapOn();
					for (int pid = 0; pid < 3; pid++) {
						for (int i = 0; i < MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES; i += MM_PAGE_SIZE_BYTES) {
							struct MM_MapResult mr1 = MM_Map(pid, i, 1);
							print_mapresult(mr1);
						}
						std::vector<uint8_t> writes;
						for (int offset = 0; offset < MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES; offset += step) {
							uint8_t value = rand() % 256;
							FAIL_IF(MM_StoreByte(pid, offset, value) != 0);
							writes.push_back(value);
						}
						for (int offset = 0; offset < MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES; offset += step) {
							uint8_t value;
							FAIL_IF(MM_LoadByte(pid, offset, &value) != 0);
							FAIL_UNLESS_EQ(value, writes[offset]);
						}
					}
					return true;
				},
			},
			{
				.name = "Multi-pid STRESS TEST",
				.points = 2,
				.runtest = [](){
					MM_SwapOn();
					for (int pid = 0; pid < 4; pid++) {
						for (int addr = 0; addr < MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES; addr += MM_PAGE_SIZE_BYTES) {
							struct MM_MapResult mr1 = MM_Map(pid, addr, 1);
							print_mapresult(mr1);
						}
					}
					std::map<std::tuple<int, uint32_t>, uint8_t> writes;
					const size_t limit = 10000;
					srand(1337);
					for (size_t i = 0; i < limit; i++) {
						int pid = rand() % MM_MAX_PROCESSES;
						uint32_t addr = rand() % MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES;
						uint8_t value = rand() % 256;
						FAIL_IF(MM_StoreByte(pid, addr, value) != 0);
						writes[{pid, addr}] = value;
					}
					srand(1337);
					for (size_t i = 0; i < limit; i++) {
						int pid = rand() % MM_MAX_PROCESSES;
						uint32_t addr = rand() % MM_PROCESS_VIRTUAL_MEMORY_SIZE_BYTES;
						uint8_t got;
						uint8_t want = writes[{pid, addr}];	
						FAIL_IF(MM_LoadByte(pid, addr, &got) != 0);
						FAIL_UNLESS_EQ(got, want);
					}
					return true;
				},
			},
		},
	},
};

int main(int argc, char **argv) {
	srand(42);
	mkdir("tests.out", 0755);
	std::map<std::tuple<int, int>, bool> results;
	int selected_section = -1;
	int selected_test = -1;
	int should_fork = 1;
	if (argc >= 2) {
		sscanf(argv[1], "%d.%d", &selected_section, &selected_test);
		should_fork = 0;
		Debug();
	}

	for (int section_index = 0; section_index < (int)section_tests.size(); section_index++) {
		if (selected_section >= 0 && section_index != selected_section) continue;
		const auto &st = section_tests[section_index];
		char filepath[128];
		for (int i = 0; i < (int)st.tests.size(); i++) {
			if (selected_test >= 0 && i != selected_test) continue;
			const auto& t = st.tests[i];
			bool passed = false;
			pid_t pid;
			std::cout << "========== Test " << section_index << "." << i << " start: " << t.name << std::endl;
			if (should_fork) {
				if ((pid = fork()) == 0) {
					// Child, run test.
					std::thread timeout([]() { sleep(10); exit(2); });
					timeout.detach();
					// Redirect stdout+stderr to files
					//sprintf(filepath, "tests.out/%d.%d.out", section_index, i);
					//CHECK(freopen(filepath, "w", stdout) != NULL);
					//sprintf(filepath, "tests.out/%d.%d.err", section_index, i);
					//CHECK(freopen(filepath, "w", stderr) != NULL);
					bool child_passed = t.runtest();
					return child_passed ? 0 : 1;
				} else if (pid < 0) {
					std::cerr << "fork() failed: " << errno << " " << strerror(errno) << std::endl;
					return 1;
				} else {
					int wstatus;
					if (waitpid(pid, &wstatus, 0) < 0) {	
						std::cerr << "waitpid() failed: " << errno << " " << strerror(errno) << std::endl;
					}
					if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
						passed = true;
					}
					if (WIFSIGNALED(wstatus)) {
						std::cout << "Terminated by signal: " << WTERMSIG(wstatus) << std::endl;
					}
				}
			} else {
				passed = t.runtest();
			}
			sprintf(filepath, "tests.out/%d.%d.*", section_index, i);
			std::cout << "========== Test " << section_index << "." << i << " end: " << (passed ? "PASSED" : "FAILED") << " output in " << filepath << std::endl;
			std::cout << std::endl;
			results[{section_index, i}] = passed;
		}
	}


	int awarded = 0;
	int total = 0;
	// Summary at the end
	for (int section_index = 0; section_index < (int)section_tests.size(); section_index++) {
		if (selected_section >= 0 && section_index != selected_section) continue;
		const auto &st = section_tests[section_index];
		int section_awarded = 0;
		int section_total = 0;
		std::cout << "== Tests for section: " << st.name << std::endl;

		for (int i = 0; i < (int)st.tests.size(); i++) {
			if (selected_test >= 0 && i != selected_test) continue;
			const auto& t = st.tests[i];
			bool passed = results[{section_index, i}];
			std::cout << "====== Test " << section_index << "." << i << " " << (passed ? "PASSED" : "FAILED") << "(" << t.points << " pts): " << t.name << std::endl;
			section_total += t.points;
			total += t.points;
			if (passed) section_awarded += t.points;
			if (passed) awarded += t.points;
		}
		std::cout << "== Tests scored " << section_awarded << " / " << section_total << std::endl;
	}
	std::cout << "== Total scored " << awarded << " / " << total << std::endl;
	return 0;
}


