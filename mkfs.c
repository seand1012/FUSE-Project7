#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  
#include "wfs.h"
#include <math.h>

FILE* fptr;

void writer_helper(void* ptr, int offset, int size){
    if(fwrite(&ptr, sizeof(struct wfs_sb), 1, fptr) != 0){
        return;
    }

    fseek(fptr, offset, SEEK_SET);
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

   

    fptr = fopen(disk_img, "w");
    if(fptr == NULL){
        return -1;
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
    int start_ibm = start_sb + sizeof(struct wfs_sb);
    int start_dbm =  start_ibm + (num_inodes * sizeof(int));
    int start_inodes = start_dbm + (num_datablocks * sizeof(int));
    int start_data = start_inodes + (num_inodes * sizeof(struct wfs_inode));

    super_block.num_inodes = num_inodes;
    super_block.num_data_blocks = num_datablocks;
    super_block.i_bitmap_ptr = start_ibm;
    super_block.d_bitmap_ptr = start_dbm;
    super_block.i_blocks_ptr = start_inodes;
    super_block.d_blocks_ptr = start_data;

    
    writer_helper(&super_block, start_ibm, sizeof(struct wfs_sb));
    writer_helper(inode_bitmap, start_dbm, sizeof(struct wfs_sb));
    writer_helper(datablock_bitmap, start_inodes, sizeof(struct wfs_sb));
    writer_helper(&super_block, start_data, sizeof(struct wfs_sb));

    fclose(fptr);
    printf("disk_img: %s, num_inodes: %d, num_datablocks: %d\n", disk_img, num_inodes, num_datablocks);
    // disk img is file to write struct to
    // write superblock to struct

}