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
#define SECTORS_PER_CLUSTER (CLUSTERSIZE / SECTORSIZE)


#define FAT_EOC 0x0FFFFFF8
#define FAT_START_SECTOR 32  // Define the start sector of the FAT table

int readsector(int fd, unsigned char *buf, unsigned int snum);
int writesector(int fd, unsigned char *buf, unsigned int snum);
int readcluster(int fd, unsigned char *buf, unsigned int cnum);
int writecluster(int fd, unsigned char *buf, unsigned int cnum);
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
    return (n == SECTORSIZE) ? 0 : 1;
}

int writesector(int fd, unsigned char *buf, unsigned int snum) {
    off_t offset;
    int n;
    offset = snum * SECTORSIZE;
    lseek(fd, offset, SEEK_SET);
    n = write(fd, buf, SECTORSIZE);
    fsync(fd);
    return (n == SECTORSIZE) ? 0 : 1;
}

int readcluster(int fd, unsigned char *buf, unsigned int cnum) {
    unsigned int snum = cnum * SECTORS_PER_CLUSTER;  // Calculate the starting sector number
    for (int i = 0; i < SECTORS_PER_CLUSTER; i++) {
        if (readsector(fd, buf + (i * SECTORSIZE), snum + i) != 0) {
            return 1;  // If any sector read fails, return error
        }
    }
    return 0;  // Successfully read the entire cluster
}

int writecluster(int fd, unsigned char *buf, unsigned int cnum) {
    unsigned int snum = cnum * SECTORS_PER_CLUSTER;  // Calculate the starting sector number
    for (int i = 0; i < SECTORS_PER_CLUSTER; i++) {
        if (writesector(fd, buf + (i * SECTORSIZE), snum + i) != 0) {
            return 1;  // If any sector write fails, return error
        }
    }
    return 0;  // Successfully wrote the entire cluster
}


void list_root_directory(int fd) {
    unsigned char sector[SECTORSIZE];

    // Read the boot sector
    readsector(fd, sector, 0);

    // Parse the boot sector
    uint16_t sector_size = *(uint16_t *)(sector + 11);
    uint8_t sectors_per_cluster = *(uint8_t *)(sector + 13);
    uint16_t reserved_sector_count = *(uint16_t *)(sector + 14);
    uint8_t num_fats = *(uint8_t *)(sector + 16);
    uint32_t total_sectors = *(uint32_t *)(sector + 32);
    uint32_t sectors_per_fat = *(uint32_t *)(sector + 36);
    uint32_t root_cluster = *(uint32_t *)(sector + 44);

    printf("Sector Size: %d\n", sector_size);
    printf("Sectors per Cluster: %d\n", sectors_per_cluster);
    printf("Reserved Sector Count: %d\n", reserved_sector_count);
    printf("Number of FATs: %d\n", num_fats);
    printf("Total Sectors: %d\n", total_sectors);
    printf("Sectors per FAT: %d\n", sectors_per_fat);
    printf("Root Cluster: %d\n", root_cluster);

    // Calculate the start sector of the root cluster
    uint32_t root_cluster_start_sector = reserved_sector_count + (num_fats * sectors_per_fat);

    unsigned char cluster[CLUSTERSIZE];
    struct msdos_dir_entry *dep;

    // Read the root directory cluster
    readsector(fd, cluster, root_cluster_start_sector);
    // if (readcluster(fd, cluster, root_cluster) != 0) {
    //     perror("Failed to read root directory cluster");
    //     return 1;
    // }
    dep = (struct msdos_dir_entry *)cluster;

    // Iterate through directory entries
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
    unsigned char sector[SECTORSIZE];
    unsigned char cluster[CLUSTERSIZE];
    struct msdos_dir_entry *dep;

    // Read the boot sector
    if (readsector(fd, sector, 0) != 0) {
        perror("Failed to read boot sector");
        return;
    }

    // Parse the boot sector
    uint16_t reserved_sector_count = *(uint16_t *)(sector + 14);
    uint8_t num_fats = *(uint8_t *)(sector + 16);
    uint32_t sectors_per_fat = *(uint32_t *)(sector + 36);

    // Calculate the start sector of the root directory cluster
    unsigned int root_cluster_start_sector = reserved_sector_count + (num_fats * sectors_per_fat);

    // Read the root directory cluster
    if (readsector(fd, cluster, root_cluster_start_sector) != 0) {
        perror("Failed to read root directory cluster");
        return;
    }

    dep = (struct msdos_dir_entry *)cluster;

    // Locate the file in the root directory
    struct msdos_dir_entry *file_entry = NULL;
    for (int i = 0; i < CLUSTERSIZE / sizeof(struct msdos_dir_entry); ++i) {
        if (dep->name[0] == 0x00) break;  // End of directory
        if (dep->name[0] != 0xE5) {  // Valid entry
            char name[9];
            char ext[4];
            strncpy(name, (char *)dep->name, 8);
            name[8] = '\0';
            strncpy(ext, (char *)dep->name + 8, 3);
            ext[3] = '\0';

            // Remove trailing spaces
            for (int j = 7; j >= 0 && name[j] == ' '; j--) {
                name[j] = '\0';
            }
            for (int j = 2; j >= 0 && ext[j] == ' '; j--) {
                ext[j] = '\0';
            }

            // Concatenate name and extension
            char fullname[13];
            if (strlen(ext) > 0) {
                snprintf(fullname, sizeof(fullname), "%s.%s", name, ext);
            } else {
                snprintf(fullname, sizeof(fullname), "%s", name);
            }

            // Convert to uppercase for comparison
            for (char *p = fullname; *p; ++p) *p = toupper(*p);

            if (strcmp(fullname, filename) == 0) {
                file_entry = dep;
                break;
            }
        }
        dep++;
    }

    if (file_entry == NULL) {
        printf("File not found: %s\n", filename);
        return;
    }

    // Read and display the file content
    uint32_t cluster_num = file_entry->start;
    uint32_t file_size = file_entry->size;
    unsigned char file_buffer[CLUSTERSIZE];
    uint32_t read_size = 0;

    while (cluster_num < FAT_EOC) {
        unsigned int cluster_start_sector = root_cluster_start_sector + (cluster_num - 2) * 2;
        if (readsector(fd, file_buffer, cluster_start_sector) != 0) {
            perror("Failed to read file cluster");
            return;
        }
        for (int i = 0; i < CLUSTERSIZE && read_size < file_size; i++, read_size++) {
            printf("%c", file_buffer[i]);
        }
        cluster_num = get_next_cluster(fd, cluster_num);
    }
    printf("\n");
}


