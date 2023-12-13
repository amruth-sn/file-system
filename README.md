****File System****

This project was implemented by Amruth Niranjan at Boston University.

Sources used: Man pages

- Global data structures: I used a superblock with 6 members (but the free pointer was largely unused). This included the size of the directory, the length of the directory, the size of the FAT, the length of the FAT, and the starting block of data, all in blocks. I statically allocated space for enough dir_entry structs that would fill up BLOCK_SIZE (4096) bytes, and this number was pretty trivially > 64 (64 files should exist as a maximum at any given time, under the specifications of the project). Each directory entry contains metadata such as the size of the file in question, a flag on whether a file exists or not, the reference count (how many file descriptors are open), the first starting block of the file in question, and the name of the file. I statically allocated 8 BLOCK_SIZEs worth of integers, or 8192 integers in total in my FAT, where each integer entry contained information about the corresponding block in the virtual disk. I also have a file descriptor table which contains MAX_FILDES file descriptors, each with an offset (file pointer), a "used" flag, and an index into which file this descriptor is referencing within the directory itself. Lastly, I statically allocated a file-system buffer called fs_buffer, which was fixed to BLOCK_SIZE bytes.

- make_fs(): I make the disk and open the disk using the functions in disk.h, and then I set some universal values within the superblock. I also initialize each integer in the FAT to -1, and then initialize some values within each directory entry in the directory. Then, I write the superblock, the FAT, and the directory to disk, using the block_write function from disk.h.

- mount_fs(): I first open the disk, then read the contents of the FAT on the virtual disk to the FAT global data structure in memory, and do something similar for the superblock and the directory.

- umount_fs(): I write the contents of the global FAT data structure in memory onto the virtual disk, and do something similar for the superblock and the directory. I then close the disk.

- fs_open(): I loop through the contents of the file descriptor array in order to see if there is an open file descriptor to use. I then make sure the file in question even exists in the directory. If these hold true, I find the first available file descriptor in the table, and set its "file" member to the index within the directory that the file in question lies. I increment the corresponding directory entry's reference count, set the file descriptor to "used", and initialize its offset to 0. I then return the file descriptor.

- fs_close(): I check whether the file descriptor in question is being used, and whether its corresponding directory entry's reference count is already 0 or if the file descriptor does not exist in the directory. If none of these are true, I decrement the corresponding directory entry's reference count, reset the "file" member of the file descriptor in question to -1, and change its status to unused.

- fs_create(): I check if the provided name is valid under the constraints of this project, and check if a file of the same name already exists in the directory. If not, I find the first available slot within the directory. At this index, I set the corresponding directory entry's status to used, copy the provided name into the member name, and initialize the file size and reference count to 0. I then start at fs.data_idx (hardwired to 10), and iterate through every block on the disk to find a FAT entry of -1, meaning a block exists to set the head of this file to. I also set the entry at that location in the FAT to the index itself, creating a loop. This allows me to distinguish between which blocks are being unused entirely as opposed to blocks at the end of a file, which becomes important later. If no free blocks exist, I throw an error.

- fs_delete(): I check if a file with the provided name exists, and if it does, I begin the deletion process only if the reference count is 0 (no file descriptors are currently viewing the file). The deletion process involves walking along the FAT starting at the head, and setting every corresponding FAT entry to -1 and writing over the contents of each block with the null character, and simultaneously traversing to the next block in question and doing this until an entry is encountered which is the same as the index itself. This marks the final block of the file, so I break out of the loop and perform the operation one more time. Then, I reset the head member within the corresponding directory entry to -1, and set the directory status to unused. I also clear the name of the file.

- fs_read(): I check whether the corresponding file descriptor is valid, and whether it is being used. If so, I check whether the number of bytes to read (nbyte) is greater than the size of the file minus the current file pointer, in which case I simply decide to read that many bytes. I use bit arithmetic (courtesy of Larry Woodman) to calculate the position of the offset within its block, as well as calculating the nth block through FAT traversals that the current file pointer lies, and I proceed to iterate to that nth block. I also set a counter to 0 which will signify how many bytes have been read. Then, while I still have more bytes to read, I read the entire offset block from disk to memory. If I have fewer bytes left to read than the BLOCK_SIZE complement of the relative position of the offset within the block, then I only copy that many bytes from the fs_buffer, starting at position equal to the offset within the block, and paste it into the parameterized buffer, at a position equal to how many bytes have been read. I increment the count by this many bytes that were read, and decrement the bytes left to read by itself one last time. Otherwise, I copy in  one BLOCK_SIZE's complement (relative to the offset within the block) amount of bytes into the same locations provided above, increment the count by that number, decrement the number of bytes to read by that number, and also increment the relative position of the offset within the block by that number. I make sure to modulo this offset value within the block by BLOCK_SIZE so as to allow it to take on a stable value for the next iteration. I then iterate through the FAT, finding the next block to read from. If this block's entry in the FAT equals its index, I break out of the loop, because this was the last block. When no more bytes are left to read, I clear the user buffer and increment the actual offset of the corresponding file descriptor to however many bytes have been read, and return the number of bytes that were read.

