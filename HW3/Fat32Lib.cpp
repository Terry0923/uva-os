#include <iostream>
#include <string>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

#include "myfat.h"

int ifd; // image file descriptor
bpbFat32 bpb;
int FirstDataSector;
uint32_t *FAT;
std::vector<dirEnt> cwd_ents;
std::vector<std::string> cwd;
std::vector<dirEnt> file_descs (128);

bool read_sector(int sec_num, int size, void* buf) {
    if (lseek(ifd, sec_num * bpb.bpb_bytesPerSec, SEEK_SET) == -1) {
        return 0;
    }

    if (read(ifd, buf, size) == -1) {
        return 0;
    }

    return size;
}

bool read_cluster(int cluster_num, int size, void* buf) {
    int sec_num = FirstDataSector + (cluster_num - 2) * bpb.bpb_secPerClus;

    return read_sector(sec_num, size, buf);
}

uint32_t next_cluster(uint32_t cluster_num) {
    // The high 4 bits of a FAT32 FAT entry are reserved
    return FAT[cluster_num] & 0x0FFFFFFF;
}

std::vector<dirEnt> read_dir_cluster(uint32_t cluster_num) {
    std::vector<uint32_t> clusters;

    do {
        clusters.push_back(cluster_num);
        cluster_num = next_cluster(cluster_num);
    } while (cluster_num < 0x0FFFFFF8);

    int cluster_bytes = bpb.bpb_secPerClus * bpb.bpb_bytesPerSec;
    int dirEnt_count = clusters.size() * cluster_bytes / sizeof(dirEnt);

    std::vector<dirEnt> dirEnt_list (dirEnt_count);

    // convert to bytes array to conduct arithmatic with corresponding size
    uint8_t *ptr = (uint8_t *) dirEnt_list.data();
    for (auto const& cn: clusters) {
        std::cout << cn << " cluster num\n";
        if (!read_cluster(cn, cluster_bytes, ptr)) {
            std::cerr << "Failed to read cluster:" << std::strerror(errno) << "\n";
            break;
        }
        ptr += cluster_bytes;
    }

    return dirEnt_list;
}

// split path into tokens and use / as root token
std::vector<std::string> tokenize_path(const char *path) {
    std::string s (path);

    std::vector<std::string> tokens;
    size_t pos = 0;
    std::string token;
    std::string delimiter = "/";

    // set root
    if (s.size() > 0 && s[0] == '/') {
        tokens.push_back("/");
    }

    while ((pos = s.find(delimiter)) != std::string::npos) {
        token = s.substr(0, pos);

        // ignore case of aa///bb and leading root
        if (token != "") {
            tokens.push_back(s.substr(0, pos));
        }
        s.erase(0, pos + delimiter.length());
    }

    if (s != "") {
        tokens.push_back(s);
    }

    return tokens;
}

// return the first cluster number for a given file name in a vector of dirEnts
// return 0 if cannot find the filename
uint32_t get_cluster_num(std::vector<dirEnt>& dir_ents, std::string token) {
    std::string name;
    int j;

    for (auto ent: dir_ents) {
        if (ent.dir_name[0] == 0xE5) {
            continue;
        }
        if (ent.dir_name[0] == 0x00) {
            break;
        }

        name.clear();
        j = 0;
        for(j = 0; j < 11; j++){
            if(ent.dir_name[j] == ' '){
                continue;
            }
            name.push_back(ent.dir_name[j]);
        }
        std::cout << "[" << name << "]" << name.size() << "\n";

        if (token == name) {
            return (ent.dir_fstClusHI << 8) | ent.dir_fstClusLO;
        }
    }

    return 0;
}

// modify a vector of absolute path tokens with the given path
// parse the .. .
// path tokens must begin with /, return empty tokens for exception
std::vector<std::string> merge_path_tokens(std::vector<std::string> tokens, const char* path) {
    std::vector<std::string> newtokens = tokenize_path(path);

    for (auto token: newtokens) {
        if (token == "/") {
            tokens.clear();
            tokens.push_back("/");
        } else if (token == "..") {
            tokens.pop_back();
            if (tokens.size() < 1) {
                return tokens;
            }
        } else if (token == ".") {
            continue;
        } else {
            tokens.push_back(token);
        }
    }

    return tokens;
}

