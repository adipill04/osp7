#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <getopt.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "wfs.h"


/*This C program initializes a file to an empty filesystem. The program receives three arguments: the disk image file, 
the number of inodes in the filesystem, and the number of data blocks in the system.
The number of blocks should always be rounded up to the nearest multiple of 32 to prevent 
the data structures on disk from being misaligned. For example:

./mkfs -d disk_img -i 32 -b 200

initializes the existing file disk_img to an empty filesystem with 32 inodes and 224 data blocks. 
The size of the inode and data bitmaps are determined by the number of blocks specified by mkfs. 
If mkfs finds that the disk image file is too small to accomodate the number of blocks, it should exit with return code 1.
 mkfs should write the superblock and root inode to the disk image.*/

//helper functions:
int roundUpByFactor(int num, int factor) {
    int remainder = num % factor;
    if (remainder != 0) {
        num += (factor - remainder);
    }
    return num;
}

//TODO: make sure everything is right : root inode struct initilization, check bitmaps...
int main(int argc, char * argv[]){
    printf("beg of mkfs\n");
    //1. parse arguments
    struct stat fileStat;
    char * diskImage;
    struct wfs_sb superblock; //superblock which needs to be init fully
    struct wfs_inode rootInode; //for init root inode

    int opt;
    while ((opt = getopt(argc, argv, "d:i:b:")) != -1) {
        switch (opt){
            case 'd':
                printf("case d\n");
                if(stat(optarg, &fileStat) < 0){ //ensure this is how stat is working
                    return 1;
                }
                diskImage = optarg;
                printf("end of case d\n");
                break;
            case 'i':
                printf("case i\n");
                superblock.num_inodes = atoi(optarg); 
                if(superblock.num_inodes % 32 != 0){       
                    superblock.num_inodes=roundUpByFactor(superblock.num_inodes, 32);
                }
                break;
            case 'b':
                printf("case b\n");
                superblock.num_data_blocks = atoi(optarg);
                if(superblock.num_data_blocks % 32 != 0){
                    superblock.num_data_blocks=roundUpByFactor(superblock.num_data_blocks, 32);
                }
                break;
            default:
                return 1;
        }
    }

    //2. init rest of superblock
    printf("#2\n");
    //check disk_img big enough
    int iBiteSize = ceil(superblock.num_inodes / 8);
    int DBSize = ceil(superblock.num_data_blocks / 8);
    if(fileStat.st_size < (off_t)sizeof(struct wfs_sb) + (off_t)iBiteSize + 
    (off_t)DBSize + (BLOCK_SIZE * superblock.num_inodes) +  //sizeof(struct wfs_inode) * superblock.num_inodes
    (BLOCK_SIZE * superblock.num_data_blocks)){
        printf("file size not big enough");
        return 1;
    }

    //setting offsets
    superblock.i_bitmap_ptr = sizeof(struct wfs_sb); 
    superblock.d_bitmap_ptr = superblock.i_bitmap_ptr + (off_t)iBiteSize;
    superblock.i_blocks_ptr = superblock.d_bitmap_ptr + (off_t)DBSize;
    superblock.d_blocks_ptr = superblock.i_blocks_ptr + BLOCK_SIZE * superblock.num_inodes; //superblock.i_blocks_ptr + sizeof(struct wfs_inode) * superblock.num_inodes;

    //3. adding root dir to disk
    printf("#3\n");
    int fd = open(diskImage, O_RDWR);
    if(fd == -1){
        perror("opening file");
        return 1;
    }

    //map file onto memory, clear values
    char* disk = mmap(NULL, fileStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memset(disk, 0, fileStat.st_size);

    //add superblock to disk
    memcpy(disk, &superblock, sizeof(struct wfs_sb));
    
    //init fields of root inode
    rootInode.num = 0;
    rootInode.size = BLOCK_SIZE;
    rootInode.mode =  __S_IFDIR | S_IRWXO;
    rootInode.uid = S_ISUID;
    rootInode.gid = S_ISGID;

    time_t current_time;
    time(&current_time);
    rootInode.atim = current_time; 
    rootInode.mtim = current_time; 
    rootInode.ctim = current_time;
    memset(rootInode.blocks, 0, sizeof(off_t) * N_BLOCKS);

    //adding root inode to array 
    memcpy(disk + superblock.i_blocks_ptr, &rootInode, sizeof(struct wfs_inode));

    //updating inode bitmap
    int numIEntries = superblock.num_inodes / 32;
    int numDEntries = superblock.num_data_blocks / 32;
    int bitmap [numIEntries], dbitmap [numDEntries];
    
    memset(bitmap, 0, sizeof(int) * numIEntries);
    memset(dbitmap, 0, sizeof(int) * numDEntries);

    bitmap[0] |= (1 << 0);

    memcpy(disk + superblock.i_bitmap_ptr, bitmap, sizeof(int) * numIEntries); 
    memcpy(disk + superblock.d_bitmap_ptr, dbitmap, sizeof(int) * numDEntries); 

    close(fd);
    return 0;
}
