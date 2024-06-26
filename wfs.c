#include "wfs.h"
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

// parse the path
// get to inode you need to be at
// get or create at the point
// need a traversal function
FILE* disk_img;
char* disk_path;
struct wfs_sb superblock;
/*
    bit operation helper functions
*/
// 1U is unsigned int
void setBit(unsigned int* bitmap, int pos) {
    *bitmap |= (1U << pos);
}

void clearBit(unsigned int* bitmap, int pos) {
    *bitmap &= ~(1U << pos);
}

int getBitValue(unsigned int* bitmap, int pos) {
    return (*bitmap >> pos) & 1U;
}
/*
    helper method that will search for and return the inode idx of the child we are looking for in a directory
    this function assumes disk_img is an open file
    returns the num associated with this child or -1 if none is found (num will point to the idx in the inode bitmap)
*/
int findChild(int parentInodeIdx, char* child){
    // find inode
    struct wfs_inode parentDirectoryInode;
    fseek(disk_img, superblock.i_blocks_ptr + (parentInodeIdx * BLOCK_SIZE), SEEK_SET);
    if (fread(&parentDirectoryInode, sizeof(struct wfs_inode), 1, disk_img) != 1){
        printf("Error reading parentDirectoryInode from disk img\n");
        // fclose(disk_img);
        return -1;
    }
    // is this parentnode a directory?
    if (!(parentDirectoryInode.mode | S_IFDIR)){
        printf("Parent node not a directory: %d, S_IFDIR = %d, parentInode.mode: %d\n", parentDirectoryInode.num, S_IFDIR, parentDirectoryInode.mode);
        return -1;
    }
    // go through this inodes' datablocks. possible dentrys in a datablock is 512 / sizeof(wfs_dentry)
    for (int i = 0; i < N_BLOCKS; i++){
        off_t datablock_offset = parentDirectoryInode.blocks[i];
        if (datablock_offset == 0){ // datablock is unititialized
            continue;
        }
        // go through wfs_dentrys in this datablock
        for (int j = 0; j < (BLOCK_SIZE / sizeof(struct wfs_dentry)); j++){
            struct wfs_dentry dentry;
            fseek(disk_img, datablock_offset + (j * sizeof(struct wfs_dentry)), SEEK_SET);
            if (fread(&dentry, sizeof(struct wfs_dentry), 1, disk_img) != 1){
                printf("error looking through datablock\n");
            }
            if (dentry.num == -1){
                continue;
            }
            // printf("parentInodeIdx: %d, dentry - num: %d name: %s, looking for: %s\n", parentInodeIdx, dentry.num, dentry.name, child);
            if (strcmp(dentry.name, child) == 0){
                printf("parentInodeIdx: %d, dentry - num: %d name: %s, looking for: %s\n", parentInodeIdx, dentry.num, dentry.name, child);
                return dentry.num;
            }
        }
        // go through all wfs_dentry structs in the associated datablock
    }
    printf("Child not found\n");
    return -1;
}
/*
    traversal method for 
*/
int traversal(const char* path, struct wfs_inode* buf){
    printf("traversing Path: %s\n", path);
    disk_img = fopen(disk_path, "r");
    // print contents of superblock - should be at offset 0
    if (!disk_img){
        printf("ERROR opening disk image in wfs_getattr\n");
        return -1;
    }

    char path_copy[strlen(path) + 1];
    strcpy(path_copy, path);

    char* token = strtok(path_copy, "/");
    char* prev = NULL;
    int currentNode = 0;
    while(token != NULL){
        // printf("token: %s\n", token);
        currentNode = findChild(currentNode, token);
        if (currentNode == -1){
            printf("path doesn't exist\n");
            // fclose(disk_img);
            return -1;
        }
        printf("%s\n", token);
        prev = token;
        token = strtok(NULL, "/");

    }
    // printf("destination node: %s\n", prev);
    int offset = superblock.i_blocks_ptr + (BLOCK_SIZE * currentNode);
    fseek(disk_img, offset, SEEK_SET);
    struct wfs_inode temp;
    if(fread(&temp, sizeof(struct wfs_inode), 1, disk_img) != 1){
        printf("Failed to read into buf\n");
        fclose(disk_img);
        return -1;
    }
    if (buf != NULL){
        // copy from temp to buf
        buf->atim = temp.atim;
        for (int i = 0; i < N_BLOCKS; i++){
            buf->blocks[i] = temp.blocks[i];
        }
        buf->ctim = temp.ctim;
        buf->gid = temp.gid;
        buf->mode = temp.mode;
        buf->mtim = temp.mtim;
        buf->nlinks = temp.nlinks;
        buf->num = temp.num;
        buf->size = temp.size;
        buf->uid = temp.uid;
    }

    printf("exiting traversal found %s\n", prev);
    fclose(disk_img);
    return currentNode;
}
/*
    traversal method for file exploration. last node doesn't exist and is the one we must create
    will return the second to last inode in the path if the path is valid
    returns -1 on failure
*/
int createTraversal(const char* path){
    printf("In createTraversal\n");
    printf("Path: %s\n", path);

    // Copy path to manipulate
    char path_copy[strlen(path) + 1];
    strcpy(path_copy, path);

    // Find the last '/' in the path
    char* lastSlash = strrchr(path_copy, '/');
    if (!lastSlash) {
        printf("Invalid path format\n");
        return -1;
    }

    // Eliminate the last node in the path
    *lastSlash = '\0';

    if (*path_copy == '\0') { // if cutting out last node leaves us with blank string, pass in root as path
        strcpy(path_copy, "/");
    }
    printf("path when cutting out last node: %s\n", path_copy);
    // Call the traversal method with the modified path
    int result = traversal(path_copy, NULL);
    if (result == -1) {
        // Path doesn't exist up to second-to-last element
        printf("Path doesn't exist up to second-to-last element\n");
        return -1;
    }
    printf("Second-to-last node index: %d\n", result);

    printf("Exiting createTraversal\n");
    return result;
}
void printDataBitmap(struct wfs_sb superblock){
    fseek(disk_img, superblock.d_bitmap_ptr, SEEK_SET);
     for (int i = 0; i < (superblock.num_data_blocks / 32); i++){
        unsigned int dbm;
        if (fread(&dbm, sizeof(unsigned int), 1, disk_img) != 1){
            printf("error reading data bitmap\n");
            fclose(disk_img);
            break;
        }
        for (int j = 0; j < 32; j++){
            printf("datablock %d in bitmap: %d\n", (i*32) + j, getBitValue(&dbm, j));
        } 
    }
}
void printInodeBitmap(struct wfs_sb superblock){
    fseek(disk_img, superblock.i_bitmap_ptr, SEEK_SET);
    for (int i = 0; i < (superblock.num_inodes / 32); i++){
        unsigned int ibm;
        if (fread(&ibm, sizeof(unsigned int), 1, disk_img) != 1){
            printf("error reading inode bitmap\n");
            fclose(disk_img);
            break;
        }
        for (int j = 0; j < 32; j++){
            printf("inode %d in bitmap: %d\n", (i*32) + j, getBitValue(&ibm, j));
        } 
    }
}
// returns -1 on failure and the idx of the inserted idx on success
// on success the idx this function returns should now be marked as 1
// this function assumes the file has already been opened for r/w ops
int insertInodeBitmap(){
    fseek(disk_img, superblock.i_bitmap_ptr, SEEK_SET);
    for (int i = 0; i < (superblock.num_inodes / 32); i++){
        // find open slot to flip bit to 1
        unsigned int ibm;
        if (fread(&ibm, sizeof(unsigned int), 1, disk_img) != 1){
            printf("error reading inode bitmap\n");
            return -1;
        }
        // is value's 1 bit set?
        for (int j = 0; j < 32; j++){
            // check all 32 bits of this unsigned int
            int value = getBitValue(&ibm, j);
            if (value % 2 == 0){
                // valid place to insert
                setBit(&ibm, j); // set the jth bit to 1
                fseek(disk_img, superblock.i_bitmap_ptr + (i * sizeof(unsigned int)), SEEK_SET);
                if (fwrite(&ibm, sizeof(int), 1, disk_img) != 1){
                    printf("error writing to inode bitmap\n");
                    return -1;
                }
                return (i*32) + j;
            }
        }
    }
    return -ENOSPC;
}
// removes "idx" index from inode bitmap (sets it to 0)
// returns 0 on success and -1 on failure
// assumes the disk_img file is open for writing
int removeInodeBitmap(int idx){
    printf("removing inode at idx: %d in bitmap\n", idx);
    int offset = superblock.i_bitmap_ptr + ((idx/32) *sizeof(unsigned int));
    int bitPosition = idx % 32; // offset finds which unsigned int holds our idx, bitPosition is the bit within this unsigned int that we want to mutate
    fseek(disk_img, offset, SEEK_SET);
    unsigned int ibm;
    if (fread(&ibm, sizeof(unsigned int), 1, disk_img) != 1){
        printf("error reading inode bitmap\n");
        return -1;
    }
    if (getBitValue(&ibm, bitPosition) % 2 == 0){
        printf("index: %d of inode bitmap is already 0\n", idx);
        return -1;
    }
    clearBit(&ibm, bitPosition);
    fseek(disk_img, offset, SEEK_SET);
    if (fwrite(&ibm, sizeof(unsigned int), 1, disk_img) != 1){
        printf("error removing from inode bitmap\n");
        return -1;
    }
    return 0;
}
// initializes a datablock, if isDirectory == 1, init with empty dentry structs
// returns offset to datablock on success and -1 on failure
// TODO: check if there is space in the file for writing? there could be space in the bitmap but not space in the file so this method might need to detect that?
int initDirectoryDatablock(int idx){
    int offset = superblock.d_blocks_ptr + (idx * BLOCK_SIZE);
    // wipe datablock
    struct wfs_dentry emptyDentry;
    memset(emptyDentry.name, 0, sizeof(emptyDentry.name)); // Initialize name with zeros
    char* blankName = "";
    strncpy(emptyDentry.name, blankName, sizeof(emptyDentry.name));
    emptyDentry.name[sizeof(emptyDentry.name) - 1] = '\0'; // https://stackoverflow.com/questions/25838628/copying-string-literals-in-c-into-an-character-array
    emptyDentry.num = -1; // Or any other invalid inode number

    // Fill the data block with empty directory entries
    printf("init new datablock...\n");
    fseek(disk_img, offset, SEEK_SET);
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct wfs_dentry)); i++) {
        if (fwrite(&emptyDentry, sizeof(struct wfs_dentry), 1, disk_img) != 1) {
            printf("error writing empty directory entry to data block\n");
            fclose(disk_img);
            return -1;
        }
    }
    return offset;
}
// returns -1 on failure and the idx of the inserted idx on success
// on success the idx this function returns should now be marked as 1
// this function assumes the file has already been opened for r/w ops
int insertDataBitmap(){
    fseek(disk_img, superblock.d_bitmap_ptr, SEEK_SET);
    for (int i = 0; i < (superblock.num_data_blocks / sizeof(unsigned int)); i++){
        // find open slot to flip bit to 1
        unsigned int dbm;
        if (fread(&dbm, sizeof(unsigned int), 1, disk_img) != 1){
            printf("error reading inode bitmap\n");
            return -1;
        }
        // is value's 1 bit set?
        for (int j = 0; j < 32; j++){
            // check all 32 bits of this unsigned int
            int value = getBitValue(&dbm, j);
            if (value % 2 == 0){
                // valid place to insert
                setBit(&dbm, j); // set the jth bit to 1
                fseek(disk_img, superblock.d_bitmap_ptr + (i * sizeof(unsigned int)), SEEK_SET);
                if (fwrite(&dbm, sizeof(unsigned int), 1, disk_img) != 1){
                    printf("error writing to inode bitmap\n");
                    return -1;
                }
                printf("inserting data bitmap at %d\n", (i*32) + j);
                return (i*32) + j;
            }
        }
    }
    return -ENOSPC;
}
// removes "idx" index from inode bitmap (sets it to 0)
// returns 0 on success and -1 on failure
// assumes the disk_img file is open for writing
int removeDataBitmap(int idx){
    int offset = superblock.d_bitmap_ptr + ((idx/32) *sizeof(unsigned int));
    int bitPosition = idx % 32; // offset finds which unsigned int holds our idx, bitPosition is the bit within this unsigned int that we want to mutate
    fseek(disk_img, offset, SEEK_SET);
    unsigned int dbm;
    if (fread(&dbm, sizeof(unsigned int), 1, disk_img) != 1){
        printf("error reading inode bitmap\n");
        return -1;
    }
    if (getBitValue(&dbm, bitPosition) % 2 == 0){
        printf("index: %d of inode bitmap is already 0\n", idx);
        return 0;
    }
    clearBit(&dbm, bitPosition);
    fseek(disk_img, offset, SEEK_SET);
    if (fwrite(&dbm, sizeof(unsigned int), 1, disk_img) != 1){
        printf("error removing from inode bitmap\n");
        return -1;
    }
    return 0;
}
// this function will write a new inode given the inode to write and the idx it corresponds to in the ibitmap
// this function assumes inode ptr passed in is nonnull (fields are initialized) and performs the write to our disk_img
// this function also assumes that our file is open for r/w ops
int writeInode(struct wfs_inode* inode, int idx){
    int offset = superblock.i_blocks_ptr + (idx * BLOCK_SIZE); // change BLOCK_SIZE to sizeof(struct wfs_inode)?
    
    fseek(disk_img, offset, SEEK_SET);

    if(fwrite(inode, sizeof(struct wfs_inode), 1, disk_img) != 1){
        printf("Error writing to inode\n");
        return -1;
    }
    
    return 0;
}
static int wfs_getattr(const char *path, struct stat *stbuf){
    printf("\nIn wfs_getattr path: %s\n", path);
    disk_img = fopen(disk_path, "r");
    // print contents of superblock - should be at offset 0
    if (!disk_img){
        printf("ERROR opening disk image in wfs_getattr\n");
        return -1;
    }
    
    fseek(disk_img, 0, SEEK_SET);
    if (fread(&superblock, sizeof(struct wfs_sb), 1, disk_img) != 1){
        printf("Error reading superblock from disk img\n");
        fclose(disk_img);
        return -1;
    }
    //printf("Superblock: num_inodes=%ld, num_data_blocks=%ld\n", superblock.num_inodes, superblock.num_data_blocks);
    fclose(disk_img);
    // need to fill in st_uid, st_gid, st_atime, st_mtime, st_mode, st_size
    struct wfs_inode destinationInode;
    memset(&destinationInode, 0, sizeof(struct wfs_inode));
    //printf("!! %s\n", path);
    int result = traversal(path, &destinationInode);

    if (result == -1){
        return -ENOENT;
    }else{
        // printf("copying into stbuf...\n");
        stbuf->st_uid = destinationInode.uid;
        stbuf->st_gid = destinationInode.gid;
        stbuf->st_atime = destinationInode.atim;
        stbuf->st_mtime = destinationInode.mtim;
        stbuf->st_mode = destinationInode.mode;
        stbuf->st_size = destinationInode.size;
    }
    printf("exiting wfs_getattr\n\n");
    return 0;
}
/*
    helper function for mkdir. inserts dentry into an Inode's datablock
    returns 0 on success and -1 on failure
*/
int insertDentry(int parentInodeIdx, struct wfs_dentry* dentry){
    // dentry will hold the inodeIdx and name of the directory we are inserting 
    printf("inserting dentry with num: %d name: %s at inode: %d\n", dentry->num, dentry->name, parentInodeIdx);
    struct wfs_inode parentInode;
    int parentInodeOffset = superblock.i_blocks_ptr + (parentInodeIdx * BLOCK_SIZE);
    fseek(disk_img, parentInodeOffset, SEEK_SET);
    if (fread(&parentInode, sizeof(struct wfs_inode), 1, disk_img) != 1) {
        printf("error reading parent directory inode\n");
        return -1;
    }
    // TODO: lazy allocation: does a datablock exist for this inode? if not allocate it, then perform dentry insert
    struct wfs_dentry currentDentry;
    for (int i = 0; i < N_BLOCKS; i++){ // go through all valid datablocks associated with this inode
        if (parentInode.blocks[i] != 0){
            for (int j = 0; j < (BLOCK_SIZE / sizeof(struct wfs_dentry)); j++){ // read all dentrys in this block
                int offset = parentInode.blocks[i] + (j * sizeof(struct wfs_dentry));
                fseek(disk_img, offset, SEEK_SET);
                if (fread(&currentDentry, sizeof(struct wfs_dentry), 1, disk_img) != 1){
                    printf("error reading dentry in datablock at %ld\n", parentInode.blocks[i]);
                    return -1;
                }
                // printf("currentDentry: num: %d, name: %s, parent: %d\n", currentDentry.num, currentDentry.name, parentInodeIdx);
                // is this dentry available?
                if (currentDentry.num == -1){
                    // write at offset and return success
                    fseek(disk_img, offset, SEEK_SET);
                    if (fwrite(dentry, sizeof(struct wfs_dentry), 1, disk_img) != 1) {
                        printf("error writing new dentry to parent Inode\n");
                        return -1;
                    }
                    return 0;
                }
            }
        }
    }
    // after for loop, means we didn't find space in our currently allocated datablocks, we try to allocate another datablock for this inode
    // parentInode.blocks field needs to hold an offset to this new datablock
    // parenInode.blocks has space?
    int datablockOffsetIdx = -1;
    for (int i = 0; i < N_BLOCKS; i++){ 
        if (parentInode.blocks[i] == 0){
            // i can hold an offset to a new datablock
            datablockOffsetIdx = i;
            break;
        }
    }
    if (datablockOffsetIdx == -1){ // case where blocks is full and the datablocks are also full
        return -ENOSPC;
    }
    // insert into databitmap
    int dataBitmapIdx = insertDataBitmap();
    if (dataBitmapIdx < 0){
        return dataBitmapIdx;
    }
    // initialize a datablock for this new directory
    int datablockOffset = initDirectoryDatablock(dataBitmapIdx);
    if (datablockOffset < 0) {
        printf("error initializing datablock for new directory\n");
        return datablockOffset;
    }

    // insert our dentry into the 0th offset of this new block
    fseek(disk_img, datablockOffset, SEEK_SET);
    if (fwrite(dentry, sizeof(struct wfs_dentry), 1, disk_img) != 1) {
        printf("error writing empty directory entry to data block\n");
        return -1;
    }
    // update parentInode.blocks with offset to new datablock
    parentInode.blocks[datablockOffsetIdx] = datablockOffset;
    fseek(disk_img, parentInodeOffset, SEEK_SET);
    if (fwrite(&parentInode, sizeof(struct wfs_inode), 1, disk_img) != 1){
        printf("error writing parentInode ot file\n");
        return -1;
    }
    return 0;

}