void display_file_binary(int fd, const char *filename) {
    unsigned char sector[SECTORSIZE];
    unsigned char cluster[CLUSTERSIZE];
    struct msdos_dir_entry *dep;

    // Read the boot sector
    if (readsector(fd, sector, 0) != 0) {
        perror("Failed to read boot sector");
        return;
    }

    // Parse the boot sector
    uint16_t reserved_sector_count = *(uint16_t *)(sector + 14);
    uint8_t num_fats = *(uint8_t *)(sector + 16);
    uint32_t sectors_per_fat = *(uint32_t *)(sector + 36);

    // Calculate the start sector of the root directory cluster
    unsigned int root_cluster_start_sector = reserved_sector_count + (num_fats * sectors_per_fat);

    // Read the root directory cluster
    if (readsector(fd, cluster, root_cluster_start_sector) != 0) {
        perror("Failed to read root directory cluster");
        return;
    }

    dep = (struct msdos_dir_entry *)cluster;

    // Locate the file in the root directory
    struct msdos_dir_entry *file_entry = NULL;
    for (int i = 0; i < CLUSTERSIZE / sizeof(struct msdos_dir_entry); ++i) {
        if (dep->name[0] == 0x00) break;  // End of directory
        if (dep->name[0] != 0xE5) {  // Valid entry
            char name[9];
            char ext[4];
            strncpy(name, (char *)dep->name, 8);
            name[8] = '\0';
            strncpy(ext, (char *)dep->name + 8, 3);
            ext[3] = '\0';

            // Remove trailing spaces
            for (int j = 7; j >= 0 && name[j] == ' '; j--) {
                name[j] = '\0';
            }
            for (int j = 2; j >= 0 && ext[j] == ' '; j--) {
                ext[j] = '\0';
            }

            // Concatenate name and extension
            char fullname[13];
            if (strlen(ext) > 0) {
                snprintf(fullname, sizeof(fullname), "%s.%s", name, ext);
            } else {
                snprintf(fullname, sizeof(fullname), "%s", name);
            }

            // Convert to uppercase for comparison
            for (char *p = fullname; *p; ++p) *p = toupper(*p);

            if (strcmp(fullname, filename) == 0) {
                file_entry = dep;
                break;
            }
        }
        dep++;
    }

    if (file_entry == NULL) {
        printf("File not found: %s\n", filename);
        return;
    }

    // Read and display the file content in binary (hexadecimal) form
    uint32_t cluster_num = file_entry->start;
    uint32_t file_size = file_entry->size;
    unsigned char file_buffer[CLUSTERSIZE];
    uint32_t read_size = 0;
    uint32_t offset = 0;

    while (cluster_num < FAT_EOC && read_size < file_size) {
        unsigned int cluster_start_sector = root_cluster_start_sector + (cluster_num - 2) * SECTORS_PER_CLUSTER;
        for (int i = 0; i < SECTORS_PER_CLUSTER && read_size < file_size; i++) {
            if (readsector(fd, file_buffer, cluster_start_sector + i) != 0) {
                perror("Failed to read file cluster");
                return;
            }
            for (int j = 0; j < SECTORSIZE && read_size < file_size; j++, read_size++, offset++) {
                if (offset % 16 == 0) {
                    printf("\n%08x: ", offset);
                }
                printf("%02x ", file_buffer[j]);
            }
        }
        cluster_num = get_next_cluster(fd, cluster_num);
    }
    printf("\n");
}


