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
#include <stddef.h>

//to ask: your doubts, then should we update access times every time we access, is off_t a primitive or pointer type
char * diskImage;
char failedgetparent = 0;
int fd;
char * disk;

//returns parent dir of file/dir path
char* get_parent_inode(char * disk, struct wfs_sb * superblock, char * path, int*skipbit){
    //printf("running helper function get_parent_inode\n\n");
    //if root just return immediately
    if(strlen(path) == 1){
        *skipbit = 1;
        return disk + superblock->i_blocks_ptr; //returning addr of root inode
    }

    // setup//////////////////////
    char * token = strtok(path, "/"); //first token is expected file or dir in root dir
    int found = 0;
    int changebit = 0;
    struct wfs_inode currentInode;
    char* prevInodeAddr = disk + superblock->i_blocks_ptr;
    char* currentInodeAddr = disk + superblock->i_blocks_ptr; //starting from root inode 
    /////////////////////////////////////////////////////////////// 
    
    //find target file/dir
    while(token){ 
        //printf("file to find: %s       \n ", token);
    
        //find directory entry of currentInode
        memcpy(&currentInode, currentInodeAddr, sizeof(struct wfs_inode));
        for (int i = 0; i < N_BLOCKS; i++){
            if(currentInode.blocks[i]){ 
                struct wfs_dentry dirEntry;
                memcpy(&dirEntry, disk + currentInode.blocks[i], sizeof(struct wfs_dentry));
                if (strcmp(dirEntry.name, token) == 0){
                    found = 1;

                    // grab inode, update currInodeAddr, prevInodeAddr
                    if(changebit){
                        prevInodeAddr = currentInodeAddr;
                    }
                    else{
                        changebit++;
                    }
                    currentInodeAddr = disk + superblock->i_blocks_ptr + (dirEntry.num * BLOCK_SIZE);
                    break;
                }
            }
        }

        if(!found){
            printf("ERROR: file not found\n");
            failedgetparent = -1;
            return &failedgetparent;
        }

        found = 0;
        token = strtok(NULL, "/");
    }

    //printf("DONE: get parent inode finished\n");  
    return prevInodeAddr;
}

//returns the actual path
char* get_path_inode(char * disk, struct wfs_sb * superblock, char * path, int*skipbit){
   // printf("running helper function get_path_inode\n\n");
    //if root just return immediately
    if(strlen(path) == 1){
        *skipbit = 1;
        return disk + superblock->i_blocks_ptr; //returning addr of root inode
    }

    // setup//////////////////////
    char * token = strtok(path, "/"); //first token is expected file or dir in root dir
    int found = 0;
    int changebit = 0;
    struct wfs_inode currentInode;
    //char* prevInodeAddr = disk + superblock->i_blocks_ptr;
    char* currentInodeAddr = disk + superblock->i_blocks_ptr; //starting from root inode 
    /////////////////////////////////////////////////////////////// 
    
    //find target file/dir
    while(token){ 
        //printf("file to find: %s       \n ", token);
    
        //find directory entry of currentInode
        memcpy(&currentInode, currentInodeAddr, sizeof(struct wfs_inode));
        for (int i = 0; i < N_BLOCKS; i++){
            if(currentInode.blocks[i]){ 
                struct wfs_dentry dirEntry;
                memcpy(&dirEntry, disk + currentInode.blocks[i], sizeof(struct wfs_dentry));
                if (strcmp(dirEntry.name, token) == 0){
                    found = 1;

                    // grab inode, update currInodeAddr, prevInodeAddr
                    if(changebit){
                        //prevInodeAddr = currentInodeAddr;
                    }
                    else{
                        changebit++;
                    }
                    currentInodeAddr = disk + superblock->i_blocks_ptr + (dirEntry.num * BLOCK_SIZE);
                    break;
                }
            }
        }

        if(!found){
            printf("ERROR: file not found\n");
            failedgetparent = -1;
            return &failedgetparent;
        }

        found = 0;
        token = strtok(NULL, "/");
    }

   // printf("DONE: get parent inode finished\n");  
    return currentInodeAddr;
}