// creating a file
static int wfs_mknod(const char* path, mode_t mode, dev_t dev){
    printf("In wfs_mknod\n");
    int result = createTraversal(path);
    if (result == -1){
        printf("invalid path in wfs_mknod\n");
        return -ENOENT;
    }
    // result should hold parent inode of node we are looking to insert
    printf("second to last inode is: %d\n", result);
    // check if second to last inode already has a child matching this node-to-insert
    disk_img = fopen(disk_path, "r+");
    // open file for r/w for bitmap ops and datablock/inode insert
    if (!disk_img){
        printf("ERROR opening disk image in wfs_mkdir\n");
        return -1;
    }

    // create inode, update inode bitmap accordingly and parent inode to point to this inode
    int inodeIdx = insertInodeBitmap();
    if (inodeIdx < 0){
        printf("error inserting into Inode bitmap\n");
        fclose(disk_img);
        return inodeIdx;
    }

    printf("inserted inode at %d in bitmap\n", inodeIdx);

    struct wfs_inode inode;
    inode.atim = 0;
    for (int i = 0; i < N_BLOCKS; i++){
        inode.blocks[i] = 0;
    }
    inode.ctim = 0;
    inode.gid = getgid();
    inode.mode = S_IFREG; // mode or S_IFDIR
    inode.mtim = 0;
    inode.nlinks = 0;
    inode.num = inodeIdx;
    inode.size = 0; // ? not sure what this should be initalized to
    inode.uid = getuid();
    
    if (writeInode(&inode, inodeIdx) == -1){
        printf("failed to write inode in mkdir\n");
        fclose(disk_img);
        return -1;
    }

    // update parent directory with dentry to this new inode
    struct wfs_dentry dentry;
    // get name of node to insert (should be no slashes if /a is path need a, if /a/b, need b)
    const char* lastSlash = strrchr(path, '/');
    char* destinationNode;
    if (lastSlash == NULL) {
        destinationNode = strdup(path); // If no slash found, return a duplicate of the whole path
    } else {
        destinationNode = strdup(lastSlash + 1); // Return a duplicate of the substring after the last slash
    }
    memset(dentry.name, 0, sizeof(dentry.name)); // Initialize name with zeros
    strncpy(dentry.name, destinationNode, sizeof(dentry.name));
    dentry.name[sizeof(dentry.name)] = '\0';
    dentry.num = inodeIdx;
    int insertDentryResult = insertDentry(result, &dentry); // error check to ensure this doesn't fail
    if (insertDentryResult < 0){
        fclose(disk_img);
        free(destinationNode);
        return insertDentryResult;
    }
     // update parent inode to have += 1 links?
    int parentInodeOffset = superblock.i_blocks_ptr + (result * BLOCK_SIZE);
    struct wfs_inode parentInode;
    fseek(disk_img, parentInodeOffset, SEEK_SET);
    if (fread(&parentInode, sizeof(struct wfs_inode), 1, disk_img) != 1) {
        printf("error reading parentInode in mknod\n");
        return -1;
    }
    // update parentInode and write back to file
    parentInode.nlinks += 1;
    fseek(disk_img, parentInodeOffset, SEEK_SET);
    if (fwrite(&parentInode, sizeof(struct wfs_inode), 1, disk_img) != 1) {
        printf("error writing parentInode in mknod\n");
        return -1;
    }
    
    printf("dentry name: %s\n", dentry.name);
    printf("exiting mkdir\n\n");
    fclose(disk_img);
    free(destinationNode);
    return 0;
}
// creating a directory
static int wfs_mkdir(const char* path, mode_t mode){
    // notse: lazy allocation. also . and .. handled by fuse, dont need to worry aobut that ourselves
    // TODO: only create datablock and assign datablock to inode when something needs to be written to datablock
    // initially we shouldn't take up datablocks for an empty dir or file. should only allocate inode initially
    printf("\nIn wfs_mkdir\n");
    int result = createTraversal(path);
    // result will hold the inodeIdx of the second to last element
    if (result == -1){
        printf("invalid path in wfs_mkdir\n");
        return -ENOENT;
    }
    printf("second to last inode is: %d\n", result);
    // check if second to last inode already has a child matching this node-to-insert
    // could just call traversal again on the non-mutated path, if succesful, then file/dir already exists, return failure
    disk_img = fopen(disk_path, "r+");
    // open file for r/w for bitmap ops and datablock/inode insert
    if (!disk_img){
        printf("ERROR opening disk image in wfs_mkdir\n");
        return -1;
    }
    // is there space in our bitmaps?
    int inodeIdx = insertInodeBitmap();
    if (inodeIdx < 0){
        printf("error inserting into Inode bitmap\n");
        fclose(disk_img);
        return inodeIdx;
    }

    printf("inserted inode at %d in bitmap\n", inodeIdx);
    // isnertion location of inode/datablock should match the idx of the bitmap -> 
    // inode should be placed at (inode_start + (idx * BLOCK_SIZE)
    // create inode, update inode bitmap accordingly and parent inode to point to this inode
    printf("mode: %d, S_IFDIR: %d\n", mode, S_IFDIR);
    struct wfs_inode inode;
    inode.atim = 0;
    for (int i = 0; i < N_BLOCKS; i++){
        inode.blocks[i] = 0;
    }
    inode.ctim = 0;
    inode.gid = getgid();
    inode.mode = S_IFDIR; // mode or S_IFDIR
    inode.mtim = 0;
    inode.nlinks = 0;
    inode.num = inodeIdx;
    inode.size = 0; // ? not sure what this should be initalized to
    inode.uid = getuid();

    // new inode is of type directory, will have references to . and .. in datablock
    // allocate datablock and dentrys
    
   
    // write inode after writing datablock
    // inode.blocks[0] = offset; // newinode should point to our new datablock
    if (writeInode(&inode, inodeIdx) == -1){
        printf("failed to write inode in mkdir\n");
        fclose(disk_img);
        return -1;
    }

    // update parent dir to have dentry to this new dir we inserted
    struct wfs_dentry dentry;
    // get name of node to insert (should be no slashes if /a is path need a, if /a/b, need b)
    const char* lastSlash = strrchr(path, '/');
    char* destinationNode;
    if (lastSlash == NULL) {
        destinationNode = strdup(path); // If no slash found, return a duplicate of the whole path
    } else {
        destinationNode = strdup(lastSlash + 1); // Return a duplicate of the substring after the last slash
    }
    memset(dentry.name, 0, sizeof(dentry.name)); // Initialize name with zeros
    strncpy(dentry.name, destinationNode, sizeof(dentry.name));
    dentry.name[sizeof(dentry.name)] = '\0';
    dentry.num = inodeIdx;
    int insertDentryResult = insertDentry(result, &dentry); // error check to ensure this doesn't fail
    if (insertDentryResult < 0){
        fclose(disk_img);
        free(destinationNode);
        return insertDentryResult;
    }
    printf("dentry name: %s\n", dentry.name);
    printf("exiting mkdir\n\n");
    fclose(disk_img);
    free(destinationNode);
    return 0;
}
int deleteDentry(int inodeToDelete, int inodeToSearch){
     struct wfs_inode parentInode;
    int parentInodeOffset = superblock.i_blocks_ptr + (inodeToSearch * BLOCK_SIZE);
    fseek(disk_img, parentInodeOffset, SEEK_SET);
    if (fread(&parentInode, sizeof(struct wfs_inode), 1, disk_img) != 1) {
        printf("error reading parent directory inode\n");
        return -1;
    }
    // TODO: lazy allocation: does a datablock exist for this inode? if not allocate it, then perform dentry insert
    struct wfs_dentry currentDentry;
    for (int i = 0; i < N_BLOCKS; i++){ // go through all valid datablocks associated with this inode
        if (parentInode.blocks[i] != 0){
            for (int j = 0; j < (BLOCK_SIZE / sizeof(struct wfs_dentry)); j++){ // read all dentrys in this block
                int offset = parentInode.blocks[i] + (j * sizeof(struct wfs_dentry));
                fseek(disk_img, offset, SEEK_SET);
                if (fread(&currentDentry, sizeof(struct wfs_dentry), 1, disk_img) != 1){
                    printf("error reading dentry in datablock at %ld\n", parentInode.blocks[i]);
                    return -1;
                }
                printf("currentDentry: num: %d, name: %s, parent: %d\n", currentDentry.num, currentDentry.name, inodeToSearch);
                // is this dentry available?
                if (currentDentry.num == inodeToDelete){
                    // write empty entry at offset and return success
                    fseek(disk_img, offset, SEEK_SET);
                    struct wfs_dentry emptyDentry;
                    emptyDentry.num = -1; // Or any other invalid inode number
                    memset(emptyDentry.name, 0, sizeof(emptyDentry.name)); // Initialize name with zeros
                    char* blankName = "";
                    strncpy(emptyDentry.name, blankName, sizeof(emptyDentry.name));
                    emptyDentry.name[sizeof(emptyDentry.name) - 1] = '\0'; // https://stackoverflow.com/questions/25838628/copying-string-literals-in-c-into-an-character-array
                    if (fwrite(&emptyDentry, sizeof(struct wfs_dentry), 1, disk_img) != 1) {
                        printf("error writing new dentry to parent Inode\n");
                        return -1;
                    }
                    return 0;
                }
            }
        }
    }
    return -1;
}
int clearDatablock(int datablockIdx){
    int offset = superblock.d_blocks_ptr + (datablockIdx * BLOCK_SIZE);
    // Seek to the position of the data block
    if (fseek(disk_img, offset, SEEK_SET) != 0) {
        perror("Error seeking file");
        return -1;
    }

    // Write 512 zero bytes to clear the data block
    char zero_buffer[512] = {0}; // Initialize a buffer with zeros
    if (fwrite(zero_buffer, sizeof(char), sizeof(zero_buffer), disk_img) != sizeof(zero_buffer)) {
        perror("Error writing to file");
        return -1;
    }
    return 0;
}
/*
    will create our indirect block full of empty offsets (0 values)
    ultimately these pointers/offsets will point to other datablocks
    assumes "datablockIdx" is already valid and allocated in our data bitmap and that the file is open for reading/writing
*/
int initIndirectBlock(int datablockIdx){
    printf("\n in initIndirectBlock: datablockidx: %d\n", datablockIdx);
    //printDataBitmap(superblock);
    int datablockOffset = superblock.d_blocks_ptr + (datablockIdx * BLOCK_SIZE);
    printf("datablockOffset: %d = superblock.d_blocks_ptr %ld + (%d * 512)\n", datablockOffset, superblock.d_blocks_ptr, datablockIdx);
    off_t emptyOffset = 0;
    printf("empty offset: %ld\n", emptyOffset);
    for (int i = 0; i < (BLOCK_SIZE / sizeof(off_t)); i++){
        fseek(disk_img, datablockOffset + (i * sizeof(off_t)), SEEK_SET);
        if (fwrite(&emptyOffset, sizeof(off_t), 1, disk_img) != 1){
            printf("error initializing indirect block with empty offsets\n");
            return -1;
        }
        off_t actualOffset;
        fseek(disk_img, datablockOffset + (i * sizeof(off_t)), SEEK_SET);
        if (fread(&actualOffset, sizeof(off_t), 1, disk_img) != 1){
            printf("error reading actualOffset\n");
            return -1;
        }
        // printf("acutalOffset: %ld\n", actualOffset);

    }
    return datablockIdx;
}
/*
    will free the offsets/pointer to datablocks in our indirect block but not the indirect block itself. unlink will do that for us
*/
int clearIndirectBlock(int datablockIdx){
    // remove valid entries/offsets from data bitmap
    int datablockOffset = superblock.d_blocks_ptr + (datablockIdx * BLOCK_SIZE);
    off_t emptyOffset;
    fseek(disk_img, datablockOffset, SEEK_SET);
    // read all offsets, calculate their idx in bitmap and remove
    for (int i = 0; i < (BLOCK_SIZE / sizeof(off_t)); i++){
        if (fread(&emptyOffset, sizeof(off_t), 1, disk_img) != 1){
            printf("error initializing indirect block with empty offsets\n");
            return -1;
        }
        if (emptyOffset != 0){
            // find idx in bitmap and remove
            int bitmapIdx = (emptyOffset - superblock.d_blocks_ptr) / BLOCK_SIZE;
            if (removeDataBitmap(bitmapIdx) < 0){
                printf("error removing %d idx from databitmap\n", bitmapIdx);
            }
        }
    }

    return 0;
}
/*
    helper method that will insert into our indirect block
    assumes this datablock is already initialized using "initIndirectBlock"
*/
int insertIndirectBlock(int datablockIdx){
    // first need to ensure that there is space in our data bitmap
    int indirectBlockOffset = superblock.d_blocks_ptr + (datablockIdx * BLOCK_SIZE);
    int dataIdx = insertDataBitmap();
    if (dataIdx < 0){
        return dataIdx; // could be -ENOSPC
    }
    off_t offsetToInsert = superblock.d_blocks_ptr + (dataIdx * BLOCK_SIZE);
    // find first open offset (offset == 0) to perform insert. inserted item should be an off_t to a datablock
    fseek(disk_img, indirectBlockOffset, SEEK_SET);
    // read all offsets, calculate their idx in bitmap and remove
    off_t currentOffset;
    for (int i = 0; i < (BLOCK_SIZE / sizeof(off_t)); i++){
        if (fread(&currentOffset, sizeof(off_t), 1, disk_img) != 1){
            printf("error initializing indirect block with empty offsets\n");
            return -1;
        }
        if (currentOffset == 0){
            // insert into this offset
            fseek(disk_img, indirectBlockOffset + (i * sizeof(off_t)), SEEK_SET);
            if (fwrite(&offsetToInsert, sizeof(off_t), 1, disk_img) != 1){
                printf("error writing offset into indirect datablock\n");
                return -1;
            }
            return dataIdx;
        }
    }
    return -ENOSPC; // if we get past this loop, there is not space in our indirect block
}
/*
    removes a file. if we have hard links, or special nodes behavior could be different
*/
static int wfs_unlink(const char* path){
    printf("In wfs_unlink\n");
    // find node to remove
    int parentInodeIdx = createTraversal(path);
    // result will hold the inodeIdx of the second to last element
    if (parentInodeIdx == -1){
        printf("invalid path in wfs_mkdir\n");
        return -ENOENT;
    }
    struct wfs_inode node_to_remove;
    int inodeIdx = traversal(path, &node_to_remove);
    if (inodeIdx < 0){
        printf("error, path doesn't exist\n");
        return -ENOENT;
    }
    // node to remove must be a file
    printf("inode idx: %d parent inode idx: %d\n", inodeIdx, parentInodeIdx);

    disk_img = fopen(disk_path, "r+");
    // print contents of superblock - should be at offset 0
    if (!disk_img){
        printf("ERROR opening disk image in wfs_getattr\n");
        return -1;
    }

    if (!(node_to_remove.mode | S_IFREG)){
        printf("error, path doesn't point to a file\n");
        fclose(disk_img);
        return -ENOENT;
    }
    // remove datablocks that belong to this inode
    // TODO: handle case where i = N_BLOCKS-1, this is a offset to a datablock holding more datablocks. free all inner datablocks and then free the indirect block
    for (int i = 0; i < N_BLOCKS; i++){
        off_t offset = node_to_remove.blocks[i];
        if (offset != 0) {
            // find correspoding idx in databitmap
            int dataIdx = (offset - superblock.d_blocks_ptr) / BLOCK_SIZE;
            // indirect block case:
            if (i == N_BLOCKS-1){
                clearIndirectBlock(dataIdx); // will clear our indirect block (just the offsets/pointers inside and free those up in bitmap)
            }
            // remove from data bitmap
            if (removeDataBitmap(dataIdx) != 0){
                printf("error removing from databitmap\n");
                fclose(disk_img);
                return -1;
            }
            clearDatablock(dataIdx);

        }
    }
    // remove from inode bitmap 
    if (removeInodeBitmap(inodeIdx) != 0){
        printf("error removing from inode bitmap\n");
        fclose(disk_img);
        return -1;
    }
    // parentInode dentry needs to be deleted, 
    deleteDentry(inodeIdx, parentInodeIdx);
    
    fclose(disk_img);
    return 0;
}

