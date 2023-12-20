#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <time.h>
#include <sync.h>

#include <fs/fat16.h>

#include "../tests/include/mocks.h"

#define BLOCK_SIZE 512
#define BOOTBLOCK_SIZE BLOCK_SIZE*4

FILE* filesystem = NULL;
#define DEBUG 1

/* TODO: as argv */
static const int IMG_SIZE = 32*1024*1024;

int mkfsv2_load_bootloader()
{
    char bootblock[BOOTBLOCK_SIZE];
    FILE* bootloader = fopen("bin/bootblock", "r");
    if(bootloader == NULL){
        return -1;
    }

    int size = fread(bootblock, 1, BOOTBLOCK_SIZE, bootloader);
    if(size != BOOTBLOCK_SIZE){
        return -2;
    }

    /* write 4 bootblock blocks */
    for(int i = 0; i < 4; i++){
        write_block(bootblock + (i*512), i);
    }

    fclose(bootloader);
    return 0;
}

int mkfsv2_load_kernel()
{
    int kernel_size;
    int kernel_block_count;
    
    FILE* kernel = fopen("bin/kernelout", "r");
    if(kernel == NULL){
        return -1;
    }

    fseek(kernel, 0, SEEK_END);
    kernel_size = ftell(kernel);
    kernel_block_count = kernel_size / 512;
    fseek(kernel, 0, SEEK_SET);
    printf("Kernel size: %d\n", kernel_size);

    char* kernel_data = malloc(kernel_size);
    if(kernel_data == NULL){
        return -2;
    }

    int size = fread(kernel_data, 1, kernel_size, kernel);
    if(size != kernel_size){
        return -3;
    }

    for(int i = 0; i < kernel_block_count; i++){
        write_block(kernel_data + (i*512), i+4);
    }

    fclose(kernel);
    free(kernel_data);

    return kernel_block_count;
}


int main(int argc, char const *argv[])
{
    int kernel_block_count;
    int ret;

    filesystem = fopen("filesystemv2.img", "w+");
    if(filesystem == NULL){
        printf("Unable to open filesystem.");
        return -1;
    }

    ret = mkfsv2_load_bootloader();
    if(ret < 0){
        printf("Unable to load bootloader: %d\n", ret);
        return -1;
    }

    kernel_block_count = mkfsv2_load_kernel();
    if(kernel_block_count <= 0){
        printf("Unable to load kernel: %d\n", kernel_block_count);
        return -1;
    }

    /* We want to reserve blocks for the kernel before the filesystem starts. */

    fat16_format("VOLUME1", kernel_block_count+4);
    if(fat16_load() < 0){
        printf("Unable to initialize FAT16 filesystem.\n");
        return -1;
    }


    fseek(filesystem, 0, SEEK_END);
    int size2 = ftell(filesystem);
    printf("Size of filesystem: %d\n", size2);
    /* pad to 32mb */
    if(size2 < IMG_SIZE){
        char* buf = malloc(IMG_SIZE - size2);
        memset(buf, 0, IMG_SIZE - size2);
        fwrite(buf, IMG_SIZE - size2, 1, filesystem);
        free(buf);

        printf("Padded filesystem to 32mb.\n");
    }   

    return 0;
}