//TODO:
// 1. add indirect logic to bottom 3
// 2. debug getattr, mknod, mkdir (maybe redo altogether)
// 3. implement other functions

//Return file attributes. fill stbuf struct
static int wfs_getattr(const char *path, struct stat *stbuf) {

    //setup///////////////////////////////////////

    //create path copy since strtok does not work on const char *path
    char *pathCopy = malloc(strlen(path) + 1); 
    strcpy(pathCopy, path);
    pathCopy[strlen(path)] = '\0';

   //
   // printf("starting getattr.  path: %s\n", pathCopy);

    // declarations & error checks
    // struct stat fileStat;
    // int fd = open(diskImage, O_RDWR);
    // if(fd == -1){
    //     perror("opening file");
    //     return 1;
    // }
    // if(fstat(fd, &fileStat) < 0){ 
    //     printf("error in stat\n");
    //     return 1;
    // }
    ///////////////////////////////////////////
    
   // char * disk = mmap(NULL, fileStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    //ptr to start of superblock
    struct wfs_sb superblock;
    memcpy(&superblock, disk, sizeof(struct wfs_sb));
    struct wfs_sb * ptr = &superblock;
    int skipbit = 0;

    //get the parent inode
    char* parentInodeAddr = get_parent_inode(disk, &superblock, pathCopy, &skipbit);
    if(failedgetparent == -1){ //failure case
        failedgetparent = 0;    
        return -ENOENT;
    }
    if(!skipbit){
        struct wfs_inode parentInode;
        memcpy(&parentInode, parentInodeAddr, sizeof(struct wfs_inode));
        struct wfs_dentry dirEntry;
        int targetFound = -1;
        char * targetName = strrchr(path, '/');
        targetName++;

        //find target directory entry. using this we find target inode
        for(int i = 0; i < N_BLOCKS; i++){
            if(parentInode.blocks[i]){
                memcpy(&dirEntry, disk + parentInode.blocks[i], sizeof(struct wfs_dentry));
                if(strcmp(dirEntry.name, targetName) == 0){
                    targetFound = i;
                    break;
                }
            }
        }

        if(targetFound == -1){
            return -ENOENT; 
        }
    

        //get the target inode & update time
        struct wfs_inode targetInode;
        memcpy(&targetInode, disk + ptr->i_blocks_ptr + (dirEntry.num * BLOCK_SIZE), sizeof(struct wfs_inode));
        time_t current_time;
        time(&current_time);
        targetInode.atim = current_time; //only accessed, so only changing access time
        memcpy(disk + ptr->i_blocks_ptr + (dirEntry.num * BLOCK_SIZE), &targetInode, sizeof(struct wfs_inode));

        //fill in stbuf struct with found target inode
        stbuf->st_uid = targetInode.uid;
        stbuf->st_gid = targetInode.gid;
        stbuf->st_atime = targetInode.atim;
        stbuf->st_mtime = targetInode.mtim;
        stbuf->st_mode = targetInode.mode;
        stbuf->st_size = targetInode.size;
    }
    else{
        struct wfs_inode rootInode;
        memcpy(&rootInode, disk + ptr->i_blocks_ptr, sizeof(struct wfs_inode));
        time_t current_time;
        time(&current_time);
        rootInode.atim = current_time; //only accessed, so only changing access time
        memcpy(disk + ptr->i_blocks_ptr, &rootInode, sizeof(struct wfs_inode));

        //fill in stbuf struct with found target inode
        stbuf->st_uid = rootInode.uid;
        stbuf->st_gid = rootInode.gid;
        stbuf->st_atime = rootInode.atim;
        stbuf->st_mtime = rootInode.mtim;
        stbuf->st_mode = rootInode.mode;
        stbuf->st_size = rootInode.size;        
    }

   // close(fd);
    //printf("DONE: getattr finished\n");     
    return 0; // Return 0 on success
}


