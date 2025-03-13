# Paging and Virtual Memory Simulator

## Description

Paging is the approach many modern operating systems take to memory management. Paging allows for commonly accessed data to be stored in physical memory while less frequently accessed data is saved to the disk, even if the data belongs to the same program. Pages are chunks of memory of a predefined size, which elminates problems with external fragmentation.

This program simulates the process of storing and retreiving pages from disk when needed, and the process of translating virtual memory addresses into physical memory addresses.

## Setup

1. Clone this repository into its own directory
2. Run the ```make``` command to compile the project
3. Setup is complete

## Running the Program

1. Run the command ```./mm_test``` to run all tests on the memory manager simulator
    - Note: The tests can be seen in mm_test.cc 

## Credits

Mark Sheahan

[Charlie Engler](https://github.com/charlieengler)

This project was completed for Mark Sheahan's CS 3013 (Operating Systems) course at Worcester Polytechnic Insitute. Most of the starter code was written by Professor Sheahan and can be seen at the point of "Initial Commit". I implemented all of the memory manager logic and data structures in subsequent commits.