void create_file(int fd, const char *filename) {
    unsigned char sector[SECTORSIZE];
    unsigned char cluster[CLUSTERSIZE];
    struct msdos_dir_entry *dep;

    // Read the boot sector
    if (readsector(fd, sector, 0) != 0) {
        perror("Failed to read boot sector");
        return;
    }

    // Parse the boot sector
    uint16_t reserved_sector_count = *(uint16_t *)(sector + 14);
    uint8_t num_fats = *(uint8_t *)(sector + 16);
    uint32_t sectors_per_fat = *(uint32_t *)(sector + 36);

    // Calculate the start sector of the root directory cluster
    unsigned int root_cluster_start_sector = reserved_sector_count + (num_fats * sectors_per_fat);

    // Read the root directory cluster
    if (readsector(fd, cluster, root_cluster_start_sector) != 0) {
        perror("Failed to read root directory cluster");
        return;
    }

    dep = (struct msdos_dir_entry *)cluster;

    // Locate a free directory entry
    struct msdos_dir_entry *free_entry = NULL;
    for (int i = 0; i < CLUSTERSIZE / sizeof(struct msdos_dir_entry); ++i) {
        if (dep->name[0] == 0x00 || dep->name[0] == 0xE5) {
            free_entry = dep;
            break;
        }
        dep++;
    }

    if (free_entry == NULL) {
        printf("No free directory entry found.\n");
        return;
    }

    // Parse the filename into name and extension
    char name[9] = {0};  // 8 characters for name + null terminator
    char ext[4] = {0};   // 3 characters for extension + null terminator
    char *dot = strrchr(filename, '.');

    if (dot == NULL) {
        strncpy(name, filename, 8);  // No extension found, use entire filename
    } else {
        strncpy(name, filename, dot - filename);  // Copy name part
        strncpy(ext, dot + 1, 3);  // Copy extension part
    }

    // Pad name and extension with spaces
    for (int i = strlen(name); i < 8; i++) {
        name[i] = ' ';
    }
    for (int i = strlen(ext); i < 3; i++) {
        ext[i] = ' ';
    }

    // Create the new file entry
    memset(free_entry, 0, sizeof(struct msdos_dir_entry));
    memcpy(free_entry->name, name, 8);
    memcpy(free_entry->name + 8, ext, 3);
    free_entry->attr = ATTR_ARCH; // Archive attribute
    free_entry->start = 0; // No cluster allocated initially
    free_entry->size = 0; // Initial size 0

    // Write the updated root directory back to disk
    if (writesector(fd, cluster, root_cluster_start_sector) != 0) {
        perror("Failed to write root directory cluster");
        return;
    }

    printf("File created: %s\n", filename);
}



