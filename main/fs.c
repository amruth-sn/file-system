#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#define MAX_F_NAME 16
#define MAX_FILDES 32
#define MAX_FILES 64

#define ONE_MEGABYTE 1048576
//Global stuffs

typedef struct SB {
        int fat_idx; // First block of the FAT
        int fat_len; // Length of FAT in blocks
        int dir_idx; // First block of directory
        int dir_len; // Length of directory in blocks
        int data_idx; // First block of file-data
        int free; //list of blocks that have not been used yet
} super_block;

typedef struct DE {
        int used; // Is this file-"slot" in use
        char name [MAX_F_NAME + 1]; // DOH!
        int size; // file size
        int head; // first data block of file
        int ref_cnt; // how many open file descriptors are there?
                    // ref_cnt > 0 -> cannot delete file
} dir_entry;

typedef struct FD {
        int used; // fd in use
        int file;//index of dir entry that fd refers to               // the first block of file (f) to which fd refers to
        int offset; // position of fd within (f)
} file_descriptor;

super_block fs;
file_descriptor fildes[MAX_FILDES]; // 32
int FAT[8 * (BLOCK_SIZE / sizeof(int))] = {0};  // Will be populated with the FAT data
dir_entry DIR[BLOCK_SIZE / sizeof(dir_entry)]; // Will be populated with the directory data
char fs_buffer[BLOCK_SIZE]; // r/w buffs
//Headers
int main();
int make_fs(char *disk_name);
int mount_fs(char *disk_name);
int umount_fs(char *disk_name);
int fs_open(char *name);
int fs_close(int filedesc);
int fs_create(char *name);
int fs_delete(char *name);
int fs_read(int filedesc, void *buf, size_t nbyte);
int fs_write(int filedesc, void *buf, size_t nbyte);
int fs_get_filesize(int filedesc);
int fs_listfiles(char ***files);
int fs_lseek(int filedesc, off_t offset);
int fs_truncate(int filedesc, off_t length);

//Implementation


int make_fs(char *disk_name){

        if(make_disk(disk_name) == -1){
                return -1;
        }

        if(open_disk(disk_name) == -1){
                return -1;
        }
        //sb_idx = 0
        //sb_len = 1
        fs.dir_idx = 1;
        fs.dir_len = 1;
        fs.fat_idx = 2;
        fs.fat_len = 8;
        fs.data_idx = 10;
        fs.free = 10;
        //make nth entry point to n+1th entry all the way to 8192

        block_write(0, (char *)&fs);


        int i;
        for(i = 0; i < DISK_BLOCKS; i++){
                FAT[i] = -1;
        }
        memset(DIR, 0, sizeof(DIR));

        for(i = 0; i < MAX_FILES; i++){
                DIR[i].used = 0;
                DIR[i].head = -1;
                DIR[i].size = 0;
                DIR[i].ref_cnt = 0;
        }

        for(i = 0; i < fs.fat_len; i++){
                block_write(fs.fat_idx + i, (char * )&FAT[i * BLOCK_SIZE/sizeof(int)]);
        }
        block_write(fs.dir_idx, (char * )&DIR);

        close_disk();
        return 0;
}
//FAT has several lists of blocks
//free ptr is always first block that is open when u call fs_write
int mount_fs(char *disk_name){
        if(open_disk(disk_name) == -1){
                return -1;
        }
        block_read(0, (char *) &fs);

        //dynamically allocate block for buffer, used in fs_read/write
        //fs_buffer = (char *) malloc(BLOCK_SIZE);

        int i;
        for (i = 0; i < fs.fat_len; i++) {
                block_read(fs.fat_idx + i, (char *)&FAT[i * BLOCK_SIZE/sizeof(int)]);
        }

        block_read(fs.dir_idx, (char *)&DIR);

        return 0;
}

int umount_fs(char *disk_name){
        //open_disk(disk_name);
        block_write(0, (char *)&fs);
        int i;
        for (i = 0; i < fs.fat_len; i++) {
                if(block_write(fs.fat_idx + i, (char *)&FAT[i * BLOCK_SIZE/sizeof(int)]) == -1){
                        return -1;
                }
        }

        if (block_write(fs.dir_idx, (char *)&DIR) == -1) {
                return -1;
        }
        //free(fs_buffer);

        close_disk();
        return 0;
}