static int wfs_rmdir(const char* path){
    printf("In wfs_rmdir\n");
  
    int parentInodeIdx = createTraversal(path);
    // result will hold the inodeIdx of the second to last element
    if (parentInodeIdx == -1){
        printf("invalid path in wfs_mkdir\n");
        return -ENOENT;
    }
    struct wfs_inode node_to_remove;
    int inodeIdx = traversal(path, &node_to_remove);
    if (inodeIdx < 0){
        printf("error, path doesn't exist\n");
        return -ENOENT;
    }
    // node to remove must be a file
    printf("inode idx: %d parent inode idx: %d\n", inodeIdx, parentInodeIdx);

    disk_img = fopen(disk_path, "r+");
    if (!disk_img){
        printf("ERROR opening disk image in wfs_getattr\n");
        return -1;
    }

    if (!(node_to_remove.mode | S_IFDIR)){
        printf("error, path doesn't point to a directory\n");
        fclose(disk_img);
        return -ENOENT;
    }
    // remove datablocks that belong to this inode
    for (int i = 0; i < N_BLOCKS; i++){
        off_t offset = node_to_remove.blocks[i];
        if (offset != 0) {
            // find correspoding idx in databitmap
            int dataIdx = (offset - superblock.d_blocks_ptr) / BLOCK_SIZE;
            // remove from data bitmap
            if (removeDataBitmap(dataIdx) != 0){
                printf("error removing from databitmap\n");
                fclose(disk_img);
                return -1;
            }
            // do we also need to clear datablocks out? after removing from databitmap
            clearDatablock(dataIdx);
            
        }
    }
    // remove from inode bitmap 
    if (removeInodeBitmap(inodeIdx) != 0){
        printf("error removing from inode bitmap\n");
        fclose(disk_img);
        return -1;
    }
    // parentInode dentry needs to be deleted, 
    deleteDentry(inodeIdx, parentInodeIdx);
    
    fclose(disk_img);
    printf("Exiting wfs_rmdir\n\n");
    return 0;
}
/*
    gets the file size (how many datablocks are allocated to this file's inode)
*/
int getFileSize(struct wfs_inode* inode){
    // TODO: handle case where i = N_BLOCKS - 1. this is a pointer/offset to another datablock holding more offsets to additional datablocks that belong to this inode
    // go through datablocks and see which are being used or not being used
    int countDatablocks = 0;
    for (int i = 0; i < N_BLOCKS; i++){
        if (inode->blocks[i] != 0){
            if (i == N_BLOCKS - 1){
                off_t indirectBlockOffset =  inode->blocks[i];
                printf("indirectBLockOffset: %ld\n", indirectBlockOffset);
                printf("indirectBLockoffset (int) %d\n", (int)indirectBlockOffset);
                
                // read all off_t's in this datablock (this is our indirect block)
                off_t curOffset;
                for (int j = 0; j < (BLOCK_SIZE / sizeof(off_t)); j++){
                    fseek(disk_img, (int)indirectBlockOffset + (j*sizeof(off_t)), SEEK_SET);
                    if (fread(&curOffset, sizeof(off_t), 1, disk_img) != 1){
                        printf("error reading offsets in indirect block\n");
                    }
                    if (curOffset != 0) {
                        // printf("curOffset: %ld\n", curOffset);
                        countDatablocks += 1;
                    }
                }
            }else{
                countDatablocks += 1;
            }
        }
    }
    return countDatablocks;
    // return number of valid datablocks as size of this file
}
/*
    return number of bytes read
*/
static int wfs_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi){
    printf("In  size: %ld offset: %ld\n", size, offset);
    // find file to read from
    struct wfs_inode inode;
    int bytesRead = 0; // return value of our read function
    int traversalResult = traversal(path, &inode);
    if (traversalResult < 0){
        printf("couldnt find file to read from. path:  %s\n", path);
        return -ENOENT;
    }
    if (!(inode.mode | S_IFREG)){
        printf("destination node of path isn't a file, path: %s\n", path);
        return -ENOENT;
    }
    disk_img = fopen(disk_path, "r"); // open file for reading
    if (!disk_img){
        printf("ERROR opening disk image in wfs_getattr\n");
        return -1;
    }
    // go to datablocks for this file and read them into the buffer ("size" bytes)
    // if (offset > (BLOCK_SIZE * N_BLOCKS)){ // TODO: fix because offset could be larger than 4096 w indirect blocks 
    //     printf("not a valid offset\n");
    //     return -1;
    // }
    int numValidBlocks = getFileSize(&inode);
    if (offset > numValidBlocks * BLOCK_SIZE){
        return bytesRead;
    }
    int start_block = offset / BLOCK_SIZE;
    int start_offset_block = offset % BLOCK_SIZE;
    // if offset is 513, go to 1th idx of second block
    // if > 512 * N_BLOCKS - invalid
    // byutes left to read is size - bytesRead
    int curBlock = -1;
    printf("searching %d through datablocks for this inode...\n", numValidBlocks);
    for (int i = 0; i < N_BLOCKS; i++){
        // TODO: the block at N_BLOCKS - 1 is an indirect block -> means it point to a datablock that has pointers to other datablocks?
        // so our indirect block is a datablock that holds 512/sizeof(off_t offset) pointers
        if (bytesRead == size){
            break;
        }
        int datablockOffset = inode.blocks[i];
        if (datablockOffset != 0){
            printf("i: %d, offset: %d, curBlock: %d\n", i, datablockOffset, curBlock);
            if (i == N_BLOCKS - 1){
                // TODO indirect block, special read case
                // if offset != 0 += curBlock
                off_t currentOffset;
                printf("reading from indirect block: %d ld: %ld\n", datablockOffset, inode.blocks[i]);
                for (int j = 0; j < (BLOCK_SIZE / sizeof(off_t)); j++){
                    // printf("ld: %ld\n", inode.blocks[i] + (j * sizeof(off_t)));
                    fseek(disk_img, inode.blocks[i] + (j * sizeof(off_t)), SEEK_SET);
                    if(fread(&currentOffset, sizeof(off_t), 1, disk_img) != 1){
                        printf("Failed to read from indirect block\n");
                        fclose(disk_img);
                        printf("Exiting wfs_read ret value: %d\n\n", bytesRead);
                        return bytesRead;
                    }
                    
                    if(currentOffset != 0){
                        curBlock++;
                        if(curBlock >= start_block){
                            int bytesLeftToRead = size - bytesRead;
                            size_t bytesToReadFromBlock = (bytesLeftToRead > (BLOCK_SIZE - start_offset_block)) ? (BLOCK_SIZE - start_offset_block) : bytesLeftToRead;
                            fseek(disk_img, currentOffset + start_offset_block, SEEK_SET);
                            printf("bytes to read from block: %ld, currentOffset: %ld\n", bytesToReadFromBlock, currentOffset);
                            printf("buf ptr: %c\n", *buf);
                            for(int k = 0; k < bytesToReadFromBlock; k++){
                                if(fread(buf, 1, 1, disk_img) != 1){
                                    printf("Failed to read from indirect block\n");
                                    fclose(disk_img);
                                    printf("Exiting wfs_read ret value: %d\n\n", bytesRead);
                                    return bytesRead;
                                }
                                buf += 1;
                                bytesLeftToRead -= 1;
                                bytesRead += 1;
                                
                            }
                            start_offset_block = 0;
                            if (bytesRead == size){
                                fclose(disk_img);
                                printf("Exiting wfs_read ret value: %d\n\n", bytesRead);
                                return bytesRead;
                            }
                        }
                    }
                }
            }
            else{
                curBlock += 1;
                if (curBlock >= start_block){
                    printf("reading from directblock: %d\n", datablockOffset);
                    // start reading from this block
                    int bytesLeftToRead = size - bytesRead;
                    size_t bytesToReadFromBlock = (bytesLeftToRead > (BLOCK_SIZE - start_offset_block)) ? (BLOCK_SIZE - start_offset_block) : bytesLeftToRead;
                    printf("bytes to read from this block: %ld\n", bytesToReadFromBlock);
                    // read contiguously from this data block starting at "datablockOffset + start, after reading from first block start is 0?
                    fseek(disk_img, datablockOffset + start_offset_block, SEEK_SET);
                    // read character by character until we reach EOF or BLOCK_SIZE or size, write chars to buf
                    for (int j = 0; j < bytesToReadFromBlock; j++){
                        size_t bytesReadFromFile = fread(buf, 1, 1, disk_img);
                        if (bytesReadFromFile <= 0) {
                            break;
                        }
                        // printf("buf: %s\n", buf);
                        buf += 1;
                        bytesLeftToRead -= 1;
                        bytesRead += 1;
                        if (bytesRead == size){
                            fclose(disk_img);
                            printf("Exiting wfs_read ret value: %d\n\n", bytesRead);
                            return bytesRead;
                        }
                    }
                    start_offset_block = 0;
                    // start_block += 1;
                }
            }
        }
    }
    // start at "offset bytes into the file"
    // return number of bytes read (should be size bytes or until end of file/data)
    printf("Exiting wfs_read ret value: %d\n\n", bytesRead);
    fclose(disk_img);
    return bytesRead;
}
/*
    inserts into data bitmap and creates a datablock for this inode
    returns the idx of this data block created in our data bitmap. mutates the inode struct by adding "offset" to inode.blocks if there is space
    this function mutates an inode in memory but the changes to the inode needs to be written to the file after this file is called
*/
int allocateFileDatablock(struct wfs_inode* inode){
    // is there space for our file to point to more datablocks?
    int insertIdx = -1;
    for (int i = 0; i < N_BLOCKS; i++){
        if (inode->blocks[i] == 0){
            insertIdx = i;
            break;
        }
    }
    printf("inside allocateFileDatablock... insertIdx: %d\n", insertIdx);
    if (insertIdx < 0){ // no more space to write
        // TODO try to use indirect block before returning no space
        int indirectBlockOffset = inode->blocks[N_BLOCKS-1];
        int indirectBlockIdx = (indirectBlockOffset - superblock.d_blocks_ptr) / BLOCK_SIZE;
        int result = insertIndirectBlock(indirectBlockIdx);
        return result;
    }
    else if (insertIdx == N_BLOCKS - 1){
        printf("alocating indirect block in data bitmap\n");
        int indirectBlockIdx = insertDataBitmap();
        if (indirectBlockIdx < 0){
            return indirectBlockIdx;
        }
        off_t datablockOffset = superblock.d_blocks_ptr + (indirectBlockIdx * BLOCK_SIZE);
        inode->blocks[insertIdx] = datablockOffset;
        initIndirectBlock(indirectBlockIdx);
        int result = insertIndirectBlock(indirectBlockIdx);
        return result;
    }else{
        // is there space in data bitmap?
        int datablockIdx = insertDataBitmap();
        if (datablockIdx < 0){ 
            return datablockIdx;
        }
        off_t datablockOffset = superblock.d_blocks_ptr + (datablockIdx * BLOCK_SIZE);
        inode->blocks[insertIdx] = datablockOffset;
        // printDataBitmap(superblock);
        return datablockIdx;
    }
}
/*
    returns number of bytes written
*/
static int wfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi){
    printf("\nIn wfs_write, size: %zd, offset: %zd\n", size, offset);
    struct wfs_inode inode;
    // does file already exist? if it doesn't do we create it?
    int traversalResult = traversal(path, &inode);
    // if (traversalResult < 0) {
    //     printf("file doesn't exist\n");
    //     return -ENOENT;
    // }
    // do we have enough space?
    int start_block = offset / BLOCK_SIZE;
    int start_offset_block = offset % BLOCK_SIZE;
    printf("start block: %d , offset into block: %d\n", start_block, start_offset_block);
    
    disk_img = fopen(disk_path, "r+"); // open file for reading/writing
    if (!disk_img){
        printf("ERROR opening disk image in write\n");
        return 0;
    }
    int bytesWritten = 0;
    int fileSizeDatablocks = getFileSize(&inode);
    printf("datablocks to go through: %d\n", fileSizeDatablocks);
    int currentValidBlock = -1;
    // write as much as you can up to "size" bytes, return error if you run out of space, write from buffer into datablocks
    // use up 7 blocks, eigth is a special case that we have to handle
    for (int i = 0; i < N_BLOCKS; i++){
        // find datablocks to try and write to -> not the first valid one, the "start_block" one is where we start writing at start_block_offset
        if (inode.blocks[i] != 0){
            // valid block
            // printf("offset for this datablock %ld\n", inode.blocks[i]);
            if (i == N_BLOCKS - 1){
                // TODO indirect block, write to it
                // find valid blocks
                off_t currentOffset = 0;
                for (int j = 0; j < (BLOCK_SIZE/sizeof(off_t)); j++){
                    // read in offset
                    // printf("reading from indirect block starting at: %ld\n",inode.blocks[i] + (j *(sizeof(off_t))));
                    fseek(disk_img, inode.blocks[i] + (j * sizeof(off_t)), SEEK_SET);
                    if (fread(&currentOffset, sizeof(off_t), 1, disk_img) != 1){
                        printf("error reading from indirect block\n");
                        return bytesWritten;
                    }
                    if (currentOffset != 0){  // is it valid
                        // printf("currentOffset to read from (indirect block offset pointer): %ld\n", currentOffset);
                        currentValidBlock += 1;
                        if (currentValidBlock >= start_block) {
                             // calc how many bytes to write to it
                            int charsToRead = 0;
                            if ((BLOCK_SIZE - start_offset_block) < (size-bytesWritten)){
                                charsToRead = BLOCK_SIZE - start_offset_block;
                            }else{
                                charsToRead = size - bytesWritten;
                            }
                            // fileSizeDatablocks++;
                            // printf("filesizeDatablocks %d, numdatablocks: %ld\n", fileSizeDatablocks, superblock.num_data_blocks);
                            // if (fileSizeDatablocks > superblock.num_data_blocks){
                            //     return -ENOSPC;
                            // }
                            printf("chars to write: %d\n", charsToRead);
                            // seek + write
                            fseek(disk_img, currentOffset + start_offset_block, SEEK_SET);
                            // write byte by byte
                            for (int x = 0; x < charsToRead; x++){
                                if (fwrite(buf, 1, 1, disk_img) != 1) {
                                    printf("error writing to file in write\n");
                                    fclose(disk_img);
                                    return bytesWritten;
                                }
                                bytesWritten += 1;
                                buf += 1;
                            }
                            // are we done writing?
                            start_offset_block = 0;
                            if (bytesWritten == size){
                                printf("wrote %d bytes\n", bytesWritten);
                                fclose(disk_img);
                                return bytesWritten;
                            }
                        }
                    }
                }
            }else{
                currentValidBlock += 1;
                if (currentValidBlock >= start_block){
                    printf("writing to block %d\n", i);
                    // start writing at start_block_offset
                    // write char by char, if we write "size" bytes, exit
                    int charsToRead = 0;
                    if ((BLOCK_SIZE - start_offset_block) < (size-bytesWritten)){
                        charsToRead = BLOCK_SIZE - start_offset_block;
                        printf("(BLOCK_SIZE - start_offset_block) < (size-bytesWritten):  %d\n", charsToRead);
                    }else{
                        charsToRead = size - bytesWritten;
                        printf("(BLOCK_SIZE - start_offset_block) >= (size-bytesWritten):  %d\n", charsToRead);
                    }
                    printf("seeking to: %ld\n", inode.blocks[i] + start_offset_block);
                    fseek(disk_img, inode.blocks[i] + start_offset_block, SEEK_SET);
                    for (int j = 0; j < charsToRead; j++){
                        // printf("buf: %s\n", buf);
                        // write from buf to file
                        if (fwrite(buf, 1, 1, disk_img) != 1) {
                            printf("error writing to file in write\n");
                            fclose(disk_img);
                            return bytesWritten;
                        }
                        bytesWritten += 1;
                        buf += 1; // is this correct behavior for a pointer?
                    }
                    start_offset_block = 0;
                    if (bytesWritten == size){
                        printf("wrote %d bytes\n", bytesWritten);
                        fclose(disk_img);
                        return bytesWritten;
                    }
                    else if(bytesWritten > size){
                        fclose(disk_img);
                        return -ENOSPC;
                    }
                }
            }
        }
    }
    // if there is still work to be done and we have no more datablocks to write to -> allocate new ones
    // allocate new datablock(s)?
    // while(still data to write) {}
    // TODO: handle case where you are allocating the eighth datablock for this file (this is the indirect block)
    printf("\nallocating new datablocks...\n\n\n\n");
    while (bytesWritten < size){
        int datablockIdx = allocateFileDatablock(&inode); // returns a datablock to write to or -ENOSPC
        if (datablockIdx < 0){
            return datablockIdx; // do we return bytesWritten instead?
        }
        fileSizeDatablocks++;
        printf("\n\nfilesizeDatablocks %d, numdatablocks: %ld\n", fileSizeDatablocks, superblock.num_data_blocks);
        if (fileSizeDatablocks > superblock.num_data_blocks){
            return -ENOSPC;
        }
        // write updated inode to file
        int fileInodeOffset = superblock.i_blocks_ptr + (traversalResult * BLOCK_SIZE);
        fseek(disk_img, fileInodeOffset, SEEK_SET);
        if (fwrite(&inode, sizeof(struct wfs_inode), 1, disk_img) != 1){
            printf("error writing file inode\n");
            fclose(disk_img);
            return bytesWritten;
        }

        int datablockOffset = superblock.d_blocks_ptr + (datablockIdx * BLOCK_SIZE); // calc offset to write to
        printf("allocated datablock in bitmap for writing: %d, offset: %d\n", datablockIdx, datablockOffset);
        // write to newly allocated datablock
        int bytesToWrite = 0;
        if (BLOCK_SIZE < (size - bytesWritten)){
            bytesToWrite = BLOCK_SIZE;
        }else{
            bytesToWrite = size - bytesWritten;
        }
        printf("bytes to write for this block: %d\n", bytesToWrite);
        fseek(disk_img, datablockOffset, SEEK_SET);
        for (int j = 0; j < bytesToWrite; j++){
            // write from buf to file
            // printf("buf: %s\n", buf);
            if (fwrite(buf, 1, 1, disk_img) != 1) {
                printf("error writing to file in write\n");
                fclose(disk_img);
                return bytesWritten;
            }
            bytesWritten += 1;
            buf += 1; // is this correct behavior for a pointer?
        }
        if (bytesWritten == size){
            printf("Exiting wfs_write %d\n\n", bytesWritten);
            fclose(disk_img);
            return bytesWritten;
        }
    }
    printf("size of file: %d\n", getFileSize(&inode));
    printf("Exiting wfs_write %d\n\n", bytesWritten);
    fclose(disk_img);
    return bytesWritten;
}

