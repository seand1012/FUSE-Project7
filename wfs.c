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

/*
    helper method that will search for and return the inode idx of the child we are looking for in a directory
    this function assumes disk_img is an open file
    returns the num associated with this child or -1 if none is found (num will point to the idx in the inode bitmap)
*/
int findChild(int parentInodeIdx, char* child, int startInodes){
    // find inode
    struct wfs_inode parentDirectoryInode;
    fseek(disk_img, startInodes + (parentInodeIdx * BLOCK_SIZE), SEEK_SET);
    if (fread(&parentDirectoryInode, sizeof(struct wfs_inode), 1, disk_img) != 1){
        printf("Error reading parentDirectoryInode from disk img\n");
        fclose(disk_img);
        return -1;
    }
    // is this parentnode a directory?
    // if it is, explore children nodes, go through all wfs_dentry structs in the associated datablock
}

void* traversal(const char* path){
    char* tok_path = strtok(path, "/");

    return NULL;
}

static int wfs_getattr(const char *path, struct stat *stbuf){
    printf("In wfs_getattr\n");
    disk_img = fopen(disk_path, "r");
    // print contents of superblock - should be at offset 0
    if (!disk_img){
        printf("ERROR opening disk image in wfs_getattr\n");
        return -1;
    }
    struct wfs_sb superblock;
    
    fseek(disk_img, 0, SEEK_SET);
    if (fread(&superblock, sizeof(struct wfs_sb), 1, disk_img) != 1){
        printf("Error reading superblock from disk img\n");
        fclose(disk_img);
        return -1;
    }
    superblock.d_blocks_ptr
    fclose(disk_img);
    printf("Superblock: num_inodes=%ld, num_data_blocks=%ld\n", superblock.num_inodes, superblock.num_data_blocks);
    
    return 0;
}

// creating a file
static int wfs_mknod(){
    printf("In wfs_mknod\n");
    return 0;
}

// creating a directory
static int wfs_mkdir(){
    printf("In wfs_mkdir\n");
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