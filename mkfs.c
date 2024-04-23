#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  

int main(int argc, char* argv[]){
    // cann take -d -i and -b flags
    char* disk_img; // -d
    int num_inodes; // -i
    int num_datablocks; // -b

    int opt;
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
    printf("disk_img: %s, num_inodes: %d, num_datablocks: %d\n", disk_img, num_inodes, num_datablocks);
    // disk img is file to write struct to
    // write superblock to struct

}