int fs_open(char *name){
        int fildescount = 0;
        int i = 0;
        for(i = 0; i < MAX_FILDES; i++){
                if(fildes[i].used){
                        fildescount++;
                }
        }
        if(fildescount >= MAX_FILDES){
                return -1;
        }

        int block_index = -1;
        for(i = 0; i < MAX_FILES; i++){
                if((strcmp(name, DIR[i].name) == 0) && DIR[i].used){
                        block_index = i;
                }
        }
        if(block_index == -1){
                return -1;
        }

        int fd = -1;
        for(i = 0; i < MAX_FILDES; i++){
                if(!fildes[i].used){
                        fd = i;
                }
        }
        if(fd == -1){
                return -1;
        }

        DIR[block_index].ref_cnt++;
        fildes[fd].used = 1;
        fildes[fd].file = block_index;
        fildes[fd].offset = 0;
        return fd;
}

int fs_close(int filedesc){

        if(fildes[filedesc].used == 0){
                return -1;
        }

        if(DIR[fildes[filedesc].file].ref_cnt == 0 || fildes[filedesc].file == -1){
                return -1;
        }

        DIR[fildes[filedesc].file].ref_cnt--;

        fildes[filedesc].file = -1;
        fildes[filedesc].used = 0;
        return 0;
}

int fs_create(char *name){
        if (strlen(name) > MAX_F_NAME) {
                return -1;
        }

        int i;

        for (i = 0; i < MAX_FILES; ++i) {
                if (DIR[i].used && strcmp(DIR[i].name, name) == 0) {
                        return -1;
                }
        }

        int index = -1;
        for(i = 0; i < MAX_FILES; i++){
                if(!DIR[i].used){
                        index = i;
                        break;
                }
        }
        if(index == -1){
                return -1;
        }

        DIR[index].used = 1;
        strncpy(DIR[index].name, name, MAX_F_NAME);
        DIR[index].name[MAX_F_NAME] = '\0';
        DIR[index].size = 0;
        DIR[index].ref_cnt = 0;

        for(i = fs.data_idx; i < DISK_BLOCKS; i++){
                if(FAT[i] == -1){
                        fs.free = i;
                        DIR[index].head = i;
                        FAT[i] = i;
                        return 0;
                }
        }
        //No more free blocks...
        return -1;
}


int fs_delete(char *name){
        int i;
        int dirindex = -1;
        for(i = 0; i < MAX_FILES; i++){
                if(strcmp(name, DIR[i].name) == 0){
                        dirindex = i;
                        break;
                }
        }
        if(dirindex == -1){
                return -1;
        }

        if(DIR[dirindex].ref_cnt > 0){
                return -1;
        }

        for(i = 0; i < BLOCK_SIZE; i++){
                fs_buffer[i] = '\0';
        }

        int head = DIR[dirindex].head;
        int temp = DIR[dirindex].head;
        while(FAT[temp] != temp){
                temp = FAT[temp];
                block_write(head, fs_buffer);
                FAT[head] = -1;
                head = temp;
        }
        block_write(temp, fs_buffer);
        FAT[temp] = -1;
        //free data blocks assc. with file. ^^^
        DIR[dirindex].head = -1;
        DIR[dirindex].used = 0;
        DIR[dirindex].name[0] = '\0';

        return 0;
}

int fs_read(int filedesc, void *buf, size_t nbyte){
        //int index = DIR[fildes[filedesc].file].head;
        //fildes[filedesc].offset
        if(filedesc < 0 || filedesc > MAX_FILDES || !fildes[filedesc].used){
                return -1;
        }

        int i;
        int bytes_to_read = nbyte;
        if(bytes_to_read > DIR[fildes[filedesc].file].size - fildes[filedesc].offset){
                bytes_to_read = DIR[fildes[filedesc].file].size - fildes[filedesc].offset;
        }

        int off_block = fildes[filedesc].offset >> 12; //offset occurs on off_block'th block of file
        int true_off = fildes[filedesc].offset & (BLOCK_SIZE - 1); // offset is on true_off'th byte of off_block
        int temp = DIR[fildes[filedesc].file].head;
        int count = 0;

        // how do u check whether the current block is equal to the offset block. WALK THE FAT
        for(i = 0; i < off_block; i++){
                temp = FAT[temp];
        }

        while(bytes_to_read > 0){

                block_read(temp, fs_buffer);
                memcpy(buf + count, fs_buffer + true_off, ((bytes_to_read < BLOCK_SIZE - true_off) ? bytes_to_read : BLOCK_SIZE - true_off));

                if(BLOCK_SIZE - true_off > bytes_to_read){
                        count += bytes_to_read;
                        bytes_to_read -= bytes_to_read;
                        //true_off += bytes_to_read;
                }
                else{
                        count += BLOCK_SIZE - true_off;
                        bytes_to_read -= BLOCK_SIZE - true_off;
                        true_off += BLOCK_SIZE - true_off;
                }

                true_off %= BLOCK_SIZE;

                if(temp == FAT[temp]){
                        break;
                }else{
                        temp = FAT[temp];
                }
        }

        fildes[filedesc].offset += count;
        //clear fs buffer
        for(i = 0; i < sizeof(fs_buffer); i++){
                fs_buffer[i] = '\0';
        }
        return count;


}


