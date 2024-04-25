#include "wfs.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h> 
#include <fuse.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>

char * diskImage;

off_t get_parent_inode(const char * path){
    printf("doing getattr\n");
    char newPath[100];
    strcpy(newPath, path);
    // 1. declarations & error checks
    struct stat fileStat;
    int fd = open(diskImage, O_RDWR);
    if(fd == -1){
        perror("opening file");
        exit(1);
    }
    if(stat(path, &fileStat) < 0){ 
        exit(1);
    }

    //ptr to start of superblock
    struct wfs_sb * ptr = mmap(NULL, fileStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    // 2. finding file
    char * token = strtok(newPath, "/");
    off_t currentInodeAddr = (off_t)ptr + ptr->i_blocks_ptr;
    off_t prevInode = 0;

    int found = 0;

    //find
    while(token){
        if(strlen(token) == 0){
            continue;
        }
    
        //find directory entry of current token (iterate through data blocks of direcory inode)
        struct wfs_inode * currentInode = (struct wfs_inode *) currentInodeAddr;
        if(S_ISDIR(currentInode->mode)){
            for(int i = 0; i < N_BLOCKS; i++){
                off_t offset = currentInode->blocks[i];
                struct wfs_dentry * dirEntry = (struct wfs_dentry *) offset;
                if(strcmp(dirEntry->name, token) == 0){
                    found = 1;

                    //traversing inodes to find current token inode
                    //update currInodeAddr, finalInode
                    off_t currAddr = (off_t)ptr + ptr->i_blocks_ptr;
                    for(int i = 0; i < ptr->num_inodes; i++){
                        struct wfs_inode tempInode;
                        memcpy(&tempInode, (struct wfs_inode *)currAddr, sizeof(struct wfs_inode));
                        // if(tempInode == NULL){ //correct???
                        //     continue;
                        // }
                        if(tempInode.num == dirEntry->num){
                            prevInode = currentInodeAddr;
                            currentInodeAddr = currAddr;
                            break;
                        }
                        else{
                            currAddr += BLOCK_SIZE;
                        }
                    }
                    break;
                }
            }
        }
        else{
            break;
        }
        if(!found){
            return -ENOENT;
        }

        found = 0;
        token = strtok(NULL, "/");
    }

    close(fd);

    return prevInode;
}

//TODO:
// 2. implement functions 

//Return file attributes. The "stat" structure is described in detail in the stat(2) manual page. 
//For the given pathname, this should fill in the elements of the "stat" structure. 
//If a field is meaningless or semi-meaningless (e.g., st_ino) then it should be set to 0 or given a "reasonable" value. 
//This call is pretty much required for a usable filesystem.
static int wfs_getattr(const char *path, struct stat *stbuf) {
    char *pathCopy = malloc(strlen(path) + 1);
    strcpy(pathCopy, path);
    printf("path: %s\n", pathCopy);
    // 1. declarations & error checks
    struct stat fileStat;
    int fd = open(diskImage, O_RDWR);
    if(fd == -1){
        perror("opening file");
        return 1;
    }
    if(stat(diskImage, &fileStat) < 0){ 
        printf("error in stat\n");
        return 1;
    }

    //ptr to start of superblock
    struct wfs_sb * ptr = mmap(NULL, fileStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   
    // 2. finding file
    char * token = strtok(pathCopy, "/");
    int tokenCount = 0;

    off_t *currentInodeAddr = (off_t*)ptr + ptr->i_blocks_ptr;
    off_t *finalInode = 0;
    int found = 0;

    //find
    while(token){ 
        tokenCount++;
        printf("while token: %s\n", token);
        if(strlen(token) == 0){
            continue;
        }
    
        //find directory entry of current token (iterate through data blocks of direcory inode)
        struct wfs_inode * currentInode = (struct wfs_inode *) currentInodeAddr;
        if(S_ISDIR(currentInode->mode)){
            printf("isDirectory\n");
            for(int i = 0; i < N_BLOCKS; i++){
                printf("iteration: %i\n", i);
                off_t *offset = &currentInode->blocks[i];
                struct wfs_dentry * dirEntry = (struct wfs_dentry *) offset;
                if(strcmp(dirEntry->name, token) == 0){
                    printf("found1\n");
                    found = 1;

                    //traversing inodes to find current token inode
                    //update currInodeAddr, finalInode
                    off_t *currAddr = (off_t*)ptr + ptr->i_blocks_ptr;
                    for(int i = 0; i < ptr->num_inodes; i++){
                        struct wfs_inode tempInode;
                        memcpy(&tempInode, (struct wfs_inode *)currAddr, sizeof(struct wfs_inode));
                        // if(&tempInode == NULL){ //correct???
                        //     continue;
                        // }
                        if(tempInode.num == dirEntry->num){
                            printf("found final inode");
                            currentInodeAddr = currAddr;
                            finalInode = currAddr;
                            break;
                        }
                        else{
                            currAddr += BLOCK_SIZE;
                        }
                    }
                    break;
                }
            }
        }
        else{
            printf("reached a file\n");
            break;
        }
        if(!found){
            printf("file not found\n");
            return -ENOENT;
        }

        found = 0;
        token = strtok(NULL, "/");
    }

    //3. update struct parameter with found inode
    if(!tokenCount){
        struct wfs_inode *foundInode = (struct wfs_inode *) finalInode;
        //memcpy(&foundInode, (struct wfs_inode *) finalInode, sizeof(struct wfs_inode));
        stbuf->st_uid = foundInode->uid;
        stbuf->st_gid = foundInode->gid;

        time_t current_time = time(NULL);
        foundInode->atim = current_time;
        stbuf->st_atime = foundInode->atim;

        stbuf->st_mtime = foundInode->mtim;
        stbuf->st_mode = foundInode->mode;
        stbuf->st_size = foundInode->size;
    }
    else{
        struct wfs_inode *rootInode = (struct wfs_inode *)((off_t*)ptr + ptr->i_blocks_ptr);
        stbuf->st_uid = rootInode->uid;
        stbuf->st_gid = rootInode->gid;

        time_t current_time = time(NULL);
        rootInode->atim = current_time;
        stbuf->st_atime = rootInode->atim;

        stbuf->st_mtime = rootInode->mtim;
        stbuf->st_mode = rootInode->mode;
        stbuf->st_size = rootInode->size;
    }
    close(fd);
    printf("DONE W GETATTR\n");     
    return 0; // Return 0 on success
}

//TODO: add valid name checking logic?
// ADD INDIRECT BLOCK LOGIC TO THIS AND MKDI
//Make a file 
static int wfs_mknod(const char* path, mode_t mode, dev_t rdev){
    //setup:
    struct stat fileStat;
    int fd = open(diskImage, O_RDWR);
    if(fd == -1){
        perror("opening file");
        return 1;
    }
    if(stat(path, &fileStat) < 0){ 
        return 1;
    }
    struct wfs_sb * ptr = mmap(NULL, fileStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   
    //1. attempt to add new file (updating inode bitmap, inodes)

    //check new file to be added can be added (check inode bitmap for adequate space).
    int index = -1;
    int rowIndex, colIndex = 0;
    int size = ptr->num_inodes / 32;
    int bitmap[size];
    memcpy(bitmap, (off_t *)ptr + ptr->i_bitmap_ptr, sizeof(int) * size);

    for(int i = 0; i < ptr->num_inodes; i++){
        //checking if ith bit is 0. Set to 1 if it is
        if(i%32 == 0 && i != 0){
            rowIndex++;
            colIndex = 0;
        }
        if(!(bitmap[rowIndex] & (1 << colIndex))){
            index = i;
            bitmap[rowIndex] |= (1 << colIndex);
            break;
        }
        colIndex++;
    }

    //found a open spot
    if(index == -1){
        return -ENOSPC;
    }

    //write back to disk
    //memcpy((off_t)ptr + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size);

    //create and add inode
    struct wfs_inode newFile;
    newFile.num = rdev; 
    newFile.size = BLOCK_SIZE; 
    newFile.mode = mode;
    newFile.uid = S_ISUID;
    newFile.gid = S_ISGID;

    time_t current_time = time(NULL); //check if right
    newFile.atim = current_time; 
    newFile.mtim = current_time; 
    //init newFile.ctim?

   // memcpy((off_t)ptr + ptr->i_blocks_ptr + (off_t)(index * BLOCK_SIZE), &newFile, sizeof(struct wfs_inode));

    //2. update parent directory with our new file. (updating data bitmap and datablocks)

    //check there is space in data bitmap
    int dsize = ptr->num_data_blocks / 32;
    int dbitmap[dsize];
    memcpy(dbitmap, (off_t*)ptr + ptr->d_bitmap_ptr, sizeof(int) * dsize);
    rowIndex = 0; colIndex = 0;
    int dindex = -1;
    for(int i = 0; i < ptr->num_data_blocks; i++){
        //checking if ith bit is 0. Set to 1 if it is
        if(i%32 == 0 && i != 0){
            rowIndex++;
            colIndex = 0;
        }
        if(!(dbitmap[rowIndex] & (1 << colIndex))){
            dindex = i;
            dbitmap[rowIndex] |= (1 << colIndex);
            break;
        }
        colIndex++;
    }

    //enforcing that we have found 1 free bit(data block) to place dir entry
    if(dindex == -1){
        return -ENOSPC;
    }

    //write back to disk
    //memcpy((off_t)ptr + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);

    //update data blocks with dir entry, write dir entry to data block in disk
    char * name = strrchr(path, '/');
    struct wfs_dentry dirEntry;
    strcpy(dirEntry.name, name + 1);
    dirEntry.num = newFile.num;

    //memcpy((off_t)ptr + ptr->d_blocks_ptr + (off_t)(dindex * BLOCK_SIZE), &dirEntry, sizeof(struct wfs_dentry));
    
    //update parent dir inode (for blocks it's tracking).
    off_t pI = get_parent_inode(path);
    off_t *pIptr = &pI;
    struct wfs_inode *parentInode = (struct wfs_inode *)pIptr;
    struct wfs_dentry *existingEntry;
    int placed = 0;
    for(int i = 0; i < N_BLOCKS; i++){
        if(!parentInode->blocks[i] && !placed){
            parentInode->blocks[i] = (off_t)ptr + ptr->d_blocks_ptr + (off_t)(dindex * BLOCK_SIZE);
            placed++;
        }
        else if(parentInode->blocks[i]){
            existingEntry = (struct wfs_dentry*)parentInode->blocks[i];
            if(strcmp(existingEntry->name, dirEntry.name) == 0){
                return -EEXIST;
            }
        }
        
    }
    if(!placed){
        return -ENOSPC; //correct error check?
    }

    memcpy((off_t *)ptr + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size);
    memcpy((off_t*)ptr + ptr->i_blocks_ptr + (off_t)(index * BLOCK_SIZE), &newFile, sizeof(struct wfs_inode));
    memcpy((off_t*)ptr + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);
    memcpy((off_t*)ptr + ptr->d_blocks_ptr + (off_t)(dindex * BLOCK_SIZE), &dirEntry, sizeof(struct wfs_dentry));        
    memcpy(pIptr, parentInode, sizeof(struct wfs_inode));

    //check if anything else
    return 0;
}

//Create a directory with the given name. The directory permissions are encoded in mode. 
//See mkdir(2) for details. This function is needed for any reasonable read/write filesystem.
static int wfs_mkdir(const char* path, mode_t mode){
    //setup:
    struct stat fileStat;
    int fd = open(diskImage, O_RDWR);
    if(fd == -1){
        perror("opening file");
        return 1;
    }
    if(stat(path, &fileStat) < 0){ 
        return 1;
    }
    struct wfs_sb * ptr = mmap(NULL, fileStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   
    //1. attempt to add new file (updating inode bitmap, inodes)

    //check new file to be added can be added (check inode bitmap for adequate space).
    int index = -1;
    int rowIndex, colIndex = 0;
    int size = ptr->num_inodes / 32;
    int bitmap[size];
    memcpy(bitmap, (off_t*)ptr + ptr->i_bitmap_ptr, sizeof(int) * size);

    for(int i = 0; i < ptr->num_inodes; i++){
        //checking if ith bit is 0. Set to 1 if it is
        if(i%32 == 0 && i != 0){
            rowIndex++;
            colIndex = 0;
        }
        if(!(bitmap[rowIndex] & (1 << colIndex))){
            index = i;
            bitmap[rowIndex] |= (1 << colIndex);
            break;
        }
        colIndex++;
    }

    //found a open spot
    if(index == -1){
        return -ENOSPC;
    }

    //write back to disk
    //memcpy((off_t)ptr + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size);

    //create and add inode
    struct wfs_inode newFile;
    newFile.num = index; 
    newFile.size = BLOCK_SIZE; 
    newFile.mode = mode;
    newFile.uid = S_ISUID;
    newFile.gid = S_ISGID;

    time_t current_time = time(NULL); //check if right
    newFile.atim = current_time; 
    newFile.mtim = current_time; 
    //init newFile.ctim?

   // memcpy((off_t)ptr + ptr->i_blocks_ptr + (off_t)(index * BLOCK_SIZE), &newFile, sizeof(struct wfs_inode));

    //2. update parent directory with our new file. (updating data bitmap and datablocks)

    //check there is space in data bitmap
    int dsize = ptr->num_data_blocks / 32;
    int dbitmap[dsize];
    memcpy(dbitmap, (off_t*)ptr + ptr->d_bitmap_ptr, sizeof(int) * dsize);
    rowIndex = 0; colIndex = 0;
    int dindex = -1;
    for(int i = 0; i < ptr->num_data_blocks; i++){
        //checking if ith bit is 0. Set to 1 if it is
        if(i%32 == 0 && i != 0){
            rowIndex++;
            colIndex = 0;
        }
        if(!(dbitmap[rowIndex] & (1 << colIndex))){
            dindex = i;
            dbitmap[rowIndex] |= (1 << colIndex);
            break;
        }
        colIndex++;
    }

    //enforcing that we have found 1 free bit(data block) to place dir entry
    if(dindex == -1){
        return -ENOSPC;
    }

    //write back to disk
    //memcpy((off_t)ptr + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);

    //update data blocks with dir entry, write dir entry to data block in disk
    char * name = strrchr(path, '/');
    struct wfs_dentry dirEntry;
    strcpy(dirEntry.name, name + 1);
    dirEntry.num = newFile.num;

    //memcpy((off_t)ptr + ptr->d_blocks_ptr + (off_t)(dindex * BLOCK_SIZE), &dirEntry, sizeof(struct wfs_dentry));
    
    //update parent dir inode (for blocks it's tracking).
    off_t useless1 = get_parent_inode(path);
    off_t *useless2 = &useless1;
    struct wfs_inode *parentInode = (struct wfs_inode *)useless2;
    struct wfs_dentry *existingEntry;
    int placed = 0;
    for(int i = 0; i < N_BLOCKS; i++){
        if(!parentInode->blocks[i] && !placed){
            parentInode->blocks[i] = (off_t)ptr + ptr->d_blocks_ptr + (off_t)(dindex * BLOCK_SIZE);
            placed++;
        }
        else if(parentInode->blocks[i]){
            existingEntry = (struct wfs_dentry*)parentInode->blocks[i];
            if(strcmp(existingEntry->name, dirEntry.name) == 0){
                return -EEXIST;
            }
        }
        
    }
    if(!placed){
        return -ENOSPC; //correct error check?
    }

    memcpy((off_t*)ptr + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size);
    memcpy((off_t*)ptr + ptr->i_blocks_ptr + (off_t)(index * BLOCK_SIZE), &newFile, sizeof(struct wfs_inode));
    memcpy((off_t*)ptr + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);
    memcpy((off_t*)ptr + ptr->d_blocks_ptr + (off_t)(dindex * BLOCK_SIZE), &dirEntry, sizeof(struct wfs_dentry));        
    memcpy(useless2, parentInode, sizeof(struct wfs_inode));

    //check if anything else
    return 0;
}

//Remove (delete) the given file, symbolic link, hard link, or special node. 
//Note that if you support hard links, unlink only deletes the data when the last hard link is removed. 
static int wfs_unlink(const char* path){

    return 0;
}

//Remove the given directory. This should succeed only if the directory is empty (except for "." and "..").
static int wfs_rmdir(const char* path){
    // off_t a1 = get_parent_inode(path);
    // off_t *a2 = &a1;
   // struct wfs_inode *parentInode = (struct wfs_inode *)a2;

    //find directory inode

    //check that all blocks in array are empty, including the indirect. Throw error if not?

    //clear the space it takes in the inodes

    //update bitmaps

    //write back to disk
    return 0;
}

//Read sizebytes from the given file into the buffer buf, beginning offset bytes into the file. See read(2) for full details. 
//Returns the number of bytes transferred, or 0 if offset was at or beyond the end of the file. Required for any sensible filesystem.
static int wfs_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi){
    return 0;
}

//As for read above, except that it can't return 0.
static int wfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi){
    return 0;
}

//Return one or more directory entries (struct dirent) to the caller. This is one of the most complex FUSE functions. 
//It is related to, but not identical to, the readdir(2) and getdents(2) system calls, and the readdir(3) library function. 
//Because of its complexity, it is described separately below. Required for essentially any filesystem, 
//since it's what makes ls and a whole bunch of other things work.
static int wfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi){
    return 0;
}

static struct fuse_operations ops = {
  .getattr = wfs_getattr,
  .mknod   = wfs_mknod,
  .mkdir   = wfs_mkdir,
  .unlink  = wfs_unlink,
  .rmdir   = wfs_rmdir,
  .read    = wfs_read,
  .write   = wfs_write,
  .readdir = wfs_readdir,
};


int main(int argc, char *argv[]) {
    // Initialize FUSE with specified operations
    // Filter argc and argv here and then pass it to fuse_main
    diskImage = argv[1];
    return fuse_main(argc - 1, argv + 1, &ops, NULL);
}