void delete_file(int fd, const char *filename) {
    unsigned char sector[SECTORSIZE];
    unsigned char cluster[CLUSTERSIZE];
    struct msdos_dir_entry *dep;

    // Read the boot sector
    if (readsector(fd, sector, 0) != 0) {
        perror("Failed to read boot sector");
        return;
    }

    // Parse the boot sector
    uint16_t reserved_sector_count = *(uint16_t *)(sector + 14);
    uint8_t num_fats = *(uint8_t *)(sector + 16);
    uint32_t sectors_per_fat = *(uint32_t *)(sector + 36);

    // Calculate the start sector of the root directory cluster
    unsigned int root_cluster_start_sector = reserved_sector_count + (num_fats * sectors_per_fat);

    // Read the root directory cluster
    if (readsector(fd, cluster, root_cluster_start_sector) != 0) {
        perror("Failed to read root directory cluster");
        return;
    }

    dep = (struct msdos_dir_entry *)cluster;

    // Locate the file in the root directory
    struct msdos_dir_entry *file_entry = NULL;
    for (int i = 0; i < CLUSTERSIZE / sizeof(struct msdos_dir_entry); ++i) {
        if (dep->name[0] == 0x00) break;  // End of directory
        if (dep->name[0] != 0xE5) {  // Valid entry
            char name[9];
            char ext[4];
            strncpy(name, (char *)dep->name, 8);
            name[8] = '\0';
            strncpy(ext, (char *)dep->name + 8, 3);
            ext[3] = '\0';

            // Remove trailing spaces
            for (int j = 7; j >= 0 && name[j] == ' '; j--) {
                name[j] = '\0';
            }
            for (int j = 2; j >= 0 && ext[j] == ' '; j--) {
                ext[j] = '\0';
            }

            // Concatenate name and extension
            char fullname[13];
            if (strlen(ext) > 0) {
                snprintf(fullname, sizeof(fullname), "%s.%s", name, ext);
            } else {
                snprintf(fullname, sizeof(fullname), "%s", name);
            }

            // Convert to uppercase for comparison
            for (char *p = fullname; *p; ++p) *p = toupper(*p);

            if (strcmp(fullname, filename) == 0) {
                file_entry = dep;
                break;
            }
        }
        dep++;
    }

    if (file_entry == NULL) {
        printf("File not found: %s\n", filename);
        return;
    }

    // Deallocate all clusters used by the file
    uint32_t cluster_num = le16toh(file_entry->start) | (le16toh(file_entry->starthi) << 16);
    unsigned char fat_sector[SECTORSIZE];

    while (cluster_num < FAT_EOC && cluster_num != 0) {
        uint32_t fat_offset = cluster_num * 4;
        uint32_t sector_number = reserved_sector_count + (fat_offset / SECTORSIZE);
        uint32_t entry_offset = fat_offset % SECTORSIZE;

        if (readsector(fd, fat_sector, sector_number) != 0) {
            perror("Failed to read FAT sector");
            return;
        }

        uint32_t next_cluster = *(uint32_t *)(fat_sector + entry_offset) & 0x0FFFFFFF;
        *(uint32_t *)(fat_sector + entry_offset) = 0;  // Mark cluster as free

        if (writesector(fd, fat_sector, sector_number) != 0) {
            perror("Failed to write FAT sector");
            return;
        }

        cluster_num = next_cluster;
    }

    // Mark the directory entry as deleted
    file_entry->name[0] = 0xE5;

    // Write the updated root directory back to disk
    if (writesector(fd, cluster, root_cluster_start_sector) != 0) {
        perror("Failed to write root directory cluster");
        return;
    }

    printf("File deleted: %s\n", filename);
}