//FIX READ AND THEN TEST.
//FIX SIZING ISSUES AND THEN TEST.
//REDO WRITE THEN TEST.
//REDO READ THEN TEST.

int fs_write(int filedesc, void *buf, size_t nbyte){
        if(filedesc < 0 || filedesc > MAX_FILDES || !fildes[filedesc].used){
                return -1;
        }

        //while no more nbyte :D ...or no more blocks left :(

        int count = 0;
        int i,j;
        int off_block = fildes[filedesc].offset >> 12;
        int true_off = fildes[filedesc].offset & (BLOCK_SIZE - 1);

        if(nbyte > (16 * ONE_MEGABYTE) - fildes[filedesc].offset){
                nbyte = (16 * ONE_MEGABYTE) - fildes[filedesc].offset;
        }


        int temp = DIR[fildes[filedesc].file].head;
        if(DIR[fildes[filedesc].file].size == 0) {// complete new file

        }
        else{
                for(i = 0; i < off_block; i++){
                        temp = FAT[temp];
                }
        }

        while(nbyte > 0){
                int bytes_to_write = BLOCK_SIZE - true_off; //how many bytes to fill up current block
                if(bytes_to_write > nbyte){
                        bytes_to_write = nbyte; //bytes left to write to "fill up" block becomes bytes left to write globally
                }

                block_read(temp, fs_buffer);
                memcpy(fs_buffer + true_off, buf + count, bytes_to_write);
                block_write(temp, fs_buffer);
                count += bytes_to_write;
                nbyte -= bytes_to_write;
                fildes[filedesc].offset += bytes_to_write;

                if(fildes[filedesc].offset % BLOCK_SIZE == 0){
                        int flag = 1;           //have to find a new block...
                        true_off = 0;
                        for(j = fs.data_idx; j < DISK_BLOCKS; j++){
                                if(FAT[j] == -1 && j != temp){
                                        FAT[temp] = j;
                                        temp = j;
                                        FAT[j] = j;
                                        flag = 0;
                                        break;
                                }
                        }
                        if(flag){
                                break;
                        }

                }
        }

        if(fildes[filedesc].offset > DIR[fildes[filedesc].file].size){
                DIR[fildes[filedesc].file].size = fildes[filedesc].offset; //set file size to offset if and only if offset > old size, which implies new stuff was added to the file.
        }



//clear fs buffer
        for(i = 0; i < sizeof(fs_buffer); i++){
                fs_buffer[i] = '\0';
        }
        return count;

}


int fs_get_filesize(int filedesc){
        if(filedesc > MAX_FILDES || filedesc < 0 || !fildes[filedesc].used){
                return -1;
        }
        else{
                return DIR[fildes[filedesc].file].size;
        }
}

int fs_listfiles(char ***files){
        //files = malloc(str array of size 65, 15);
        //loop thru DIR and populate
        //return 0 or -1;

        if((*files = (char**) malloc((MAX_FILES + 1) * sizeof(char *))) == NULL){
                return -1;
        }

        int i, j;
        for(i = 0, j = 0; i < MAX_FILES; i++){
                if(DIR[i].used){
                        if(((*files)[j]= (char *) malloc((strlen(DIR[i].name)) + 1)) == NULL){
                                return -1;
                        }

                        strcpy((*files)[j++], DIR[i].name);
                }
        }
        (*files)[j] = NULL;
        return 0;
}

int fs_lseek(int filedesc, off_t offset){
        if(filedesc < 0 || filedesc > MAX_FILDES || !fildes[filedesc].used){
                return -1;
        }

        if(offset > DIR[fildes[filedesc].file].size || offset < 0){
                return -1;
        }

        fildes[filedesc].offset = offset;
        return 0;
}