//Make a file 
static int wfs_mknod(const char* path, mode_t mode, dev_t rdev){
    //printf("making file using mknod\n");

//setup ///////////////////////////////
    //create path copy since strtok does not work on const char *path
    char *pathCopy = malloc(strlen(path) + 1); 
    strcpy(pathCopy, path);
    pathCopy[strlen(path)] = '\0';

    // declarations & error checks
    // struct stat fileStat;
    // int fd = open(diskImage, O_RDWR);
    // if(fd == -1){
    //     perror("opening file");
    //     return 1;
    // }
    // if(fstat(fd, &fileStat) < 0){ 
    //     printf("error in stat\n");
    //     return 1;
    // }
    
    //char * disk = mmap(NULL, fileStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    //ptr to start of superblock
    struct wfs_sb superblock;
    memcpy(&superblock, disk, sizeof(struct wfs_sb));
    struct wfs_sb * ptr = &superblock;
    int skipbit = 0;
    ///////////////////////////////////////////


    //1. attempt to add new file (updating inode bitmap, inodes)

    //check new file to be added can be added (check inode bitmap for adequate space).
    int index = -1;
    int rowIndex = 0;
    int colIndex = 0;
    int size = ptr->num_inodes / 32;
    int bitmap[size];
    memcpy(bitmap, disk + ptr->i_bitmap_ptr, sizeof(int) * size);

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
    struct wfs_inode newFileInode;
    newFileInode.num = index; 
    newFileInode.size = BLOCK_SIZE; //maybe start w/ 0?
    newFileInode.mode = mode;
    newFileInode.uid = S_ISUID;
    newFileInode.gid = S_ISGID;
    time_t current_time;
    time(&current_time);
    newFileInode.atim = current_time; 
    newFileInode.mtim = current_time; 
    newFileInode.ctim = current_time;

    //init indirect block for our file, add offset to inode
    int dindex = -1;
    int drowIndex = 0;
    int dcolIndex = 0;
    int dsize = ptr->num_data_blocks / 32;
    int dbitmap[dsize];
    memcpy(dbitmap, disk + ptr->d_bitmap_ptr, sizeof(int) * dsize);
    for(int i = 0; i < ptr->num_data_blocks; i++){
        //checking if ith bit is 0. Set to 1 if it is
        if(i%32 == 0 && i != 0){
            drowIndex++;
            dcolIndex = 0;
        }
        if(!(dbitmap[drowIndex] & (1 << dcolIndex))){
            dindex = i;
            dbitmap[drowIndex] |= (1 << dcolIndex);
            break;
        }
        dcolIndex++;
    }
    if(dindex == -1){
        return -ENOSPC;
    }

    newFileInode.blocks[IND_BLOCK] = ptr->d_blocks_ptr + (dindex * BLOCK_SIZE);
    
   // memcpy((off_t)ptr + ptr->i_blocks_ptr + (off_t)(index * BLOCK_SIZE), &newFile, sizeof(struct wfs_inode));

    //2. update parent directory with our new file. (updating data bitmap and datablocks)
    int d2index = -1; //index where dir entry of our new file goes
    int d2rowIndex = 0; 
    int d2colIndex = 0;
    //check there is space in data bitmap
    for(int i = 0; i < ptr->num_data_blocks; i++){
        //checking if ith bit is 0. Set to 1 if it is
        if(i%32 == 0 && i != 0){
            d2rowIndex++;
            d2colIndex = 0;
        }
        if(!(dbitmap[d2rowIndex] & (1 << d2colIndex))){
            d2index = i;
            dbitmap[d2rowIndex] |= (1 << d2colIndex);
            break;
        }
        d2colIndex++;
    }

    //enforcing that we have found 1 free bit(data block) to place dir entry
    if(d2index == -1){
        return -ENOSPC;
    }

    //write back to disk
    //memcpy((off_t)ptr + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);

    //update data blocks with dir entry, write dir entry to data block in disk
    char * name = strrchr(path, '/');
    name++;
    struct wfs_dentry dirEntry;
    strcpy(dirEntry.name, name); 
    dirEntry.num = newFileInode.num;

    //memcpy((off_t)ptr + ptr->d_blocks_ptr + (off_t)(dindex * BLOCK_SIZE), &dirEntry, sizeof(struct wfs_dentry));
    
    //add dir entry to parent inode
    char * shortened = strrchr(path, '/');
    int modifiedPathLength = strlen(path) - strlen(shortened) + 1;
    char * modifiedPath = malloc(sizeof(char) * modifiedPathLength);
    strncpy(modifiedPath, path, modifiedPathLength); //eliminated last part of string so we have a valid argument for get_path_inode

    char *pI = get_path_inode(disk, &superblock, modifiedPath, &skipbit);
    if(failedgetparent == -1){ //failure case
        failedgetparent = 0;    
        return -ENOENT;
    }
    struct wfs_inode parentInode; //acquiring parent inode
    memcpy(&parentInode, pI, sizeof(struct wfs_inode));

    //checking file or dir does not already exist, adding new dir entry if possible
    struct wfs_dentry existingEntry; 
    int placed = 0;
    for(int i = 0; i < N_BLOCKS; i++){ 
        if(!parentInode.blocks[i] && !placed){
            parentInode.blocks[i] = ptr->d_blocks_ptr + (d2index * BLOCK_SIZE); //d2index is correct? -- confirm if unsure
            placed++;
        }
        else if(parentInode.blocks[i]){
            memcpy(&existingEntry, disk + parentInode.blocks[i], sizeof(struct wfs_dentry));
            if(strcmp(existingEntry.name, dirEntry.name) == 0){
                return -EEXIST;
            }
        }
    }
    if(!placed){
        return -ENOSPC; //if no space
    }

    memcpy(disk + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size); //writing inode bitmap
    memcpy(disk + ptr->i_blocks_ptr + (index * BLOCK_SIZE), &newFileInode, sizeof(struct wfs_inode)); //writing new file inode 
    memcpy(disk + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize); //writing data bitmap
    memcpy(disk + ptr->d_blocks_ptr + (d2index * BLOCK_SIZE), &dirEntry, sizeof(struct wfs_dentry)); //writing new dir entry to datablocks
    memcpy(pI, &parentInode, sizeof(struct wfs_inode)); //writing parent inode

   // close(fd);
    //check if anything else
   // printf("mknod finished\n\n");
    return 0;
}