void write_to_file(int fd, const char *filename, int offset, int n, int data) {
    unsigned char sector[SECTORSIZE];
    unsigned char cluster[CLUSTERSIZE];
    struct msdos_dir_entry *dep;

    // Read the boot sector
    if (readsector(fd, sector, 0) != 0) {
        perror("Failed to read boot sector");
        return;
    }

    // Parse the boot sector
    uint16_t reserved_sector_count = *(uint16_t *)(sector + 14);
    uint8_t num_fats = *(uint8_t *)(sector + 16);
    uint32_t sectors_per_fat = *(uint32_t *)(sector + 36);

    // Calculate the start sector of the root directory cluster
    unsigned int root_cluster_start_sector = reserved_sector_count + (num_fats * sectors_per_fat);

    // Read the root directory cluster
    if (readsector(fd, cluster, root_cluster_start_sector) != 0) {
        perror("Failed to read root directory cluster");
        return;
    }

    dep = (struct msdos_dir_entry *)cluster;

    // Locate the file in the root directory
    struct msdos_dir_entry *file_entry = NULL;
    for (int i = 0; i < CLUSTERSIZE / sizeof(struct msdos_dir_entry); ++i) {
        if (dep->name[0] == 0x00) break;  // End of directory
        if (dep->name[0] != 0xE5) {  // Valid entry
            char name[9];
            char ext[4];
            strncpy(name, (char *)dep->name, 8);
            name[8] = '\0';
            strncpy(ext, (char *)dep->name + 8, 3);
            ext[3] = '\0';

            // Remove trailing spaces
            for (int j = 7; j >= 0 && name[j] == ' '; j--) {
                name[j] = '\0';
            }
            for (int j = 2; j >= 0 && ext[j] == ' '; j--) {
                ext[j] = '\0';
            }

            // Concatenate name and extension
            char fullname[13];
            if (strlen(ext) > 0) {
                snprintf(fullname, sizeof(fullname), "%s.%s", name, ext);
            } else {
                snprintf(fullname, sizeof(fullname), "%s", name);
            }

            // Convert to uppercase for comparison
            for (char *p = fullname; *p; ++p) *p = toupper(*p);

            if (strcmp(fullname, filename) == 0) {
                file_entry = dep;
                break;
            }
        }
        dep++;
    }

    if (file_entry == NULL) {
        printf("File not found: %s\n", filename);
        return;
    }

    uint32_t cluster_num = le16toh(file_entry->start) | (le16toh(file_entry->starthi) << 16);
    uint32_t file_size = le32toh(file_entry->size);
    uint32_t file_offset = offset;
    unsigned char write_buffer[CLUSTERSIZE];
    memset(write_buffer, data, CLUSTERSIZE);

    while (file_offset >= CLUSTERSIZE) {
        if (cluster_num >= FAT_EOC) {
            printf("Offset exceeds file size\n");
            return;
        }
        file_offset -= CLUSTERSIZE;
        cluster_num = get_next_cluster(fd, cluster_num);
    }

    int bytes_written = 0;
    while (bytes_written < n) {
        if (cluster_num >= FAT_EOC || cluster_num == 0) {
            // Allocate new cluster
            uint32_t new_cluster = allocate_cluster(fd);
            if (new_cluster == 0) {
                printf("No free clusters available\n");
                return;
            }
            set_fat_entry(fd, cluster_num, new_cluster);
            cluster_num = new_cluster;
            set_fat_entry(fd, cluster_num, FAT_EOC);
        }

        unsigned int cluster_start_sector = root_cluster_start_sector + (cluster_num - 2) * SECTORS_PER_CLUSTER;
        if (readsector(fd, cluster, cluster_start_sector) != 0) {
            perror("Failed to read file cluster");
            return;
        }

        for (int i = file_offset; i < CLUSTERSIZE && bytes_written < n; i++, bytes_written++) {
            cluster[i] = data;
        }

        if (writesector(fd, cluster, cluster_start_sector) != 0) {
            perror("Failed to write file cluster");
            return;
        }

        file_offset = 0;  // Reset file_offset for subsequent clusters
        cluster_num = get_next_cluster(fd, cluster_num);
    }

    if (offset + n > file_size) {
        file_entry->size = htole32(offset + n);
    }

    // Write the updated root directory back to disk
    if (writesector(fd, cluster, root_cluster_start_sector) != 0) {
        perror("Failed to write root directory cluster");
        return;
    }

    printf("Data written to file: %s\n", filename);
}

uint32_t allocate_cluster(int fd) {
    unsigned char sector[SECTORSIZE];
    for (uint32_t i = 2; ; i++) {
        uint32_t fat_offset = i * 4;
        uint32_t sector_number = reserved_sector_count + (fat_offset / SECTORSIZE);
        uint32_t entry_offset = fat_offset % SECTORSIZE;

        if (readsector(fd, sector, sector_number) != 0) {
            perror("Failed to read FAT sector");
            return 0;
        }

        uint32_t *fat_entry = (uint32_t *)(sector + entry_offset);
        if ((*fat_entry & 0x0FFFFFFF) == 0) {
            *fat_entry = 0x0FFFFFF8;  // Mark cluster as allocated
            if (writesector(fd, sector, sector_number) != 0) {
                perror("Failed to write FAT sector");
                return 0;
            }
            return i;
        }
    }
}

void set_fat_entry(int fd, uint32_t cluster, uint32_t value) {
    unsigned char sector[SECTORSIZE];
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_number = reserved_sector_count + (fat_offset / SECTORSIZE);
    uint32_t entry_offset = fat_offset % SECTORSIZE;

    if (readsector(fd, sector, sector_number) != 0) {
        perror("Failed to read FAT sector");
        return;
    }

    uint32_t *fat_entry = (uint32_t *)(sector + entry_offset);
    *fat_entry = value & 0x0FFFFFFF;  // Set new cluster value

    if (writesector(fd, sector, sector_number) != 0) {
        perror("Failed to write FAT sector");
    }
}

uint32_t get_next_cluster(int fd, uint32_t cluster) {
    unsigned char sector[SECTORSIZE];
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_number = reserved_sector_count + (fat_offset / SECTORSIZE);
    uint32_t entry_offset = fat_offset % SECTORSIZE;

    if (readsector(fd, sector, sector_number) != 0) {
        perror("Failed to read FAT sector");
        return FAT_EOC;
    }

    uint32_t next_cluster = *(uint32_t *)(sector + entry_offset) & 0x0FFFFFFF;
    return next_cluster;
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