- fs_write(): I check whether the corresponding file descriptor is valid, and whether it is being used. If so, I check whether the number of bytes to read (nbyte) is greater than the size of 16MB (in bytes) minus the current file pointer's position, in which case I simply decide to read that many bytes, according to the specifications of the project. I use bit arithmetic (courtesy of Larry Woodman) to calculate the position of the offset within its block, as well as calculating the nth block through FAT traversals that the current file pointer lies, and I proceed to iterate to that nth block. I also set a counter to 0 which will signify how many bytes have been written. Then, while I still have more bytes to write according to nbyte, I first check whether the BLOCK_SIZE complement of the relative position of the offset within the block is greater than how many bytes are actually left to write according to nbyte. I set the number of bytes left to write to the smaller value. From here, I read the entire offset block to memory, and copy in however many bytes are left to write at the relative position of the offset within the block's index in the fs_buffer. In many cases, this will be 0, but on the first iteration, it is dependent on the relative position of the offset within the block. I then write this block back to the virtual disk, incrementing the counter by how many ever bytes were read, decrementing nbyte by the same number, and incrementing the offset of the corresponding file descriptor by the same number. Then, I check whether the offset of the corresponding file descriptor is a multiple of BLOCK_SIZE, and if it is, I have to search for a new block. I set the relative position of the offset within the block to 0, and iterate through the DISK (starting at fs.fat_idx, hardwired to 10) until I find a FAT entry initialized to -1. I then set the entry of the block I am currently at to the index of the found block, set the current block to the found block, and set the found block's entry to itself, to temporarily (or permanently) mark the end of the file. If no blocks are found, I instantly break out of the loop. Once the loop ends or I break out, I check to see whether the actual location of the file pointer of the corresponding file descriptor is actually greater than the size that the file used to be. This is so that I can update the size if necessary: if it is greater, then this implies that new data was appended to the file, meaning I should set the size of the file to wherever this offset is. If it is not greater, I do not have to do anything because this means nothing new was appended to the old end of the file and that the size stayed the same.

- fs_get_filesize(): I check if the provided file descriptor is valid and whether it is being used, and if it is, I return the size of the corresponding file by indexing into the directory.

- fs_listfiles(): I dynamically allocate space for MAX_FILES by allocating space for MAX_FILES strings and a NULL terminator at the end. file names. I then check whether each file in the directory is being used, and if so, I dynamically allocate space for the name of the file, and copy this into a subsequent index of the files string array. If fewer than MAX_FILES files exist in the direcetory, then the last entry becomes a NULL terminator.

- fs_lseek(): I check to see whether the provided file descriptor is valid and if it is being used, but also if the provided offset is greater than the current size of the file or less than 0. If not, I set the offset of the corresponding file descriptor to the provided offset.

- fs_truncate(): I use the provided truncate-length to calculate the nth block that the truncate-marker exists, and calculate the marker's position within the block by using simple bit arithmetic (courtesy of Larry Woodman). I check whether the file descriptor is valid and if it is being used, alongside a check to see if the truncate-length is smaller than the current size of the file. If true, I also set the provided file descriptor's offset to the truncate-length in the case where the offset is greater than the truncate-length. From this point, I traverse n blocks to reach the truncate-block. First clearing the fs_buffer, I read the contents of the truncate-block to the fs_buffer, then subsequently clear all bytes of data from the character after the truncate-marker until the end of the block. I then write this block back to disk. From here, I can simply walk the FAT, setting subsequent entries to -1 and clearing their blocks of data, checking to see if the entry matches its own index, implying we are at the end of the file. I also make sure to recreate the loop and set the entry of the truncate-block to itself so that it accurately marks the end of file. I change the size of the file to the truncate-length and return.

Please let me know if there are any bugs. Thank you for reading, and enjoy!

Amruth Niranjan
