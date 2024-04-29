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
#include <libgen.h>

char * diskImage;
char failedgetparent = 0;
int fd;
char * disk;

//TODO:
//1. finish write(), readdir()
//2. fix rmdir if needed
//3. other tests to be solved..?

//gets a valid space within a datablock for a dir entry (grouping em all up)
off_t getValidSpace(struct wfs_inode *parentInode, struct wfs_sb *superblock, int * dbitmap){
    //start at beggining of blocks arary
    off_t currOffset;
    int index = -1;

    struct wfs_dentry entryToPlace = {0};

    //find open spot in blocks array
    for(int i = 0; i < N_BLOCKS; i++){
        if(parentInode->blocks[i]){
            index = i;
        }
    }

    int drowIndex = 0;
    int dcolIndex = 0;
    int dindex = -1; //index of free data block 

    //if no open spots
    if(index == -1){
        //find open spot on data bitmap, update it
        for(int i = 0; i < superblock->num_data_blocks; i++){
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
        
        //add that offset to the open block to our blocks[0]
        parentInode->blocks[0] = superblock->d_blocks_ptr + (dindex * BLOCK_SIZE);

        //return that offset
        return parentInode->blocks[0];
    }

    //check if block has open spots in it
    off_t startBlock = parentInode->blocks[index];
    currOffset = startBlock;

    //if open spot found within the block, return offset
    
    //comparison to find open space
    while(memcmp(disk + currOffset, &entryToPlace, sizeof(struct wfs_dentry)) != 0 && currOffset < startBlock + BLOCK_SIZE){
        currOffset = currOffset + sizeof(struct wfs_dentry);
    }
    if(currOffset < startBlock + BLOCK_SIZE){
        return currOffset;
    }
    

    //if farthest "open" block doesn't have open spots within it
    int d2rowIndex = 0;
    int d2colIndex = 0;
    int d2index = -1; //index of free data block 
   
    //find new spot on data bitmap, update it
    for(int i = 0; i < superblock->num_data_blocks; i++){
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
    if(d2index == -1){
        return -ENOSPC;
    }           
    
    //add that offset to blocks[index+1] (provided index+1 less than N_BLOCKS)
    if(index + 1 < N_BLOCKS){
        parentInode->blocks[index+1] = superblock->d_blocks_ptr + (d2index * BLOCK_SIZE);
        return parentInode->blocks[index+1]; //return this offset
    }
    else{
        return -ENOSPC; //if no more space in the blocks array
    }
    
    //if nothing returns above, return -1?
    return -1;
}

//returns the actual path
char* get_path_inode(char * disk, struct wfs_sb * superblock, char * path, int*skipbit){
   // printf("running helper function get_path_inode\n\n");
    printf("starting get_path_inode. Path: %s\n", path);
    if(strcmp("/", path) == 0){
        return disk + superblock->i_blocks_ptr; //return root 
    }

    // setup//////////////////////
    char * pathCopy = malloc(strlen(path) + 1);
    strcpy(pathCopy, path);
    char * token = strtok(pathCopy, "/"); //first token is expected file or dir in root dir
    int found = 0;
    struct wfs_inode currentInode;
    char* currentInodeAddr = disk + superblock->i_blocks_ptr; //starting from root inode 
    struct wfs_dentry dirEntry;
    struct wfs_dentry nullEntry = {0};
    /////////////////////////////////////////////////////////////// 

    printf("fault1\n");
    off_t startBlock = 0;   
    off_t currOffset = 0;
    //find target file/dir  
    while(token){ 
        printf("file to find: %s       \n ", token);
        printf("1.1\n");
        //find directory entry of currentInode
        memcpy(&currentInode, currentInodeAddr, sizeof(struct wfs_inode)); 
        printf("1.2\n");
        for (int i = 0; i < N_BLOCKS; i++){
            if(currentInode.blocks[i]){ 
                printf("1.3\n");
                startBlock = currentInode.blocks[i];
                currOffset = startBlock;
                while(currOffset < startBlock + BLOCK_SIZE){
                    if(memcmp(disk + currOffset, &nullEntry, sizeof(struct wfs_dentry)) != 0){
                        memcpy(&dirEntry, disk + currOffset, sizeof(struct wfs_dentry));
                        if (strncmp(dirEntry.name, token, strlen(token)) == 0){ //strcmp(dirEntry.name, token) == 0
                            found = 1;
                            currentInodeAddr = disk + superblock->i_blocks_ptr + (dirEntry.num * BLOCK_SIZE);
                            break;
                        }
                    }
                    currOffset = currOffset + sizeof(struct wfs_dentry);
                }
                if(found){
                    break;
                }
            }
        }

        if(!found){
            printf("ERROR: file not found. Path: %s\n", path);
            failedgetparent = -1;
            return &failedgetparent;
        }

        found = 0;
        token = strtok(NULL, "/");
    }
    printf("fault2\n");
    printf("DONE: get path inode finished\n\n");  
    return currentInodeAddr;
}

//Return file attributes. fill stbuf struct
static int wfs_getattr(const char *path, struct stat *stbuf) {
    printf("WFS_GETATTR: using wfs_getattr\n");
    //setup///////////////////////////////////////

    //create path copy since strtok does not work on const char *path
    char *pathCopy = malloc(strlen(path) + 1); 
    strcpy(pathCopy, path);
    pathCopy[strlen(path)] = '\0';

    printf("getattr1: %s\n", path);
    //ptr to start of superblock
    struct wfs_sb superblock;
    memcpy(&superblock, disk, sizeof(struct wfs_sb));
    //struct wfs_sb * ptr = &superblock;
    int skipbit = 0;
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////

printf("getattr2: %s\n", path);
    //get the target inode & update time
    char *tI = get_path_inode(disk, &superblock, pathCopy, &skipbit); 
    if(failedgetparent == -1){
        failedgetparent = 0;
        return -ENOENT;
    }
    struct wfs_inode targetInode;
    memcpy(&targetInode, tI, sizeof(struct wfs_inode));
    time_t current_time;
    time(&current_time);
    targetInode.atim = current_time; //only accessed, so only changing access time
    memcpy(tI, &targetInode, sizeof(struct wfs_inode));

printf("getattr3: %s\n", path);
    //fill in stbuf struct with found target inode
    stbuf->st_uid = targetInode.uid;
    stbuf->st_gid = targetInode.gid;
    stbuf->st_atime = targetInode.atim;
    stbuf->st_mtime = targetInode.mtim;
    stbuf->st_mode = targetInode.mode;
    stbuf->st_size = targetInode.size;

    printf("DONE: getattr finished\n\n");     
    return 0; // Return 0 on success
}

//Make a file 
static int wfs_mknod(const char* path, mode_t mode, dev_t rdev){
    printf("WFS_MKNOD: using mknod\n");

//setup ///////////////////////////////
    //create path copy since strtok does not work on const char *path
    char *pathCopy = malloc(strlen(path) + 1); 
    strcpy(pathCopy, path);
    pathCopy[strlen(path)] = '\0';

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
    newDirInode.size = 0; //BLOCK_SIZE
    mode = __S_IFREG | S_IRWXO;
    newDirInode.mode = mode;
    newDirInode.uid = S_ISUID;
    newDirInode.gid = S_ISGID;
    time_t current_time;
    time(&current_time);
    newDirInode.atim = current_time; 
    newDirInode.mtim = current_time; 
    newDirInode.ctim = current_time;
    memset(newDirInode.blocks, 0, sizeof(off_t) * N_BLOCKS);
    
    //(debugging purposes: indirect logic went here. Obv not needed tho)
   
    // memcpy((off_t)ptr + ptr->i_blocks_ptr + (off_t)(index * BLOCK_SIZE), &newFile, sizeof(struct wfs_inode));
    //2. update parent directory with our new directory. (updating data bitmap and datablocks)
    int dsize = ptr->num_data_blocks / 32;
    int dbitmap[dsize];
    memcpy(dbitmap, disk + ptr->d_bitmap_ptr, sizeof(int) * dsize);

    //ERROR CASE (if valid space not found within the data block consisting of inode and dirEntries): Implement if needed

    //write back to disk
    //memcpy((off_t)ptr + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);
    //update data blocks with dir entry, write dir entry to data block in disk
    char * name = basename(pathCopy);
    struct wfs_dentry dirEntry;
    memset(dirEntry.name, 0, sizeof(char) * MAX_NAME);
    strncpy(dirEntry.name, name, strlen(name)); //strcpy(dirEntry.name, name); 
    dirEntry.num = newDirInode.num;
    //int dbitmapChange = 0;
    //memcpy((off_t)ptr + ptr->d_blocks_ptr + (off_t)(dindex * BLOCK_SIZE), &dirEntry, sizeof(struct wfs_dentry));
    
    //add dir entry to parent inode
    char * modifiedPath = dirname(pathCopy);
    printf("WFS_MKDIR: path being passed: %s\n", pathCopy);
    printf("WFS_MKDIR: directory of path: %s\n", modifiedPath);
    char *pI = get_path_inode(disk, &superblock, modifiedPath, &skipbit);
    if(failedgetparent == -1){ //failure case
        failedgetparent = 0;    
        return -ENOENT;
    }    
    struct wfs_inode parentInode; //acquiring parent inode
    memcpy(&parentInode, pI, sizeof(struct wfs_inode));

    //checking file or dir does not already exist
    struct wfs_dentry existingEntry; 
    off_t startBlock2 = 0;
    off_t currOffset2 = startBlock2;
    struct wfs_dentry nullEntry = {0};
    for(int i = 0; i < N_BLOCKS; i++){ 
        if(parentInode.blocks[i]){
            startBlock2 = parentInode.blocks[i];
            currOffset2 = startBlock2;
            while(currOffset2 < startBlock2 + BLOCK_SIZE){
                if(memcmp(disk + currOffset2, &nullEntry, sizeof(struct wfs_dentry)) != 0){
                    memcpy(&existingEntry, disk + currOffset2, sizeof(struct wfs_dentry));
                    if(strncmp(existingEntry.name, dirEntry.name, strlen(dirEntry.name)) == 0){ //strcmp(existingEntry.name, dirEntry.name) == 0
                        return -EEXIST;
                    }     
                }   
                currOffset2 = currOffset2 + sizeof(struct wfs_dentry);        
            }
        }
    }

    //find spot for direntry to go
    off_t newDirEntry = getValidSpace(&parentInode, &superblock, dbitmap);
    if(newDirEntry == -ENOSPC){ //if exact spot was not found 
        printf("ERROR: failed finding new space for direntry\n\n");
        return -ENOSPC;
    }
    else if(newDirEntry == -1){
        printf("ERROR: something went wrong in getValidSpace...?");
        return 1;
    }
    
    memcpy(disk + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size); //writing inode bitmap
    memcpy(disk + ptr->i_blocks_ptr + (index * BLOCK_SIZE), &newDirInode, sizeof(struct wfs_inode)); //writing new directory inode 
    memcpy(disk + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize); //writing data bitmap
    memcpy(disk + newDirEntry, &dirEntry, sizeof(struct wfs_dentry)); //writing new dir entry to datablocks
    memcpy(pI, &parentInode, sizeof(struct wfs_inode)); //writing parent inode

    printf("WFS_MKNOD: COMPLETE\n\n\n");
    return 0;
    //INDIRECT ALLOCATION LOGIC: USE FOR LATER/////////////////////////////
    // int dindex = -1;
    // int drowIndex = 0;
    // int dcolIndex = 0;
    //  int d2size = ptr->num_data_blocks / 32; dunno if i need this and 2 lines below
    //  int dbitmap[d2size];
    //  memcpy(dbitmap, disk + ptr->d_bitmap_ptr, sizeof(int) * d2size);
    // for(int i = 0; i < ptr->num_data_blocks; i++){
    //     //checking if ith bit is 0. Set to 1 if it is
    //     if(i%32 == 0 && i != 0){
    //         drowIndex++;
    //         dcolIndex = 0;
    //     }
    //     if(!(dbitmap[drowIndex] & (1 << dcolIndex))){
    //         dindex = i;
    //         dbitmap[drowIndex] |= (1 << dcolIndex);
    //         break;
    //     }
    //     dcolIndex++;
    // }
    // if(dindex == -1){
    //     return -ENOSPC;
    // }

    // newDirInode.blocks[IND_BLOCK] = ptr->d_blocks_ptr + (dindex * BLOCK_SIZE);
///////////////////////////////////////////////////////////////////
}

//Create a directory with the given name. The directory permissions are encoded in mode. 
//See mkdir(2) for details. This function is needed for any reasonable read/write filesystem.
static int wfs_mkdir(const char* path, mode_t mode){
    printf("WFS_MKDIR: using mkdir\n");

//setup ///////////////////////////////
    //create path copy since strtok does not work on const char *path
    char *pathCopy = malloc(strlen(path) + 1); 
    strcpy(pathCopy, path);
    pathCopy[strlen(path)] = '\0';

    //ptr to start of superblock
    struct wfs_sb superblock;
    memcpy(&superblock, disk, sizeof(struct wfs_sb));
    struct wfs_sb * ptr = &superblock;
    int skipbit = 0;
    ///////////////////////////////////////////

    printf("mkdir1\n");
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

printf("mkdir2\n");
    //write back to disk 
    //memcpy((off_t)ptr + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size);

    //create and add inode
    struct wfs_inode newDirInode;
    newDirInode.num = index; 
    newDirInode.size = BLOCK_SIZE; 
    mode = __S_IFDIR | S_IRWXO;
    newDirInode.mode = mode;
    newDirInode.uid = S_ISUID;
    newDirInode.gid = S_ISGID;
    time_t current_time;
    time(&current_time);
    newDirInode.atim = current_time; 
    newDirInode.mtim = current_time; 
    newDirInode.ctim = current_time;
    memset(newDirInode.blocks, 0, sizeof(off_t) * N_BLOCKS);
    
    //(debugging purposes: indirect logic went here. Obv not needed tho)
   
    // memcpy((off_t)ptr + ptr->i_blocks_ptr + (off_t)(index * BLOCK_SIZE), &newFile, sizeof(struct wfs_inode));
printf("mkdir3\n");
    //2. update parent directory with our new directory. (updating data bitmap and datablocks)
    int dsize = ptr->num_data_blocks / 32;
    int dbitmap[dsize];
    memcpy(dbitmap, disk + ptr->d_bitmap_ptr, sizeof(int) * dsize);

    //ERROR CASE (if valid space not found within the data block consisting of inode and dirEntries): Implement if needed

    //write back to disk
    //memcpy((off_t)ptr + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);
printf("mkdir4\n");
    //update data blocks with dir entry, write dir entry to data block in disk
    char * name = basename(pathCopy);
    struct wfs_dentry dirEntry;
    memset(dirEntry.name, 0, sizeof(char) * MAX_NAME);
    strncpy(dirEntry.name, name, strlen(name)); //strcpy(dirEntry.name, name); 
    dirEntry.num = newDirInode.num;
    //int dbitmapChange = 0;
    //memcpy((off_t)ptr + ptr->d_blocks_ptr + (off_t)(dindex * BLOCK_SIZE), &dirEntry, sizeof(struct wfs_dentry));
    
    //add dir entry to parent inode
    char * modifiedPath = dirname(pathCopy);
    printf("WFS_MKDIR: path being passed: %s\n", pathCopy);
    printf("WFS_MKDIR: directory of path: %s\n", modifiedPath);
    char *pI = get_path_inode(disk, &superblock, modifiedPath, &skipbit);
    if(failedgetparent == -1){ //failure case
        failedgetparent = 0;    
        return -ENOENT;
    }    
    printf("mkdir5\n");
    struct wfs_inode parentInode; //acquiring parent inode
    memcpy(&parentInode, pI, sizeof(struct wfs_inode));

    //checking file or dir does not already exist
    struct wfs_dentry existingEntry; 
    off_t startBlock2 = 0;
    off_t currOffset2 = startBlock2;
    struct wfs_dentry nullEntry = {0};
    for(int i = 0; i < N_BLOCKS; i++){ 
        if(parentInode.blocks[i]){
            startBlock2 = parentInode.blocks[i];
            currOffset2 = startBlock2;
            while(currOffset2 < startBlock2 + BLOCK_SIZE){
                if(memcmp(disk + currOffset2, &nullEntry, sizeof(struct wfs_dentry)) != 0){
                    memcpy(&existingEntry, disk + currOffset2, sizeof(struct wfs_dentry));
                    if(strncmp(existingEntry.name, dirEntry.name, strlen(dirEntry.name)) == 0){ //strcmp(existingEntry.name, dirEntry.name) == 0
                        return -EEXIST;
                    }     
                }   
                currOffset2 = currOffset2 + sizeof(struct wfs_dentry);        
            }
        }
    }

    //find spot for direntry to go
    off_t newDirEntry = getValidSpace(&parentInode, &superblock, dbitmap);
    if(newDirEntry == -ENOSPC){ //if exact spot was not found 
        printf("ERROR: failed finding new space for direntry\n\n");
        return -ENOSPC;
    }
    else if(newDirEntry == -1){
        printf("ERROR: something went wrong in getValidSpace...?");
        return 1;
    }

printf("mkdir6\n");
    memcpy(disk + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size); //writing inode bitmap
    memcpy(disk + ptr->i_blocks_ptr + (index * BLOCK_SIZE), &newDirInode, sizeof(struct wfs_inode)); //writing new directory inode 
    memcpy(disk + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize); //writing data bitmap
    memcpy(disk + newDirEntry, &dirEntry, sizeof(struct wfs_dentry)); //writing new dir entry to datablocks
    memcpy(pI, &parentInode, sizeof(struct wfs_inode)); //writing parent inode

    printf("WFS_MKDIR: COMPLETE\n\n\n");
    return 0;
}

//Remove (delete) the given file, symbolic link, hard link, or special node. 
//Note that if you support hard links, unlink only deletes the data when the last hard link is removed. 
static int wfs_unlink(const char* path){
//setup////////////
    //////////////////////
    printf("WFS_UNLINK: starting unlink\n");
    printf("path: %s\n", path);
    struct wfs_sb superblock; 
    memcpy(&superblock, disk, sizeof(struct wfs_sb));
    struct wfs_sb * ptr = &superblock;
    int skipbit = 0;
    char *pathCopy = malloc(strlen(path) + 1); 
    strcpy(pathCopy, path);
    printf("pathCopy: %s\n", pathCopy);  
    char * modifiedPath = dirname(pathCopy);
    char* a1 = get_path_inode(disk, ptr, modifiedPath, &skipbit);
    if(failedgetparent == -1){ //failure case
        failedgetparent = 0;    
        return -ENOENT;
    }  
    struct wfs_inode parentInode;
    memcpy(&parentInode, a1, sizeof(struct wfs_inode));
    char * pathCopy2 = malloc(strlen(path) + 1);
    strcpy(pathCopy2, path);
    char * name = basename(pathCopy2); 
    int found = 0;
    struct wfs_dentry dEntry;
    int dsize = ptr->num_data_blocks / 32;
    int dbitmap[dsize];
    int startBlock2Index = -1;
    memcpy(dbitmap, disk + ptr->d_bitmap_ptr, sizeof(int) * dsize); 
    //////////////////////////// //////////////////////////// ////////////////////////////

    //find directory inode.  
    off_t startBlock2 = 0;
    off_t currOffset2 = startBlock2;
    struct wfs_dentry nullEntry = {0};
    for(int i = 0; i < N_BLOCKS; i++){ 
        if(parentInode.blocks[i]){
            startBlock2 = parentInode.blocks[i];
            startBlock2Index = i;
            currOffset2 = startBlock2;
            while(currOffset2 < startBlock2 + BLOCK_SIZE){
                if(memcmp(disk + currOffset2, &nullEntry, sizeof(struct wfs_dentry)) != 0){
                    memcpy(&dEntry, disk + currOffset2, sizeof(struct wfs_dentry));
                    printf("dEntry.name: %s\n", dEntry.name);
                    printf("name: %s\n\n", name);
                    if(strcmp(dEntry.name, name) == 0){
                        found = 1;
                        break;
                    }     
                }   
                currOffset2 = currOffset2 + sizeof(struct wfs_dentry);        
            }
            if(found){
                break;
            }
        }
    }

    if(!found){
        printf("ERROR: dir entry not found");
        return 1;
    }

    struct wfs_inode currInode;
    char *currInodeAddr = disk + ptr->i_blocks_ptr + (dEntry.num * BLOCK_SIZE);
    memcpy(&currInode, currInodeAddr, sizeof(struct wfs_inode));

    //2. clearing data in datablocks for the file

    //traverse blocks array in inode up until IND_BLOCK, clear defined blocks. Update dbitmap as you go
    for(int i = 0; i < IND_BLOCK; i++){
        if(currInode.blocks[i]){
            memset(disk + currInode.blocks[i], 0, sizeof(char) * BLOCK_SIZE);
            int dbIndex = currInode.blocks[i] / BLOCK_SIZE;
            dbitmap[dbIndex / 32] &= ~(1 << (dbIndex % 32)); //setting to 0. check if index is right
            currInode.blocks[i] = 0;
        }
    }

    //at IND_BLOCK, traverse through the block and clear blocks pointed to by valid offsets. Finally clear indirectBlock.
    //Update dbitmap as you go
    if(currInode.blocks[IND_BLOCK]){
        off_t indirectBlock = currInode.blocks[IND_BLOCK];
        off_t currOffset = indirectBlock;
        off_t dataBlock;
        while(currOffset < indirectBlock + BLOCK_SIZE){
            memcpy(&dataBlock, disk + currOffset, sizeof(off_t));
            if(dataBlock){
                memset(disk + dataBlock, 0, sizeof(char) * BLOCK_SIZE);
                int dbIndex = dataBlock / BLOCK_SIZE;
                dbitmap[dbIndex / 32] &= ~(1 << (dbIndex % 32)); //setting to 0. check if index is right
            }
            currOffset = currOffset + sizeof(off_t);
        }
        
        memset(disk + indirectBlock, 0, sizeof(char) * BLOCK_SIZE);
        int dbIndex = indirectBlock / BLOCK_SIZE;
        dbitmap[dbIndex / 32] &= ~(1 << (dbIndex % 32)); //setting to 0. check if index is right
        currInode.blocks[IND_BLOCK] = 0;
    }


    //3. Updating parent inode (clearing dir entry from it)

    //find dir entry. Clear the data at that addr. Update dbitmap if neccessary (if block becomes fully free)
    memset(disk + currOffset2, 0, sizeof(struct wfs_dentry));
    char blockEmpty[BLOCK_SIZE];
    memset(blockEmpty, 0, sizeof(char) * BLOCK_SIZE);
    if(memcmp(disk + startBlock2, blockEmpty, sizeof(char) * BLOCK_SIZE) == 0){ //if block that dir entry was in is now empty
        int db2index = startBlock2 / BLOCK_SIZE;
        dbitmap[db2index / 32] &= ~(1 << (db2index % 32)); //setting to 0. check if index is right
        parentInode.blocks[startBlock2Index] = 0;
    }
    
    //updating parent inode
    memcpy(disk + ptr->i_blocks_ptr + (BLOCK_SIZE * parentInode.num), &parentInode, sizeof(struct wfs_inode)); //updating parent inode

    //update inode and inode bitmap of current file
    memset(currInodeAddr, 0, sizeof(struct wfs_inode)); //its inode itself
    int size = ptr->num_inodes / 32;
    int bitmap[size];
    memcpy(bitmap, disk + ptr->i_bitmap_ptr, sizeof(int) * size); //current dir 
    bitmap[currInode.num/32] &= ~(1 << (currInode.num % 32)); //setting to 0. CHECK if correct
    memcpy(disk + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size);

    //update data bitmap
    memcpy(disk + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);

    printf("WFS_UNLINK: DONE\n\n");
    return 0;
}

//Remove the given directory. This should succeed only if the directory is empty (except for "." and "..").
//TODO: confirm that this is right. Update and fix as needed
static int wfs_rmdir(const char* path){
    //setup////////////
    //////////////////////
    printf("WFS_UNLINK: starting unlink\n");
    printf("path: %s\n", path);
    struct wfs_sb superblock; 
    memcpy(&superblock, disk, sizeof(struct wfs_sb));
    struct wfs_sb * ptr = &superblock;
    int skipbit = 0;
    char *pathCopy = malloc(strlen(path) + 1); 
    strcpy(pathCopy, path);
    printf("pathCopy: %s\n", pathCopy);  
    char * modifiedPath = dirname(pathCopy);
    char* a1 = get_path_inode(disk, ptr, modifiedPath, &skipbit);
    if(failedgetparent == -1){ //failure case
        failedgetparent = 0;    
        return -ENOENT;
    }  
    struct wfs_inode parentInode;
    memcpy(&parentInode, a1, sizeof(struct wfs_inode));
    char * pathCopy2 = malloc(strlen(path) + 1);
    strcpy(pathCopy2, path);
    char * name = basename(pathCopy2); 
    int found = 0;
    struct wfs_dentry dEntry;
    int dsize = ptr->num_data_blocks / 32;
    int dbitmap[dsize];
    int startBlock2Index = -1;
    memcpy(dbitmap, disk + ptr->d_bitmap_ptr, sizeof(int) * dsize); 
    //////////////////////////// //////////////////////////// ////////////////////////////

    //find directory inode.  
    off_t startBlock2 = 0;
    off_t currOffset2 = startBlock2;
    struct wfs_dentry nullEntry = {0};
    for(int i = 0; i < N_BLOCKS; i++){ 
        if(parentInode.blocks[i]){
            startBlock2 = parentInode.blocks[i];
            startBlock2Index = i;
            currOffset2 = startBlock2;
            while(currOffset2 < startBlock2 + BLOCK_SIZE){
                if(memcmp(disk + currOffset2, &nullEntry, sizeof(struct wfs_dentry)) != 0){
                    memcpy(&dEntry, disk + currOffset2, sizeof(struct wfs_dentry));
                    printf("dEntry.name: %s\n", dEntry.name);
                    printf("name: %s\n\n", name);
                    if(strcmp(dEntry.name, name) == 0){
                        found = 1;
                        break;
                    }     
                }   
                currOffset2 = currOffset2 + sizeof(struct wfs_dentry);        
            }
            if(found){
                break;
            }
        }
    }

    if(!found){
        printf("ERROR: dir entry not found");
        return 1;
    }

    struct wfs_inode currInode;
    char *currInodeAddr = disk + ptr->i_blocks_ptr + (dEntry.num * BLOCK_SIZE);
    memcpy(&currInode, currInodeAddr, sizeof(struct wfs_inode));

    // //2. clearing data in datablocks for the file

    // //traverse blocks array in inode up until IND_BLOCK, clear defined blocks. Update dbitmap as you go
    // for(int i = 0; i < IND_BLOCK; i++){
    //     if(currInode.blocks[i]){
    //         memset(disk + currInode.blocks[i], 0, sizeof(char) * BLOCK_SIZE);
    //         int dbIndex = currInode.blocks[i] / BLOCK_SIZE;
    //         dbitmap[dbIndex / 32] &= ~(1 << (dbIndex % 32)); //setting to 0. check if index is right
    //         currInode.blocks[i] = 0;
    //     }
    // }

    // //at IND_BLOCK, traverse through the block and clear blocks pointed to by valid offsets. Finally clear indirectBlock.
    // //Update dbitmap as you go
    // if(currInode.blocks[IND_BLOCK]){
    //     off_t indirectBlock = currInode.blocks[IND_BLOCK];
    //     off_t currOffset = indirectBlock;
    //     off_t dataBlock;
    //     while(currOffset < indirectBlock + BLOCK_SIZE){
    //         memcpy(&dataBlock, disk + currOffset, sizeof(off_t));
    //         if(dataBlock){
    //             memset(disk + dataBlock, 0, sizeof(char) * BLOCK_SIZE);
    //             int dbIndex = dataBlock / BLOCK_SIZE;
    //             dbitmap[dbIndex / 32] &= ~(1 << (dbIndex % 32)); //setting to 0. check if index is right
    //         }
    //         currOffset = currOffset + sizeof(off_t);
    //     }
        
    //     memset(disk + indirectBlock, 0, sizeof(char) * BLOCK_SIZE);
    //     int dbIndex = indirectBlock / BLOCK_SIZE;
    //     dbitmap[dbIndex / 32] &= ~(1 << (dbIndex % 32)); //setting to 0. check if index is right
    //     currInode.blocks[IND_BLOCK] = 0;
    // }

    //2. (for rmdir) check if dir empty
    for(int i = 0; i < N_BLOCKS; i++){
        if(currInode.blocks[i]){
            printf("ERROR: dir not empty\n");
            return 1;
        }
    }


    //3. Updating parent inode (clearing dir entry from it)

    //find dir entry. Clear the data at that addr. Update dbitmap if neccessary (if block becomes fully free)
    memset(disk + currOffset2, 0, sizeof(struct wfs_dentry));
    char blockEmpty[BLOCK_SIZE];
    memset(blockEmpty, 0, sizeof(char) * BLOCK_SIZE);
    if(memcmp(disk + startBlock2, blockEmpty, sizeof(char) * BLOCK_SIZE) == 0){ //if block that dir entry was in is now empty
        int db2index = startBlock2 / BLOCK_SIZE;
        dbitmap[db2index / 32] &= ~(1 << (db2index % 32)); //setting to 0. check if index is right
        parentInode.blocks[startBlock2Index] = 0;
    }
    
    //updating parent inode
    memcpy(disk + ptr->i_blocks_ptr + (BLOCK_SIZE * parentInode.num), &parentInode, sizeof(struct wfs_inode)); //updating parent inode

    //update inode and inode bitmap of current file
    memset(currInodeAddr, 0, sizeof(struct wfs_inode)); //its inode itself
    int size = ptr->num_inodes / 32;
    int bitmap[size];
    memcpy(bitmap, disk + ptr->i_bitmap_ptr, sizeof(int) * size); //current dir 
    bitmap[currInode.num/32] &= ~(1 << (currInode.num % 32)); //setting to 0. CHECK if correct
    memcpy(disk + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size);

    //update data bitmap
    memcpy(disk + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);

    printf("WFS_UNLINK: DONE\n\n");
    return 0;
    // printf("RMDIR: Start\n");
    // //setup////////////
    // ////////////////////////
    // struct wfs_sb superblock; 
    // memcpy(&superblock, disk, sizeof(struct wfs_sb));
    // struct wfs_sb * ptr = &superblock;
    // int skipbit = 0;
    // char *pathCopy = malloc(strlen(path) + 1); 
    // strcpy(pathCopy, path);
    // pathCopy[strlen(path)] = '\0';    
    // char * modifiedPath = dirname(pathCopy);
    // printf("rmdir2\n");
    // char* a1 = get_path_inode(disk, ptr, modifiedPath, &skipbit);
    // struct wfs_inode parentInode;
    // memcpy(&parentInode, a1, sizeof(struct wfs_inode));
    // char * name = basename(pathCopy);
    // int found = -1;
    // struct wfs_dentry dEntry;
    // //////////////////////////// //////////////////////////// ////////////////////////////

    // printf("rmdir3\n");
    // //find directory inode.
    // for(int i = 0; i < N_BLOCKS; i++){
    //     if(parentInode.blocks[i]){
    //         char *inodeOffset = disk + parentInode.blocks[i];
    //         memcpy(&dEntry, inodeOffset, sizeof(struct wfs_dentry));
    //         if(strcmp(dEntry.name, name) == 0){
    //             found = i;
    //             break;
    //         }
    //     }
    // }

    // if(found == -1){
    //     printf("ERROR: dir entry not found");
    //     return -ENOENT;
    // }

    // printf("rmdir4\n");
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

    // //in case it wasn't already, set the value to -1 in dirEntryOffsets (we don't need the spot anymore)
    // dirEntryOffsets[currInode.num] = -1;

    // //within parent dir, check if its empty
    // int emptybit = 0;
    //    for(int i = 0; i < N_BLOCKS; i++){
    //     if(parentInode.blocks[i]){
    //         emptybit = 1;
    //         break;
    //     }
    // }

    // printf("rmdir5\n");
    // //clear the space it takes in the inodes
    // memset(currInodeAddr, 0, sizeof(struct wfs_inode)); //its inode itself
    // memset(disk + parentInode.blocks[found], 0, sizeof(struct wfs_dentry)); //removing directory entry of it in parent
    // parentInode.blocks[found] = 0;
    // memcpy(disk + ptr->i_blocks_ptr + (BLOCK_SIZE * parentInode.num), &parentInode, sizeof(struct wfs_inode)); //updating parent inode

    // //update indode bitmap
    // int size = ptr->num_inodes / 32;
    // int bitmap[size];
    // memcpy(bitmap, disk + ptr->i_bitmap_ptr, sizeof(int) * size); //current dir 
    // bitmap[currInode.num/32] &= ~(1 << (currInode.num % 32)); //setting to 0. CHECK if correct
    // memcpy(disk + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size);

    // printf("rmdir6\n");
    // //update data bitmap
    // if(!emptybit){
    //     off_t diff = dirEntryOffsets[parentInode.num] - ptr->d_blocks_ptr;
    //     int dbIndex = (diff/BLOCK_SIZE) - 1;
    //     int dsize = ptr->num_data_blocks / 32;
    //     int dbitmap[dsize];
    //     memcpy(dbitmap, disk + ptr->d_bitmap_ptr, sizeof(int) * dsize); //for parent
    //     dbitmap[dbIndex/32] &= ~(1 << (dbIndex % 32)); //setting to 0, CHECK
    //     memcpy(disk + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);
    //     dirEntryOffsets[parentInode.num] = -1;
    // }

    // printf("RMDIR: End\n");
    //return 0;
}

//Read sizebytes from the given file into the buffer buf, beginning offset bytes into the file. See read(2) for full details. 
//Returns the number of bytes transferred, or 0 if offset was at or beyond the end of the file. Required for any sensible filesystem.
static int wfs_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi){
    ///////////////////////////////////////////////
    char * pathCopy = malloc(strlen(path) + 1);
    strcpy(pathCopy, path);
    int skipbit = 0;
    struct wfs_sb superblock;
    memcpy(&superblock, disk, sizeof(struct wfs_sb));
    char *fileInodeAddr = get_path_inode(disk, &superblock, pathCopy, &skipbit);
    struct wfs_inode fileInode;
    memcpy(&fileInode, fileInodeAddr, sizeof(struct wfs_inode));
    ////////////////////////////////////////////////

    //check if offset is greater than the file itself
    if(offset >= fileInode.size){
        return 0;
    }

    //place offset in correct position in our data blocks
    int startBlockIndex = offset / BLOCK_SIZE;
    off_t startOffset = offset - (BLOCK_SIZE * startBlockIndex); //where specifically offset goes
    off_t offsetCopy = offset;
    
    if(startBlockIndex <= IND_BLOCK){
        //starting from startOffset, read byte by byte into the buffer. Make sure to transfer to indirect logic when IND_BLOCK
        //index is reached. Stop the entire while loop once bytes read == size
        off_t bytesRead = 0;
        while(bytesRead < size && offsetCopy < fileInode.size){
            if(startOffset % BLOCK_SIZE == 0 && bytesRead > 0){
                startBlockIndex++;
                startOffset = 0;
            }
            if(startBlockIndex == IND_BLOCK){
                break;
            }
            else{
                memcpy(buf + bytesRead, disk + fileInode.blocks[startBlockIndex] + startOffset, sizeof(char));
            }
            bytesRead++;
            startOffset++;
            offsetCopy++;
        }
        
        off_t currIndOffset = 0;
        off_t indBlock;
        if(startBlockIndex == IND_BLOCK && fileInode.blocks[IND_BLOCK] != 0){
            for(int i = 0; i < BLOCK_SIZE / sizeof(off_t); i++){ //BLOCK_SIZE
                memcpy(&indBlock, disk + fileInode.blocks[startBlockIndex] + currIndOffset, sizeof(off_t));
                if(indBlock){
                    int j = 0;
                    while(bytesRead < size && offsetCopy < fileInode.size && j < BLOCK_SIZE){
                        memcpy(buf, disk + indBlock + j, sizeof(char));
                        bytesRead++;
                        offsetCopy++;
                        j++;
                    } 
                }
                currIndOffset = currIndOffset + sizeof(off_t);
            }
        }
        return bytesRead;
    }
    else{
        //within ind block, calculate which block (off_t value) to access
        int startIndexFromInd = startBlockIndex - IND_BLOCK;
        off_t 

        //precisely put your starting point there , and read byte by byte until complete
        
    }
    return 0;
}

//As for read above, except that it can't return 0.
//Q: where should offset go? invalid offset? 
static int wfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi){
    ///////////////////////////////////////////////
    // char * pathCopy = malloc(strlen(path) + 1);
    // strcpy(pathCopy, path);
    // int skipbit = 0;
    // struct wfs_sb superblock;
    // memcpy(&superblock, disk, sizeof(struct wfs_sb));
    // char *fileInodeAddr = get_path_inode(disk, &superblock, pathCopy, &skipbit);
    // struct wfs_inode fileInode;
    // memcpy(&fileInode, fileInodeAddr, sizeof(struct wfs_inode));
    // ////////////////////////////////////////////////

    // //place offset in correct position in our data blocks
    // int startBlockIndex = offset / BLOCK_SIZE;
    // off_t startOffset = offset - (BLOCK_SIZE * startBlockIndex); //where specifically offset goes
    // off_t offsetCopy = offset;
    // off_t bytesWritten = 0;
    // int index = 0;
    
    // //for loop 
    // //starting from startOffset, read byte by byte into the buffer. Make sure to transfer to indirect logic when IND_BLOCK
    // //index is reached. Stop the entire while loop once bytes read == size
    // if(startBlockIndex >= IND_BLOCK){
    //     while(bytesRead < size && offsetCopy < fileInode.size){
    //         if(startOffset % BLOCK_SIZE == 0 && bytesRead > 0){
    //             startBlockIndex++;
    //             startOffset = 0;
    //         }
    //         if(startBlockIndex == IND_BLOCK){
    //             break;
    //         }
    //         else{
    //             memcpy(buf + bytesRead, disk + fileInode.blocks[startBlockIndex] + startOffset, sizeof(char));
    //         }
    //         bytesRead++;
    //         startOffset++;
    //         offsetCopy++;
    //     }
    // }


  

    //begin writing byte by byte from this specific offset. Update dbitmap as you go. Update inode blocks offset as well.
    //do this until IND_BLOCK

    //check if IND_BLOCK is needed -- then Allocate block if yes. 
    //for each "off_t" in our indirect. if it is defined simply write byte by byte. 
    //If its not allocate a block, update dbitmap and ind block, and write to that block
    
    //memcpy data bitmaps, curr inode
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
