#include "wfs.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[]){
    char* disk_path;
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

    return -1;
}