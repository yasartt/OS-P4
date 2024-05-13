#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#define SECTOR_SIZE 512
#define CLUSTER_SIZE 1024 // 2 sectors per cluster
#define ROOT_DIR_CLUSTER 2
#define ENTRY_SIZE 32
#define FAT_START_CLUSTER 32
#define EOF_CLUSTER 0x0FFFFFF8

typedef struct {
    char name[11]; // 8 for name + 3 for extension
    uint8_t attr;
    uint8_t reserved[10];
    uint16_t start; // starting cluster of the file
    uint32_t size;  // size of the file
} DirectoryEntry;

// Function Prototypes
int readsector(int fd, unsigned char *buf, unsigned int snum);
int writesector(int fd, unsigned char *buf, unsigned int snum);
void list_files(int fd);
void read_file_ascii(int fd, const char* filename);
void read_file_binary(int fd, const char* filename);
uint32_t read_fat_entry(int fd, uint32_t cluster);
off_t cluster_to_offset(uint32_t cluster);

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <disk image> <command> [options]\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("Failed to open disk image");
        return 1;
    }

    if (strcmp(argv[2], "-l") == 0) {
        list_files(fd);
    } else if (strcmp(argv[2], "-r") == 0 && argc > 4) {
        if (strcmp(argv[3], "-a") == 0) {
            read_file_ascii(fd, argv[4]);
        } else if (strcmp(argv[3], "-b") == 0) {
            read_file_binary(fd, argv[4]);
        }
    }

    close(fd);
    return 0;
}

void list_files(int fd) {
    unsigned char buffer[CLUSTER_SIZE];
    lseek(fd, cluster_to_offset(ROOT_DIR_CLUSTER), SEEK_SET);
    read(fd, buffer, CLUSTER_SIZE);

    for (int i = 0; i < CLUSTER_SIZE / ENTRY_SIZE; i++) {
        DirectoryEntry *entry = (DirectoryEntry *)(buffer + i * ENTRY_SIZE);
        if (entry->name[0] == 0x00) break; // End of directory
        if (entry->name[0] == 0xE5) continue; // Deleted file
        printf("%s %d\n", entry->name, entry->size);
    }
}

void read_file_ascii(int fd, const char *filename) {
    DirectoryEntry entry;
    unsigned char buffer[CLUSTER_SIZE];
    int found = 0;

    lseek(fd, cluster_to_offset(ROOT_DIR_CLUSTER), SEEK_SET);
    read(fd, &entry, sizeof(entry));

    while (strncmp(entry.name, filename, strlen(filename)) == 0) {
        found = 1;
        uint32_t cluster = entry.start;

        while (cluster < EOF_CLUSTER) {
            lseek(fd, cluster_to_offset(cluster), SEEK_SET);
            read(fd, buffer, CLUSTER_SIZE);
            printf("%.*s", entry.size, buffer); // Only print the file size amount of data
            cluster = read_fat_entry(fd, cluster);
        }

        if (found) break;
    }

    if (!found) {
        printf("File not found.\n");
    }
}

void read_file_binary(int fd, const char *filename) {
    DirectoryEntry entry;
    unsigned char buffer[CLUSTER_SIZE];
    int found = 0;

    lseek(fd, cluster_to_offset(ROOT_DIR_CLUSTER), SEEK_SET);
    read(fd, &entry, sizeof(entry));

    while (strncmp(entry.name, filename, strlen(filename)) == 0) {
        found = 1;
        uint32_t cluster = entry.start;

        while (cluster < EOF_CLUSTER) {
            lseek(fd, cluster_to_offset(cluster), SEEK_SET);
            read(fd, buffer, CLUSTER_SIZE);
            for (int i = 0; i < entry.size; i++) {
                printf("%02X ", buffer[i]);
                if ((i + 1) % 16 == 0) printf("\n");
            }
            cluster = read_fat_entry(fd, cluster);
        }

        if (found) break;
    }

    if (!found) {
        printf("File not found.\n");
    }
}

uint32_t read_fat_entry(int fd, uint32_t cluster) {
    uint32_t entry_offset = FAT_START_CLUSTER * CLUSTER_SIZE + cluster * 4;
    uint32_t next_cluster;

    lseek(fd, entry_offset, SEEK_SET);
    read(fd, &next_cluster, sizeof(next_cluster));

    return next_cluster & 0x0FFFFFFF; // Mask the high 4 bits
}

off_t cluster_to_offset(uint32_t cluster) {
    return (cluster - 2) * CLUSTER_SIZE + (FAT_START_CLUSTER * SECTOR_SIZE);
}