int fs_truncate(int filedesc, off_t length){
        int last_block = length >> 12;
        int length_pos = length & (BLOCK_SIZE - 1);
        int head = DIR[fildes[filedesc].file].head;

        if(filedesc > MAX_FILDES || filedesc < 0 || !fildes[filedesc].used){
                return -1;
        }

        if(length > DIR[fildes[filedesc].file].size){
                return -1;
        }
        //Assuming length < file.size and filedesc is valid
        if(fildes[filedesc].offset > length){
                fildes[filedesc].offset = length;
        }

        int i;
        for(i = 0; i < last_block; i++){
                head = FAT[head];
        }

        for(i = 0; i < BLOCK_SIZE; i++){
                fs_buffer[i] = '\0';
        }

        block_read(head, fs_buffer);
        for(i = length_pos+1; i < BLOCK_SIZE; i++){
                fs_buffer[i] = '\0';
        }
        //block_read
        // clear everything after length_pos within this block. (last_block).
        //block_write the same block back to disk
        block_write(head, fs_buffer);


        for(i = 0; i < BLOCK_SIZE; i++){
                fs_buffer[i] = '\0';
        }
        int flag = 1;
        int temp = head;
        while(FAT[temp] != temp){
                block_write(temp, fs_buffer);
                if(temp == FAT[temp]){
                        break;
                }else{
                        temp = FAT[temp];
                        if(flag){
                                FAT[head] = head;
                                flag = 0;
                        }
                        else{
                                FAT[head] = -1;
                        }

                        head = temp;
                }
        }

        DIR[fildes[filedesc].file].size = length;
        return 0;
}




//MAIN FUNC

/*
int main() {
    // Initialize the virtual disk and file system
        printf("hi\n");
    if (make_fs("virtual_disk") == -1 || mount_fs("virtual_disk") == -1) {
        fprintf(stderr, "Failed to initialize the file system.\n");
        return 1;
    }
        printf("hi2\n");
    // Create a few files
    if (fs_create("file1") == -1 || fs_create("file2") == -1 || fs_create("file3") == -1) {
        fprintf(stderr, "Failed to create files.\n");
        umount_fs("virtual_disk");
        return 1;
    }

    // Open files
    int fd1 = fs_open("file1");
    int fd2 = fs_open("file2");
    int fd3 = fs_open("file3");

    if (fd1 == -1 || fd2 == -1 || fd3 == -1) {
        fprintf(stderr, "Failed to open files.\n");
        umount_fs("virtual_disk");
        return 1;
    }

    // Write some data to the files
        fs_write(fd2, "Greetings, file2!", 18) == -1 ||
        fs_write(fd3, "Salutations, file3!", 20) == -1) {
        fprintf(stderr, "Failed to write data to files.\n");
        umount_fs("virtual_disk");
        return 1;
    }
        if(fs_lseek(fd1, 0) == -1 || fs_lseek(fd2, 0) == -1 || fs_lseek(fd3, 0) == -1){
                fprintf(stderr, "Failed to lseek.\n");
                umount_fs("virtual_disk");
                return -1;
        }

    // Read data from the files
    char buf1[4078], buf2[19], buf3[21];
    if (fs_read(fd1, buf1, 4077) == -1 ||
        fs_read(fd2, buf2, 18) == -1 ||
        fs_read(fd3, buf3, 20) == -1) {
        fprintf(stderr, "Failed to read data from files.\n");
        umount_fs("virtual_disk");
        return 1;
    }

    buf1[4077] = buf2[18] = buf3[20] = '\0';  // Null-terminate the strings

    // Display the read data
    printf("Data from file1: %s\n", buf1);
    printf("Data from file2: %s\n", buf2);
    printf("Data from file3: %s\n", buf3);

        //printf("%d", fs_get_filesize(fd1));
        //printf("\n");
        printf("%d", fs_get_filesize(fd2));
        printf("\n");
        printf("%d", fs_get_filesize(fd3));
        printf("\n");

        //fs_lseek(fd1, 4076);

        fs_write(fd1, "helloamruthtesting123456789", 28);
        fs_lseek(fd1, 0);
        char buf4[29];
        fs_read(fd1, buf4, 4104);
        buf4[4103] = '\0';
        printf("\n\n\n\n\nNEW DATA FROM FILE1\n\n: %s\n\n", buf4);
        printf("%d", fs_get_filesize(fd1));




    // Close files
    if (fs_close(fd1) == -1 || fs_close(fd2) == -1 || fs_close(fd3) == -1) {
        fprintf(stderr, "Failed to close files.\n");
        umount_fs("virtual_disk");
        return 1;
    }


        char*** files;
        if(fs_listfiles(files) == -1){
                fprintf(stderr, "failed to print all files\n");
                umount_fs("virtual_disk");
                return 1;
        }

    // Unmount the file system
    if (umount_fs("virtual_disk") == -1) {
        fprintf(stderr, "Failed to unmount the file system.\n");
        return 1;
    }

    printf("File system testing completed successfully.\n");
    return 0;
}
*/
