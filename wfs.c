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

//to ask: your doubts, then should we update access times every time we access, is off_t a primitive or pointer type
char * diskImage;
char failedgetparent = 0;
int fd;
char * disk;
off_t *dirEntryOffsets;

//gets a valid space within a datablock for a dir entry (grouping em all up)
off_t getValidSpace(struct wfs_inode *parentInode){
    if(dirEntryOffsets[parentInode->num] == -1){
        return -2;
    }

    //go to data block
    off_t startBlock = dirEntryOffsets[parentInode->num];
    
    //skip offset by inode bytes + traverse existing (if there) dirEntires and find open space
    off_t currOffset = startBlock;
    struct wfs_dentry entryToPlace = {0};

    while(memcmp(disk + currOffset, &entryToPlace, sizeof(struct wfs_dentry)) != 0 && currOffset < startBlock + BLOCK_SIZE){
        currOffset = currOffset + sizeof(struct wfs_dentry);
    }

    if(currOffset >= startBlock + BLOCK_SIZE){
        printf("ERROR: space not found for dirEntry");
        return -1;
    }

    //return offset you found the open space
    return currOffset;
}

//returns the actual path
//TODO: debug seg fault between 1.3 & 1.4!
char* get_path_inode(char * disk, struct wfs_sb * superblock, char * path, int*skipbit){
   // printf("running helper function get_path_inode\n\n");
    printf("starting get_path_inode\n");
    if(strcmp("/", path) == 0){
        return disk + superblock->i_blocks_ptr; //return root 
    }

    // setup//////////////////////
    char * token = strtok(path, "/"); //first token is expected file or dir in root dir
    int found = 0;
    struct wfs_inode currentInode;
    char* currentInodeAddr = disk + superblock->i_blocks_ptr; //starting from root inode 
    struct wfs_dentry dirEntry;
    /////////////////////////////////////////////////////////////// 

    printf("fault1\n");
    //find target file/dir
    while(token){ 
        //printf("file to find: %s       \n ", token);
        printf("1.1\n");
        //find directory entry of currentInode
        memcpy(&currentInode, currentInodeAddr, sizeof(struct wfs_inode)); 
        printf("1.2\n");
        for (int i = 0; i < N_BLOCKS; i++){
            if(currentInode.blocks[i]){ 
                printf("1.3\n");
                memcpy(&dirEntry, disk + currentInode.blocks[i], sizeof(struct wfs_dentry));
                printf("1.4\n");
                if (strcmp(dirEntry.name, token) == 0){
                    found = 1;
                    currentInodeAddr = disk + superblock->i_blocks_ptr + (dirEntry.num * BLOCK_SIZE);
                    break;
                }
                printf("1.5\n");
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
    printf("DONE: get path inode finished\n");  
    return currentInodeAddr;
}

//TODO:
// 1. add indirect logic to bottom 3
// 2. debug getattr, mknod, mkdir (maybe redo altogether)
// 3. implement other functions

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

    printf("DONE: getattr finished\n");     
    return 0; // Return 0 on success
}

//Make a file 
//TODO: update mknod to mkdir logic once mkdir works
static int wfs_mknod(const char* path, mode_t mode, dev_t rdev){
    //printf("making file using mknod\n");

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
    newFileInode.size = BLOCK_SIZE; //maybe set to 0
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
    char * name = basename(pathCopy);
    struct wfs_dentry dirEntry;
    strcpy(dirEntry.name, name); 
    dirEntry.num = newFileInode.num;

    //memcpy((off_t)ptr + ptr->d_blocks_ptr + (off_t)(dindex * BLOCK_SIZE), &dirEntry, sizeof(struct wfs_dentry));
    
    //add dir entry to parent inode
    char * modifiedPath = dirname(pathCopy); //eliminated last part of string so we have a valid argument for get_path_inode

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
            placed=1;
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
//TODO: debug till tests pass
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
    strcpy(dirEntry.name, name); 
    dirEntry.num = newDirInode.num;
    int dbitmapChange = 0;
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

    //find spot for direntry to go
    off_t newDirEntry = getValidSpace(&parentInode);
    if(newDirEntry == -1){ //if exact spot was not found 
        printf("ERROR: failed finding new space for direntry\n\n");
        return 1;
    }
    else if(newDirEntry == -2){ //new datablock for our dirEntry (adding first direntry for an inode)
        dbitmapChange = 1;
        //update data bitmap, find data block index, update dirEntryOffsets array
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
                    dirEntryOffsets[parentInode.num] = superblock.d_blocks_ptr + (off_t)(d2index * BLOCK_SIZE);
                    newDirEntry = dirEntryOffsets[parentInode.num];
                    dbitmap[d2rowIndex] |= (1 << d2colIndex);
                    break;
                }
                d2colIndex++;
            }

            //enforcing that we have found 1 free bit(data block) to place dir entry
            if(d2index == -1){
                return -ENOSPC;
            }
    }

    //checking file or dir does not already exist, adding new dir entry if possible
    struct wfs_dentry existingEntry; 
    int placed = 0;
    for(int i = 0; i < N_BLOCKS; i++){ 
        if(!parentInode.blocks[i] && !placed){
            parentInode.blocks[i] = newDirEntry; 
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

printf("mkdir6\n");
    memcpy(disk + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size); //writing inode bitmap
    memcpy(disk + ptr->i_blocks_ptr + (index * BLOCK_SIZE), &newDirInode, sizeof(struct wfs_inode)); //writing new directory inode 
    if(dbitmapChange){
        memcpy(disk + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize); //writing data bitmap
    }
    memcpy(disk + newDirEntry, &dirEntry, sizeof(struct wfs_dentry)); //writing new dir entry to datablocks
    memcpy(pI, &parentInode, sizeof(struct wfs_inode)); //writing parent inode

    printf("WFS_MKDIR: COMPLETE\n\n\n");
    return 0;
}

//Remove (delete) the given file, symbolic link, hard link, or special node. 
//Note that if you support hard links, unlink only deletes the data when the last hard link is removed. 
static int wfs_unlink(const char* path){

    return 0;
}

//Remove the given directory. This should succeed only if the directory is empty (except for "." and "..").
//update time with accesses?
static int wfs_rmdir(const char* path){
    //setup////////////
    ////////////////////////
    struct wfs_sb superblock; 
    memcpy(&superblock, disk, sizeof(struct wfs_sb));
    struct wfs_sb * ptr = &superblock;
    int skipbit = 0;
    char *pathCopy = malloc(strlen(path) + 1); 
    strcpy(pathCopy, path);
    pathCopy[strlen(path)] = '\0';    
    char * modifiedPath = dirname(pathCopy);
    char* a1 = get_path_inode(disk, ptr, modifiedPath, &skipbit);
    struct wfs_inode parentInode;
    memcpy(&parentInode, a1, sizeof(struct wfs_inode));
    char * name = basename(pathCopy);
    int found = -1;
    struct wfs_dentry dEntry;
    //////////////////////////// //////////////////////////// ////////////////////////////

    //find directory inode.
    for(int i = 0; i < N_BLOCKS; i++){
        if(parentInode.blocks[i]){
            char *inodeOffset = disk + parentInode.blocks[i];
            memcpy(&dEntry, inodeOffset, sizeof(struct wfs_dentry));
            if(strcmp(dEntry.name, name) == 0){
                found = i;
                break;
            }
        }
    }

    if(found == -1){
        printf("ERROR: dir entry not found");
        return -ENOENT;
    }

    struct wfs_inode currInode;
    char *currInodeAddr = disk + ptr->i_blocks_ptr + (dEntry.num * BLOCK_SIZE);
    memcpy(&currInode, currInodeAddr, sizeof(struct wfs_inode));

    //check that all blocks in array are empty. Throw error if not?
    for(int i = 0; i < N_BLOCKS; i++){
        if(currInode.blocks[i]){
            printf("ERROR: directory is not empty\n\n\n");
            return 1;
        }
    }

    //in case it wasn't already, set the value to -1 in dirEntryOffsets (we don't need the spot anymore)
    dirEntryOffsets[currInode.num] = -1;

    //within parent dir, check if its empty
    int emptybit = 0;
       for(int i = 0; i < N_BLOCKS; i++){
        if(parentInode.blocks[i]){
            emptybit = 1;
            break;
        }
    }
    if(!emptybit){
        
    }

    //clear the space it takes in the inodes
    memset(currInodeAddr, 0, sizeof(struct wfs_inode)); //its inode itself
    memset(disk + parentInode.blocks[found], 0, sizeof(struct wfs_dentry)); //removing directory entry of it in parent
    parentInode.blocks[found] = 0;
    memcpy(disk + ptr->i_blocks_ptr + (BLOCK_SIZE * parentInode.num), &parentInode, sizeof(struct wfs_inode)); //updating parent inode

    //update indode bitmap
    int size = ptr->num_inodes / 32;
    int bitmap[size];
    memcpy(bitmap, disk + ptr->i_bitmap_ptr, sizeof(int) * size); //current dir 
    bitmap[currInode.num/32] &= ~(1 << (currInode.num % 32)); //setting to 0. CHECK if correct
    memcpy(disk + ptr->i_bitmap_ptr, bitmap, sizeof(int) * size);

    //update data bitmap
    if(!emptybit){
        off_t diff = dirEntryOffsets[parentInode.num] - ptr->d_blocks_ptr;
        int dbIndex = (diff/BLOCK_SIZE) - 1;
        int dsize = ptr->num_data_blocks / 32;
        int dbitmap[dsize];
        memcpy(dbitmap, disk + ptr->d_bitmap_ptr, sizeof(int) * dsize); //for parent
        dbitmap[dbIndex/32] &= ~(1 << (dbIndex % 32)); //setting to 0, CHECK
        memcpy(disk + ptr->d_bitmap_ptr, dbitmap, sizeof(int) * dsize);
        dirEntryOffsets[parentInode.num] = -1;
    }

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
    struct wfs_sb superblock;
    memcpy(&superblock, disk, sizeof(struct wfs_sb));

    dirEntryOffsets = malloc(sizeof(off_t) * superblock.num_inodes);
    memset(dirEntryOffsets, -1, sizeof(off_t) * superblock.num_inodes);

    return fuse_main(argc - 1, argv + 1, &ops, NULL);
}
