#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  
#include <fcntl.h>
#include <string.h>
#include "wfs.h"
#include <math.h>

FILE* fptr;
// 1U is unsigned int
void setBit(unsigned int* bitmap, int pos) {
    *bitmap |= (1U << pos);
}

void clearBit(unsigned int* bitmap, int pos) {
    *bitmap &= (1U << pos);
}

int getBitValue(unsigned int* bitmap, int pos) {
    return (*bitmap >> pos) & 1U;
}

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

    // int inode_bitmap[num_inodes];
    // int datablock_bitmap[num_datablocks];
    //int inodes[num_inodes * sizeof(struct wfs_inode)];
   
    int start_sb = 0;
    int start_ibm = start_sb + sizeof(struct wfs_sb); // inode bitmap
    printf("Start ibm: %d\n", start_ibm);
    int len_ibm = (num_inodes/32) * sizeof(unsigned int);
    printf("Length of ibm: %d bytes, bits: %d\n", len_ibm, len_ibm*8);
    //int start_dbm =  start_ibm + len_ibm; // data bitmap
    int start_dbm = start_ibm + len_ibm;
    printf("Start dbm: %d\n", start_dbm);
    //int len_dbm = num_datablocks + sizeof(int); // changed to * instead of +
    int len_dbm = (num_datablocks / 32) * sizeof(unsigned int);
    printf("Length of data bitmap: %d bytes, bits: %d\n", len_dbm, len_dbm*8);
    int start_inodes = start_dbm + len_dbm;
    printf("Start inodes: %d\n", start_inodes);
    int start_data = start_inodes + (num_inodes * BLOCK_SIZE);
    printf("start data: %d\n", start_data);
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
    printf("super_block.d_bitmap_ptr: %ld\n", super_block.d_bitmap_ptr);
    super_block.i_blocks_ptr = start_inodes;
    super_block.d_blocks_ptr = start_data;
    // write superblock to file
    fseek(fptr, 0, SEEK_SET);
    if(fwrite(&super_block, sizeof(struct wfs_sb), 1, fptr) != 1){
        printf("error copying superblock to file\n");
    }
    
    // write inode bitmap to file
    fseek(fptr, start_ibm, SEEK_SET);
    unsigned int zero = 0;
    for (int i = 0; i < (num_inodes/32); i++){
        if (fwrite(&zero, sizeof(int), 1, fptr) != 1){
            printf("error copying inode bitmap to file\n");
        }
    }

    // write data bitmap to file
    fseek(fptr, start_dbm, SEEK_SET);
    for (int i = 0; i < (num_datablocks/32); i++){
        if (fwrite(&zero, sizeof(int), 1, fptr) != 1){
            printf("error copying inode bitmap to file\n");
        }
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
    setBit(&zero, 0);
    if (fwrite(&zero, sizeof(int), 1, fptr) != 1){
        printf("error writing to 0th index of inode bitmap\n");
    }

    // init dentry in 0th datablock with "."
    struct wfs_dentry rootDataBlock;
    char* dot = ".";
    int i;
    int dot_length = strlen(dot);
    for (i = 0 ; i < dot_length; i++){
        rootDataBlock.name[i] = dot[i];
    }
    rootDataBlock.name[i] = '\0';
    rootDataBlock.num = rootInode.num;
    fseek(fptr, start_data, SEEK_SET);
    if(fwrite(&rootDataBlock, sizeof(struct wfs_dentry), 1, fptr) != 1){
        printf("error writing the directory entry");
    }
    
    fseek(fptr, start_dbm, SEEK_SET);
    setBit(&zero, 0);
    if(fwrite(&zero, sizeof(int), 1, fptr) != 1){
        printf("error writing to bitmap");
    }

    fclose(fptr);
    printf("disk_img: %s, num_inodes: %d, num_datablocks: %d\n", disk_img, num_inodes, num_datablocks);
    // disk img is file to write struct to
    // write superblock to struct

}