//Create a directory with the given name. The directory permissions are encoded in mode. 
//See mkdir(2) for details. This function is needed for any reasonable read/write filesystem.
static int wfs_mkdir(const char* path, mode_t mode){
    //printf("making dir using mkdir\n");

//setup ///////////////////////////////
    //create path copy since strtok does not work on const char *path
    char *pathCopy = malloc(strlen(path) + 1); 
    strcpy(pathCopy, path);
    pathCopy[strlen(path)] = '\0';

    // declarations & error checks
    // struct stat fileStat;
    // int fd = open(diskImage, O_RDWR);
    // if(fd == -1){
    //     perror("opening file");
    //     return 1;
    // }
    // if(fstat(fd, &fileStat) < 0){ 
    //     printf("error in stat\n");
    //     return 1;
    // }
    
   // char * disk = mmap(NULL, fileStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    //ptr to start of superblock
    struct wfs_sb superblock;
    memcpy(&superblock, disk, sizeof(struct wfs_sb));
    struct wfs_sb * ptr = &superblock;
    int skipbit = 0;
    ///////////////////////////////////////////


    //1. attempt to add new directory (updating inode bitmap, inodes)

    //check new file to be added can be added (check inode bitmap for adequate space).
    int index = -1;
    int rowIndex = 0;
    int colIndex = 0;
    int size = ptr->num_inodes / 32;
    int bitmap[size];
    memcpy(bitmap, disk + ptr->i_bitmap_ptr, sizeof(int) * size);

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
    struct wfs_inode newDirInode;
    newDirInode.num = index; 
    newDirInode.size = BLOCK_SIZE; 
    newDirInode.mode = mode;
    newDirInode.uid = S_ISUID;
    newDirInode.gid = S_ISGID;
    time_t current_time;
    time(&current_time);
    newDirInode.atim = current_time; 
    newDirInode.mtim = current_time; 
    newDirInode.ctim = current_time;
    
    //(debugging purposes: indirect logic went here. Obv not needed tho)
   
    // memcpy((off_t)ptr + ptr->i_blocks_ptr + (off_t)(index * BLOCK_SIZE), &newFile, sizeof(struct wfs_inode));

    //2. update parent directory with our new directory. (updating data bitmap and datablocks)
    int dsize = ptr->num_data_blocks / 32;
    int dbitmap[dsize];
    memcpy(dbitmap, disk + ptr->d_bitmap_ptr, sizeof(int) * dsize);

    int d2index = -1; //index where dir entry of our new file goes
    int d2rowIndex= 0;
    int d2colIndex = 0;
    //check there is space in data bitmap
    for(int i = 0; i < ptr->num_data_blocks; i++){
        //printf("iteration: %i\n", i);
        //checking if ith bit is 0. Set to 1 if it is
        if(i%32 == 0 && i != 0){
            d2rowIndex++;
            d2colIndex = 0;
        }
        if(!(dbitmap[d2rowIndex] & (1 << d2colIndex))){
            d2index = i;
            dbitmap[d2rowIndex] |= (1 << d2colIndex);
            break;
        }
        d2colIndex++;
    }

    //enforcing that we have found 1 free bit(data block) to place dir entry
    if(d2index == -1){
        return -ENOSPC;
    }

    //write back to disk
    //memcpy((off_t)ptr + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);

    //update data blocks with dir entry, write dir entry to data block in disk
    char * name = strrchr(path, '/');
    name++;
    struct wfs_dentry dirEntry;
    strcpy(dirEntry.name, name); 
    dirEntry.num = newDirInode.num;

    //memcpy((off_t)ptr + ptr->d_blocks_ptr + (off_t)(dindex * BLOCK_SIZE), &dirEntry, sizeof(struct wfs_dentry));
    
    //add dir entry to parent inode
    char * shortened = strrchr(path, '/');
    int modifiedPathLength = strlen(path) - strlen(shortened) + 1;
    char * modifiedPath = malloc(sizeof(char) * modifiedPathLength);
    strncpy(modifiedPath, path, modifiedPathLength); //eliminated last part of string so we have a valid argument for get_path_inode

    char *pI = get_path_inode(disk, &superblock, modifiedPath, &skipbit);
    if(failedgetparent == -1){ //failure case
        failedgetparent = 0;    
        return -ENOENT;
    }    
    struct wfs_inode parentInode; //acquiring parent inode
    memcpy(&parentInode, pI, sizeof(struct wfs_inode));

    //checking file or dir does not already exist, adding new dir entry if possible
    struct wfs_dentry existingEntry; 
    int placed = 0;
    for(int i = 0; i < N_BLOCKS; i++){ 
        if(!parentInode.blocks[i] && !placed){
            parentInode.blocks[i] = ptr->d_blocks_ptr + (d2index * BLOCK_SIZE); //d2index is correct? -- confirm if unsure
            placed++;
        }
        else if(parentInode.blocks[i]){
            memcpy(&existingEntry, disk + parentInode.blocks[i], sizeof(struct wfs_dentry));
            if(strcmp(existingEntry.name, dirEntry.name) == 0){
                return -EEXIST;
            }
        }
    }
    if(!placed){
        return -ENOSPC; //if no space
    }

    memcpy(disk + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size); //writing inode bitmap
    memcpy(disk + ptr->i_blocks_ptr + (index * BLOCK_SIZE), &newDirInode, sizeof(struct wfs_inode)); //writing new directory inode 
    memcpy(disk + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize); //writing data bitmap
    memcpy(disk + ptr->d_blocks_ptr + (d2index * BLOCK_SIZE), &dirEntry, sizeof(struct wfs_dentry)); //writing new dir entry to datablocks
    memcpy(pI, &parentInode, sizeof(struct wfs_inode)); //writing parent inode

   // close(fd);
    //check if anything else
   // printf("DONE: mkdir finished, file created?\n\n");
    return 0;
}

