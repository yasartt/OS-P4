#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <linux/msdos_fs.h>
#include <stdint.h>

#define FALSE 0
#define TRUE 1

#define SECTORSIZE 512   // bytes
#define CLUSTERSIZE  1024  // bytes

#define FAT_EOC 0x0FFFFFF8
#define FAT_START_SECTOR 32  // Define the start sector of the FAT table

int readsector(int fd, unsigned char *buf, unsigned int snum);
int writesector(int fd, unsigned char *buf, unsigned int snum);
void list_root_directory(int fd);
void display_file_ascii(int fd, const char *filename);
void display_file_binary(int fd, const char *filename);
void create_file(int fd, const char *filename);
void delete_file(int fd, const char *filename);
void write_to_file(int fd, const char *filename, int offset, int n, int data);
uint32_t get_next_cluster(int fd, uint32_t cluster);
uint32_t get_file_size(int fd, uint32_t start_cluster);
void print_help();

int main(int argc, char *argv[])
{
    char diskname[128];
    int fd;
    unsigned char sector[SECTORSIZE];

    if (argc < 3) {
        print_help();
        return 1;
    }

    strcpy(diskname, argv[1]);

    fd = open(diskname, O_SYNC | O_RDWR);
    if (fd < 0) {
        printf("could not open disk image\n");
        exit(1);
    }

    if (strcmp(argv[2], "-l") == 0) {
        list_root_directory(fd);
    } else if (strcmp(argv[2], "-r") == 0) {
        if (argc < 5) {
            print_help();
            close(fd);
            return 1;
        }
        if (strcmp(argv[3], "-a") == 0) {
            display_file_ascii(fd, argv[4]);
        } else if (strcmp(argv[3], "-b") == 0) {
            display_file_binary(fd, argv[4]);
        } else {
            print_help();
        }
    } else if (strcmp(argv[2], "-c") == 0) {
        if (argc < 4) {
            print_help();
            close(fd);
            return 1;
        }
        create_file(fd, argv[3]);
    } else if (strcmp(argv[2], "-d") == 0) {
        if (argc < 4) {
            print_help();
            close(fd);
            return 1;
        }
        delete_file(fd, argv[3]);
    } else if (strcmp(argv[2], "-w") == 0) {
        if (argc < 7) {
            print_help();
            close(fd);
            return 1;
        }
        int offset = atoi(argv[4]);
        int n = atoi(argv[5]);
        int data = atoi(argv[6]);
        write_to_file(fd, argv[3], offset, n, data);
    } else if (strcmp(argv[2], "-h") == 0) {
        print_help();
    } else {
        print_help();
    }

    close(fd);
    return 0;
}

int readsector(int fd, unsigned char *buf, unsigned int snum) {
    off_t offset;
    int n;
    offset = snum * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);
    n = read(fd, buf, SECTORSIZE);
    return (n == SECTORSIZE) ? 0 : -1;
}

int writesector(int fd, unsigned char *buf, unsigned int snum) {
    off_t offset;
    int n;
    offset = snum * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);
    n = write(fd, buf, SECTORSIZE);
    fsync(fd);
    return (n == SECTORSIZE) ? 0 : -1;
}

void list_root_directory(int fd) {
    unsigned char sector[SECTORSIZE];
    struct fat_boot_sector *boot;
    uint32_t root_cluster;

    readsector(fd, sector, 0);
    boot = (struct fat_boot_sector *)sector;
    root_cluster = boot->fat32.root_cluster;

    unsigned char cluster[CLUSTERSIZE];
    struct msdos_dir_entry *dep;

    readsector(fd, cluster, root_cluster);
    dep = (struct msdos_dir_entry *)cluster;

    for (int i = 0; i < CLUSTERSIZE / sizeof(struct msdos_dir_entry); ++i) {
        if (dep->name[0] == 0x00) break;
        if (dep->name[0] == 0xE5) {
            dep++;
            continue;
        }
        char name[9];
        char ext[4];
        strncpy(name, (char *)dep->name, 8);
        name[8] = '\0';
        strncpy(ext, (char *)dep->name + 8, 3);
        ext[3] = '\0';

        printf("%s.%s %d\n", name, ext, dep->size);
        dep++;
    }
}

void display_file_ascii(int fd, const char *filename) {
    // Implement the function to display the content of a file in ASCII form
}

void display_file_binary(int fd, const char *filename) {
    // Implement the function to display the content of a file in binary form
}

void create_file(int fd, const char *filename) {
    // Implement the function to create a file
}

void delete_file(int fd, const char *filename) {
    // Implement the function to delete a file
}

void write_to_file(int fd, const char *filename, int offset, int n, int data) {
    // Implement the function to write data to a file
}

uint32_t get_next_cluster(int fd, uint32_t cluster) {
    unsigned char sector[SECTORSIZE];
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_number = FAT_START_SECTOR + (fat_offset / SECTORSIZE);
    uint32_t entry_offset = fat_offset % SECTORSIZE;

    readsector(fd, sector, sector_number);
    uint32_t *fat_entry = (uint32_t *)(sector + entry_offset);
    return *fat_entry & 0x0FFFFFFF;
}

uint32_t get_file_size(int fd, uint32_t start_cluster) {
    uint32_t cluster = start_cluster;
    uint32_t size = 0;

    while (cluster < FAT_EOC) {
        size += CLUSTERSIZE;
        cluster = get_next_cluster(fd, cluster);
    }

    return size;
}

void print_help() {
    printf("Usage: fatmod DISKIMAGE [option] [arguments]\n");
    printf("Options:\n");
    printf("  -l                    List files in the root directory\n");
    printf("  -r -a FILENAME        Display the content of FILENAME in ASCII form\n");
    printf("  -r -b FILENAME        Display the content of FILENAME in binary form\n");
    printf("  -c FILENAME           Create a file named FILENAME in the root directory\n");
    printf("  -d FILENAME           Delete a file named FILENAME\n");
    printf("  -w FILENAME OFFSET N DATA\n");
    printf("                        Write DATA byte N times to FILENAME starting at OFFSET\n");
    printf("  -h                    Display this help message\n");
}