// return a vector of dirEnts of the path tokens
// return empty vector if path does not exist
std::vector<dirEnt> get_path_dirEnts(const char* path) {
    std::vector<dirEnt> dir_ents = cwd_ents;
    uint32_t cluster_num;
    std::vector<std::string> path_tokens = tokenize_path(path);

    for (auto token: path_tokens) {
        if (token == "/") {
            cluster_num = bpb.bpb_RootClus;
        } else {
            cluster_num = get_cluster_num(dir_ents, token);
        }

        if (cluster_num == 0) {
            if (token == "." || token == "..") {
                // only root has no . .. file
                // 0 when .. is root
                cluster_num = bpb.bpb_RootClus;
            } else {
                dir_ents.clear();
                break;
            }
        }
        dir_ents = read_dir_cluster(cluster_num);
    }

    return dir_ents;
}

bool FAT_mount(const char *path) {
    ifd = open(path, O_RDWR);
    if (ifd == -1) {
        std::cerr << "Failed to open raw image:" << std::strerror(errno) << "\n";
        return 0;
    }

    if (read(ifd, &bpb, sizeof(bpbFat32)) == -1) {
        std::cerr << "Failed to read bdp:" << std::strerror(errno) << "\n";
        return 0;
    }

    // RootDirSectors = ((BPB_RootEntCnt * 32) + (BPB_BytsPerSec – 1)) / BPB_BytsPerSec;
    // in FAT32, BPB_RootEntCnt is always 0
    int RootDirSectors = 0;
    int FATSz = bpb.bpb_FATSz32;
    FirstDataSector = bpb.bpb_rsvdSecCnt + (bpb.bpb_numFATs * FATSz) + RootDirSectors;

    int DataSec = bpb.bpb_totSec32 - FirstDataSector;
    int CountofClusters = DataSec / bpb.bpb_secPerClus;
    if (CountofClusters < 65525) {
        std::cerr << "Invalid Fat32 cluster number:" << CountofClusters << "\n";
        return 0;
    }


    std::cout << "cluster count: " << CountofClusters << "\n";
    std::cout << "bpb rsv sec: " << bpb.bpb_rsvdSecCnt << "\n";
    std::cout << "bpb fat size: " << FATSz << "\n";

    int FATBytes = FATSz * bpb.bpb_bytesPerSec;
    FAT = (uint32_t *) malloc(FATBytes);
    if (!read_sector(bpb.bpb_rsvdSecCnt, FATBytes, FAT)) {
        std::cerr << "Failed to read FAT\n";
        return 0;
    }

    // std::cout << "FAT entry count: " << (FATBytes / sizeof(uint32_t)) << "\n";

    if (FAT_cd("/") == -1) {
        std::cerr << "Failed to set default CWD\n";
    }

    return 1;
}


int FAT_cd(const char *path) {
    std::vector<dirEnt> dir_ents = get_path_dirEnts(path);

    if (dir_ents.size() == 0) {
        return -1;
    }

    cwd_ents = dir_ents;
    return 1;
}

int FAT_open(const char *path) {
    return 0;
}

int FAT_close(int fd) {
    return 0;
}

int FAT_pread(int fildes, void *buf, int nbyte, int offset) {
    return 0;
}

dirEnt * FAT_readDir(const char *dirname) {
    // std::vector<std::string> tokens = tokenize_path(dirname);
    std::vector<dirEnt> dir_ents = get_path_dirEnts(dirname);

    if (dir_ents.size() == 0) {
        return NULL;
    }

    size_t size = sizeof(dirEnt) * dir_ents.size();
    dirEnt *buf = (dirEnt *) malloc(size);
    memcpy(buf, dir_ents.data(), size);

    return buf;
}