//Remove (delete) the given file, symbolic link, hard link, or special node. 
//Note that if you support hard links, unlink only deletes the data when the last hard link is removed. 
static int wfs_unlink(const char* path){

    return 0;
}

//Remove the given directory. This should succeed only if the directory is empty (except for "." and "..").
//MODEL FUNCTION!!!!
//update time with accesses?
//is our bitmap correct?
//
static int wfs_rmdir(const char* path){

    // //setup //////////////////////////// //////////////////////////// ////////////////////////////
    // struct stat fileStat;
    // int fd = open(diskImage, O_RDWR);
    // if(fd == -1){
    //     perror("opening file");
    //     return 1;
    // }
    // if(fstat(diskImage, &fileStat) < 0){  //issue with stat? fstat instead?
    //     printf("error in stat\n");
    //     return 1;
    // }

    // //new approach to pointer arithmetic...? - CHECK
    // char * disk = mmap(NULL, fileStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    // struct wfs_sb superblock; 
    // memcpy(&superblock, disk, sizeof(struct wfs_sb));
    // struct wfs_sb * ptr = &superblock;
    // char* a1 = get_parent_inode(path);  //CHANGE FUNCTION TO RETURN A CHAR*, assuming here that it returns a char ptr
    // //////////

    // struct wfs_inode parentInode;
    // memcpy(&parentInode, a1, sizeof(struct wfs_inode));
    // char * name = strrchr(path, '/');
    // name++;
    // int found = -1;
    // struct wfs_dentry dEntry;
    // //////////////////////////// //////////////////////////// ////////////////////////////

    // //find directory inode.
    // for(int i = 0; i < N_BLOCKS; i++){
    //     char *inodeOffset = disk + parentInode.blocks[i];
    //     if(!inodeOffset){
    //         continue;
    //     }
    //     memcpy(&dEntry, inodeOffset, sizeof(struct wfs_dentry));
    //     if(strcmp(dEntry.name, name) == 0){
    //         found = i;
    //         break;
    //     }
    // }

    // if(found == -1){
    //     return -ENOENT;
    // }

    // struct wfs_inode currInode;
    // char *currInodeAddr = disk + ptr->i_blocks_ptr + (dEntry.num * BLOCK_SIZE);
    // memcpy(&currInode, currInodeAddr, sizeof(struct wfs_inode));

    // //check that all blocks in array are empty. Throw error if not?
    // for(int i = 0; i < N_BLOCKS; i++){
    //     if(currInode.blocks[i]){
    //         printf("ERROR: directory is not empty\n\n\n");
    //         return 1;
    //     }
    // }
   
    // //find data block index
    // char *dbStart = disk + ptr->d_blocks_ptr;
    // int dbIndex = -1;
    // for(int i = 0; i < ptr->num_data_blocks; i++){
    //     if(dbStart == disk + parentInode.blocks[found]){
    //         dbIndex = i;
    //         break;
    //     }
    //     dbStart = dbStart + BLOCK_SIZE;
    // }

    // if(dbIndex == -1){
    //     printf("ERROR: did not work\n\n");
    // }

    // //clear the space it takes in the inodes
    // memset(currInodeAddr, 0, sizeof(struct wfs_inode)); //its inode itself
    // memset(disk + parentInode.blocks[found], 0, sizeof(struct wfs_dentry)); //removing directory entry of it in parent
    // parentInode.blocks[found] = 0;
    // memcpy(disk + ptr->i_blocks_ptr + (BLOCK_SIZE * parentInode.num), &parentInode, sizeof(struct wfs_inode)); //updating parent inode

    // //update bitmaps
    // int size = ceil(ptr->num_inodes / 32);
    // int bitmap[size];
    // int dbitmap[size];
    // memcpy(bitmap, disk + ptr->i_bitmap_ptr, sizeof(int) * size); //current dir 
    // memcpy(dbitmap, disk + ptr->d_bitmap_ptr, sizeof(int) * size); //for parent 
    // bitmap[currInode.num/32] &= ~(1 << (currInode.num % 32)); //setting to 0. CHECK if correct
    // dbitmap[dbIndex/32] &= ~(1 << (dbIndex % 32)); //setting to 0, CHECK
    // memcpy(disk + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size);
    // memcpy(disk + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * size);

    // //write back to disk
    // close(fd);
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
    fd = open(diskImage, O_RDWR);
    struct stat fileStat;

    if(fd==-1){
        printf("ERROR: opening file");
        exit(1);
    }
    if(fstat(fd, &fileStat) < 0){ 
        return 1;
    }
    disk = mmap(NULL, fileStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return fuse_main(argc - 1, argv + 1, &ops, NULL);
}
