#include "wfs.h"
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

// parse the path
// get to inode you need to be at
// get or create at the point
// need a traversal function
FILE* disk_img;
char* disk_path;
struct wfs_sb superblock;

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
        fclose(disk_img);
        return -1;
    }
    // is this parentnode a directory?
    if (parentDirectoryInode.mode != S_IFDIR){
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
            if (fread(&dentry, sizeof(struct wfs_dentry), 1, disk_img) != 1){
                printf("error looking through datablock\n");
            }
            if (strcmp(dentry.name, child) == 0){
                return dentry.num;
            }
        }
        // go through all wfs_dentry structs in the associated datablock
    }
    return -1;
}
/*
    traversal method for 
*/
int traversal(const char* path, struct wfs_inode* buf){
    printf("In traversal\n");
    printf("Path: %s\n", path);
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
        currentNode = findChild(currentNode, token);
        if (currentNode == -1){
            printf("path doesn't exist\n");
            fclose(disk_img);
            return -1;
        }
        printf("%s\n", token);
        prev = token;
        token = strtok(NULL, "/");

    }
    printf("destination node: %s\n", prev);
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

    printf("exiting traversal\n");
    fclose(disk_img);
    return 0;
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
    for (int i = 0; i < superblock.num_data_blocks; i++){
        int dbm_value;
        if (fread(&dbm_value, sizeof(int), 1, disk_img) != 1){
            printf("error reading inode bitmap\n");
            fclose(disk_img);
            break;
        }
        printf("datablock %d in bitmap: %d\n", i, dbm_value);
    }
}
void printInodeBitmap(struct wfs_sb superblock){
    fseek(disk_img, superblock.i_bitmap_ptr, SEEK_SET);
    for (int i = 0; i < superblock.num_inodes; i++){
        int ibm_value;
        if (fread(&ibm_value, sizeof(int), 1, disk_img) != 1){
            printf("error reading inode bitmap\n");
            fclose(disk_img);
            break;
        }
        printf("inode %d in bitmap: %d\n", i, ibm_value);
    }
}
// returns -1 on failure and the idx of the inserted idx on success
// on success the idx this function returns should now be marked as 1
// this function assumes the file has already been opened for r/w ops
int insertInodeBitmap(){
    fseek(disk_img, superblock.i_bitmap_ptr, SEEK_SET);
    for (int i = 0; i < superblock.num_inodes; i++){
        // find open slot to flip bit to 1
        int value;
        if (fread(&value, sizeof(int), 1, disk_img) != 1){
            printf("error reading inode bitmap\n");
            return -1;
        }
        // is value's 1 bit set?
        if (value % 2 == 0){
            // valid place to insert
            int one = 1;
            fseek(disk_img, superblock.i_bitmap_ptr + (i * sizeof(int)), SEEK_SET);
            if (fwrite(&one, sizeof(int), 1, disk_img) != 1){
                printf("error writing to inode bitmap\n");
                return -1;
            }
            return i;
        }
    }
    return -1;
}
// returns -1 on failure and the idx of the inserted idx on success
// on success the idx this function returns should now be marked as 1
// this function assumes the file has already been opened for r/w ops
int insertDataBitmap(){
    return -1;
}
static int wfs_getattr(const char *path, struct stat *stbuf){
    printf("In wfs_getattr path: %s\n", path);
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
    printf("Superblock: num_inodes=%ld, num_data_blocks=%ld\n", superblock.num_inodes, superblock.num_data_blocks);
    fclose(disk_img);
    // need to fill in st_uid, st_gid, st_atime, st_mtime, st_mode, st_size
    struct wfs_inode destinationInode;
    memset(&destinationInode, 0, sizeof(struct wfs_inode));
    //printf("!! %s\n", path);
    int result = traversal(path, &destinationInode);

    if (result == -1){
        return -ENOENT;
    }else{
        printf("copying into stbuf...\n");
        stbuf->st_uid = destinationInode.uid;
        stbuf->st_gid = destinationInode.gid;
        stbuf->st_atime = destinationInode.atim;
        stbuf->st_mtime = destinationInode.mtim;
        stbuf->st_mode = destinationInode.mode;
        stbuf->st_size = destinationInode.size;
    }
    printf("exiting wfs_getattr\n");
    return 0;
}

// creating a file
static int wfs_mknod(const char* path, mode_t mode, dev_t dev){
    printf("In wfs_mknod\n");
    int result = createTraversal(path);
    if (result == -1){
        printf("invalid path in wfs_mknod\n");
        return -ENOENT;
    }else{
        // result should hold parent inode of node we are looking to insert
        // check if second to last inode already has a child matching this node-to-insert
        // create inode, update inode bitmap accordingly and parent inode to point to this inode
        // do we create data block?
    }
    return 0;
}

// creating a directory
static int wfs_mkdir(const char* path, mode_t mode){
    printf("In wfs_mkdir\n");
    int result = createTraversal(path);
    if (result == -1){
        printf("invalid path in wfs_mkdir\n");
        return -ENOENT;
    }else{
        printf("second to last inode is: %d\n", result);
        // check if second to last inode already has a child matching this node-to-insert
        // could just call traversal again on the non-mutated path, if succesful, then file/dir already exists, return failure
        if (traversal(path, NULL) != -1){
            printf("%s path already exists\n", path);
            return -EEXIST;
        }
        disk_img = fopen(disk_path, "r+");
        // open file for r/w for bitmap ops and datablock/inode insert
        if (!disk_img){
            printf("ERROR opening disk image in wfs_mkdir\n");
            return -1;
        }
        // create inode, update inode bitmap accordingly and parent inode to point to this inode
        // is there space in our bitmaps?
        // is there space for our new inode and datablock to be inserted?
        // new inode is of type directory, will have references to . and .. in datablock
        // need to allocate both an inode and a datablock
    }
    return 0;
}

static int wfs_unlink(){
    printf("In wfs_unlink\n");
    return 0;
}

static int wfs_rmdir(){
    printf("In wfs_rmdir\n");
    return 0;
}

static int wfs_read(){
    printf("In wfs_read\n");
    return 0;
}

static int wfs_write(){
    printf("In wfs_write\n");
    return 0;
}

static int wfs_readdir(){
    printf("In wfs_readdir\n");
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