// https://www.cs.hmc.edu/~geoff/classes/hmc.cs135.201001/homework/fuse/fuse_doc.html#readdir-details
static int wfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi){
    printf("In wfs_readdir\n");
    struct wfs_inode directory;
    
    int directoryInodeIdx = traversal(path, &directory);
    printf("dir inode idx: %d\n", directoryInodeIdx);

    disk_img = fopen(disk_path, "r+");
    if (!disk_img){
        printf("ERROR opening disk image in wfs_getattr\n");
        return -1;
    }
    // read data nodes of this directory and put all valid dentrys into buf
    // filler(buf, name, stat, off)
    for (int i = 0; i < N_BLOCKS; i++){
        int datablockOffset = directory.blocks[i];
        fseek(disk_img, datablockOffset, SEEK_SET);
        for(int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++){
            struct wfs_dentry dentry;
            if (datablockOffset != 0){
                // read dentries from datablock
                if (fread(&dentry, sizeof(struct wfs_dentry), 1, disk_img) != 1){
                    printf("error reading dentry from directory\n");
                    break;
                }
                // printf("dentry num: %d name: %s\n", dentry.num, dentry.name);
                if (dentry.num != -1){
                    // valid dentry
                    printf("dentry num: %d name: %s\n", dentry.num, dentry.name);
                    if (filler(buf, dentry.name, NULL, 0) != 0){
                        return -ENOSPC;
                    }
                }
            }
        }
    }
    // readdir should also add . and .. entries to buffer
    printf("Exiting wfs_readdir\n\n");
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


int main(int argc, char* argv[]){
    char* mount_point;
    int f_flag = 0;
    int s_flag = 0;

    for (int i = 1; i < argc; i++){
        if (strcmp(argv[i], "-f") == 0){
            f_flag = 1;
        }else if(strcmp(argv[i], "-s") == 0){
            s_flag = 1;
        }else{
            if (!disk_path){
                disk_path = argv[i];
                
            }else if(!mount_point){
                mount_point = argv[i];
            }
        }
    }
    printf("f_flag: %d, s_flag: %d, disk_path: %s, mount_point %s\n", f_flag, s_flag, disk_path, mount_point);
    if (!disk_path || !mount_point){
        printf("Usage: ./wfs disk_path [flags] mount_point\n");
    }

    // format will be ./wfs disk_path [flags] mount_point
    // before passing argc and argv into fuse_main, need to decrement argc and remove 0th arg
    argc--;
    for (int i = 0; i < argc; i++){
        argv[i] = argv[i+1];
    }
    argv[argc] = NULL;
    return fuse_main(argc, argv, &ops, NULL);
}