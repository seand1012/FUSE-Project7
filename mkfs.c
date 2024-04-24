#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  
#include <fcntl.h>
#include <string.h>
#include "wfs.h"
#include <math.h>

FILE* fptr;


int main(int argc, char* argv[]){
    // can take -d -i and -b flags
    char* disk_img; // -d
    int num_inodes; // -i
    int num_datablocks; // -b
    
    int opt;
    struct wfs_sb super_block;
    // int opt_idx = 0;

    while ((opt = getopt(argc, argv, "d:i:b:")) != -1) {
        switch (opt) {
            case 'd':
                disk_img = optarg;
                break;
            case 'i':
                num_inodes = atoi(optarg);
                break;
            case 'b':
                num_datablocks = atoi(optarg);
                break;
            default: 
                ////printf("error, need -n, and -s flags\n");
                return -1;
        }
    } 
    
    if(num_datablocks % 32 != 0){
        num_datablocks = (int) floor((double)(num_datablocks / 32));
        num_datablocks++;
        num_datablocks *= 32;
    }

    if(num_inodes % 32 != 0){
        num_inodes = (int) floor((double) (num_inodes / 32));
        num_inodes++;
        num_inodes *= 32;
    }

    int inode_bitmap[num_inodes];
    int datablock_bitmap[num_datablocks];
    //int inodes[num_inodes * sizeof(struct wfs_inode)];
   
    int start_sb = 0;
    int start_ibm = start_sb + sizeof(struct wfs_sb); // inode bitmap
    int len_ibm = num_inodes * sizeof(int);
    int start_dbm =  start_ibm + len_ibm; // data bitmap
    int len_dbm = num_datablocks + sizeof(int);
    int start_inodes = start_dbm + len_dbm;
    int start_data = start_inodes + (num_inodes * BLOCK_SIZE);
    //int end_fs = start_data + (num_datablocks * BLOCK_SIZE);

    //in mkfs all we write to file is sb, empty bitmaps, and root inode?
     //"mkfs should write the superblock and root inode to the disk image."
    int end_init_fs = start_sb + sizeof(struct wfs_sb) + len_ibm + len_dbm + BLOCK_SIZE;
    // root inode is BLOCK_SIZE. init FS with one inode and no data?

    fptr = fopen(disk_img, "r+");
    if(fptr == NULL){
        printf("error opening disk_img\n");
        return -1;
    }

    fseek(fptr, 0, SEEK_END);
    long int file_size = ftell(fptr);
    printf("%d > %ld\n", end_init_fs, file_size);
    if (end_init_fs > file_size){
        printf("error the file system doesn't fit in disk_img: %d > %ld\n", end_init_fs, file_size);
        fclose(fptr);
        exit(1);
    }

    super_block.num_inodes = num_inodes;
    super_block.num_data_blocks = num_datablocks;
    super_block.i_bitmap_ptr = start_ibm;
    super_block.d_bitmap_ptr = start_dbm;
    super_block.i_blocks_ptr = start_inodes;
    super_block.d_blocks_ptr = start_data;
    // write superblock to file
    fseek(fptr, 0, SEEK_SET);
    if(fwrite(&super_block, sizeof(struct wfs_sb), 1, fptr) != 1){
        printf("error copying superblock to file\n");
    }
    
    // write inode bitmap to file
    fseek(fptr, start_ibm, SEEK_SET);
    if (fwrite(inode_bitmap, sizeof(int), num_inodes, fptr) != num_inodes){
        printf("error copying inode bitmap to file\n");
    }

    // write data bitmap to file
    fseek(fptr, start_dbm, SEEK_SET);
    if (fwrite(datablock_bitmap, sizeof(int), num_datablocks, fptr) != num_datablocks){
        printf("error copying data bitmap to file\n");
    }
    
    // init inode blocks in file - inodes take up BLOCK_SIZE space according to piazza
    fseek(fptr, start_inodes, SEEK_SET);
    struct wfs_inode rootInode;
    rootInode.num = 0;
    rootInode.mode = S_IFDIR;
    rootInode.uid = 0;
    rootInode.gid = 0;
    rootInode.size = BLOCK_SIZE;
    rootInode.nlinks = 0;
    rootInode.atim = 0;
    rootInode.mtim = 0;
    rootInode.ctim = 0;
    for (int i = 0; i < N_BLOCKS; i++){
        rootInode.blocks[i] = 0;
    }

    // TODO init field of inode?
    if (fwrite(&rootInode, sizeof(struct wfs_inode), 1, fptr) != 1) {
        printf("error copying root inode\n");
    }

    fseek(fptr, start_ibm, SEEK_SET);
    int one = 1;
    if (fwrite(&one, sizeof(int), 1, fptr) != 1){
        printf("error writing to 0th index of inode bitmap\n");
    }

    struct wfs_dentry rootDataBlock;
    rootDataBlock.name[0] = '.';
    rootDataBlock.num = rootInode.num;
    fseek(fptr, start_data, SEEK_SET);
    if(fwrite(&rootDataBlock, sizeof(struct wfs_dentry), 1, fptr) != 1){
        printf("error writing the directory entry");
    }
    
    fseek(fptr, start_dbm, SEEK_SET);
    if(fwrite(&one, sizeof(int), 1, fptr) != 1){
        printf("error writing to bitmap");
    }

    fclose(fptr);
    printf("disk_img: %s, num_inodes: %d, num_datablocks: %d\n", disk_img, num_inodes, num_datablocks);
    // disk img is file to write struct to
    // write superblock to struct

}