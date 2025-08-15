#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <fstream>

// Constants for file system
const unsigned int MEMORY_SIZE = 1024 * 1024; // 1MB
const unsigned int BLOCK_SIZE = 1024;         // 1KB (kept as required)
const unsigned int TOTAL_BLOCKS = MEMORY_SIZE / BLOCK_SIZE; // 1024 blocks
const unsigned int MAX_INODES = 128;
const unsigned int INODE_SIZE = 64;
const unsigned int INODE_BLOCKS = (MAX_INODES * INODE_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE;
const unsigned int FIRST_DATA_BLOCK = 1 + INODE_BLOCKS; // SuperBlock + Inode blocks
const unsigned int DIRECT_BLOCKS = 10;
const unsigned int MAX_FILENAME_LENGTH = 28;
const unsigned int MAX_PATH_LENGTH = 256;

// SuperBlock structure
struct SuperBlock {
    unsigned int magic;           // Magic number to identify the file system
    unsigned int blockSize;       // Size of each block
    unsigned int totalBlocks;     // Total number of blocks
    unsigned int freeBlocks;      // Number of free blocks
    unsigned int maxInodes;       // Maximum number of inodes
    unsigned int freeInodes;      // Number of free inodes
    unsigned int firstFreeBlock;  // First free block in free list
    unsigned int firstFreeInode;  // First free inode in free list
};

// Inode structure
struct Inode {
    unsigned int type;            // 0 = file, 1 = directory
    unsigned int size;            // Size in bytes
    time_t creationTime;          // Creation time
    time_t modificationTime;      // Last modification time
    unsigned int blockAddresses[DIRECT_BLOCKS]; // Direct block addresses
    unsigned int indirectBlock;   // Indirect block address
};

// Directory entry structure
struct DirectoryEntry {
    char name[MAX_FILENAME_LENGTH];
    unsigned int inodeNumber;
};

class FileSystem {
private:
    char* memory;                 // File system memory
    unsigned int currentInodeNumber; // Current directory inode number
    std::string currentPath;      // Current path

    // Helper functions
    void initializeFileSystem();
    void loadFileSystem();
    void saveFileSystem();
    
    unsigned int allocateBlock();
    void deallocateBlock(unsigned int blockNum);
    
    unsigned int allocateInode();
    void deallocateInode(unsigned int inodeNum);
    
    Inode readInode(unsigned int inodeNum);
    void writeInode(unsigned int inodeNum, const Inode& inode);
    
    std::vector<DirectoryEntry> readDirectoryEntries(unsigned int inodeNum);
    bool addDirectoryEntry(unsigned int dirInodeNum, const std::string& name, unsigned int inodeNum);
    bool removeDirectoryEntry(unsigned int dirInodeNum, const std::string& name);
    int findDirectoryEntry(unsigned int dirInodeNum, const std::string& name);
    
    void initializeDirectory(unsigned int dirInodeNum, unsigned int parentInodeNum);
    
    int getInodeFromPath(const std::string& path);
    std::vector<std::string> parsePath(const std::string& path);
    std::pair<int, std::string> getParentInodeAndFilename(const std::string& path);

public:
    FileSystem();
    ~FileSystem();
    void run();
    
    // Command functions
    void cmdTouch(const std::string& filename, unsigned int size);
    void cmdRm(const std::string& filename);
    void cmdMkdir(const std::string& dirname);
    void cmdRmdir(const std::string& dirname);
    void cmdCd(const std::string& path);
    void cmdLs(const std::string& path);
    void cmdCopyFile(const std::string& src, const std::string& dest);
    void cmdSum();
    void cmdCat(const std::string& filename);
    void cmdDebug(); // Added debug command
};

FileSystem::FileSystem() {
    memory = new char[MEMORY_SIZE];
    initializeFileSystem();
    loadFileSystem();
}

FileSystem::~FileSystem() {
    saveFileSystem();
    delete[] memory;
}

void FileSystem::initializeFileSystem() {
    // Initialize memory
    memset(memory, 0, MEMORY_SIZE);
    
    // Initialize SuperBlock
    SuperBlock* superBlock = reinterpret_cast<SuperBlock*>(memory);
    superBlock->magic = 0x12345678;
    superBlock->blockSize = BLOCK_SIZE;
    superBlock->totalBlocks = TOTAL_BLOCKS;
    superBlock->freeBlocks = TOTAL_BLOCKS - FIRST_DATA_BLOCK;
    superBlock->maxInodes = MAX_INODES;
    superBlock->freeInodes = MAX_INODES - 1; // Reserve inode 0 for root directory
    superBlock->firstFreeBlock = FIRST_DATA_BLOCK; // This should be FIRST_DATA_BLOCK, not 0
    superBlock->firstFreeInode = 1; // Inode 0 is reserved for root directory

    // Initialize free block list
    for (unsigned int i = FIRST_DATA_BLOCK; i < TOTAL_BLOCKS - 1; i++) {
        unsigned int* nextFree = reinterpret_cast<unsigned int*>(memory + i * BLOCK_SIZE);
        *nextFree = i + 1;
    }
    // Last block points to 0 (end of list)
    unsigned int* lastBlock = reinterpret_cast<unsigned int*>(memory + (TOTAL_BLOCKS - 1) * BLOCK_SIZE);
    *lastBlock = 0;
    
    // Initialize free inode list
    for (unsigned int i = 1; i < MAX_INODES - 1; i++) {
        Inode* inode = reinterpret_cast<Inode*>(memory + BLOCK_SIZE + i * INODE_SIZE);
        inode->indirectBlock = i + 1; // Use indirectBlock field to store next free inode
    }
    // Last inode points to 0 (end of list)
    Inode* lastInode = reinterpret_cast<Inode*>(memory + BLOCK_SIZE + (MAX_INODES - 1) * INODE_SIZE);
    lastInode->indirectBlock = 0;
    
    // Initialize root directory
    Inode rootInode;
    rootInode.type = 1; // Directory
    rootInode.size = 0;
    rootInode.creationTime = time(nullptr);
    rootInode.modificationTime = rootInode.creationTime;
    memset(rootInode.blockAddresses, 0, sizeof(rootInode.blockAddresses));
    
    // Allocate a block for root directory
    unsigned int rootBlock = allocateBlock();
    rootInode.blockAddresses[0] = rootBlock;
    rootInode.indirectBlock = 0;
    
    // Write root inode
    writeInode(0, rootInode);
    
    // Initialize root directory with . and .. entries
    initializeDirectory(0, 0);
    
    // Set current directory to root
    currentInodeNumber = 0;
    currentPath = "/";
}

void FileSystem::loadFileSystem() {
    std::ifstream file("filesystem.dat", std::ios::binary);
    if (file) {
        file.read(memory, MEMORY_SIZE);
        file.close();
        
        // Set current directory to root
        currentInodeNumber = 0;
        currentPath = "/";
    }
}

void FileSystem::saveFileSystem() {
    std::ofstream file("filesystem.dat", std::ios::binary);
    if (file) {
        file.write(memory, MEMORY_SIZE);
        file.close();
    }
}

unsigned int FileSystem::allocateBlock() {
    SuperBlock* superBlock = reinterpret_cast<SuperBlock*>(memory);
    
    if (superBlock->freeBlocks == 0 || superBlock->firstFreeBlock == 0) {
        std::cout << "Debug: No free blocks available. Free blocks: " << superBlock->freeBlocks 
                  << ", First free block: " << superBlock->firstFreeBlock << std::endl;
        return 0; // No free blocks
    }
    
    unsigned int blockNum = superBlock->firstFreeBlock;
    
    // FIXED: Check if block number is valid
    if (blockNum >= TOTAL_BLOCKS) {
        std::cout << "Debug: Invalid block number in free list: " << blockNum << std::endl;
        return 0;
    }
    
    unsigned int* nextFree = reinterpret_cast<unsigned int*>(memory + blockNum * BLOCK_SIZE);
    superBlock->firstFreeBlock = *nextFree;
    superBlock->freeBlocks--;
    
    // Clear the allocated block
    memset(memory + blockNum * BLOCK_SIZE, 0, BLOCK_SIZE);
    
    return blockNum;
}

void FileSystem::deallocateBlock(unsigned int blockNum) {
    if (blockNum < FIRST_DATA_BLOCK || blockNum >= TOTAL_BLOCKS) {
        return; // Invalid block number
    }
    
    SuperBlock* superBlock = reinterpret_cast<SuperBlock*>(memory);
    
    // Add block to free list
    unsigned int* nextFree = reinterpret_cast<unsigned int*>(memory + blockNum * BLOCK_SIZE);
    *nextFree = superBlock->firstFreeBlock;
    superBlock->firstFreeBlock = blockNum;
    superBlock->freeBlocks++;
}

unsigned int FileSystem::allocateInode() {
    SuperBlock* superBlock = reinterpret_cast<SuperBlock*>(memory);
    
    if (superBlock->freeInodes == 0 || superBlock->firstFreeInode == 0) {
        return MAX_INODES; // No free inodes
    }
    
    unsigned int inodeNum = superBlock->firstFreeInode;
    Inode* inode = reinterpret_cast<Inode*>(memory + BLOCK_SIZE + inodeNum * INODE_SIZE);
    superBlock->firstFreeInode = inode->indirectBlock;
    superBlock->freeInodes--;
    
    // Initialize the allocated inode
    inode->type = 0;
    inode->size = 0;
    inode->creationTime = time(nullptr);
    inode->modificationTime = inode->creationTime;
    memset(inode->blockAddresses, 0, sizeof(inode->blockAddresses));
    inode->indirectBlock = 0;
    
    return inodeNum;
}

void FileSystem::deallocateInode(unsigned int inodeNum) {
    if (inodeNum >= MAX_INODES) {
        return; // Invalid inode number
    }
    
    SuperBlock* superBlock = reinterpret_cast<SuperBlock*>(memory);
    
    // Add inode to free list
    Inode* inode = reinterpret_cast<Inode*>(memory + BLOCK_SIZE + inodeNum * INODE_SIZE);
    inode->indirectBlock = superBlock->firstFreeInode;
    superBlock->firstFreeInode = inodeNum;
    superBlock->freeInodes++;
}

Inode FileSystem::readInode(unsigned int inodeNum) {
    Inode inode;
    
    if (inodeNum >= MAX_INODES) {
        // Return empty inode for invalid inode number
        memset(&inode, 0, sizeof(Inode));
        return inode;
    }
    
    // Read inode from memory
    memcpy(&inode, memory + BLOCK_SIZE + inodeNum * INODE_SIZE, sizeof(Inode));
    
    return inode;
}

void FileSystem::writeInode(unsigned int inodeNum, const Inode& inode) {
    if (inodeNum >= MAX_INODES) {
        return; // Invalid inode number
    }
    
    // Write inode to memory
    memcpy(memory + BLOCK_SIZE + inodeNum * INODE_SIZE, &inode, sizeof(Inode));
}

std::vector<DirectoryEntry> FileSystem::readDirectoryEntries(unsigned int inodeNum) {
    std::vector<DirectoryEntry> entries;
    
    Inode inode = readInode(inodeNum);
    if (inode.type != 1) {
        return entries; // Not a directory
    }
    
    // Read directory entries from direct blocks
    for (unsigned int i = 0; i < DIRECT_BLOCKS; i++) {
        if (inode.blockAddresses[i] == 0) {
            continue;
        }
        
        char* blockData = memory + inode.blockAddresses[i] * BLOCK_SIZE;
        unsigned int entriesPerBlock = BLOCK_SIZE / sizeof(DirectoryEntry);
        
        for (unsigned int j = 0; j < entriesPerBlock; j++) {
            DirectoryEntry* entry = reinterpret_cast<DirectoryEntry*>(blockData + j * sizeof(DirectoryEntry));
            
            if (entry->inodeNumber != 0) {
                entries.push_back(*entry);
            }
        }
    }
    
    // Read directory entries from indirect blocks
    if (inode.indirectBlock != 0) {
        unsigned int* indirectBlockData = reinterpret_cast<unsigned int*>(memory + inode.indirectBlock * BLOCK_SIZE);
        
        for (unsigned int i = 0; i < BLOCK_SIZE / sizeof(unsigned int); i++) {
            if (indirectBlockData[i] == 0) {
                continue;
            }
            
            char* blockData = memory + indirectBlockData[i] * BLOCK_SIZE;
            unsigned int entriesPerBlock = BLOCK_SIZE / sizeof(DirectoryEntry);
            
            for (unsigned int j = 0; j < entriesPerBlock; j++) {
                DirectoryEntry* entry = reinterpret_cast<DirectoryEntry*>(blockData + j * sizeof(DirectoryEntry));
                
                if (entry->inodeNumber != 0) {
                    entries.push_back(*entry);
                }
            }
        }
    }
    
    return entries;
}

bool FileSystem::addDirectoryEntry(unsigned int dirInodeNum, const std::string& name, unsigned int inodeNum) {
    if (name.length() >= MAX_FILENAME_LENGTH) {
        return false; // Name too long
    }
    
    Inode dirInode = readInode(dirInodeNum);
    if (dirInode.type != 1) {
        return false; // Not a directory
    }
    
    // Create new directory entry
    DirectoryEntry newEntry;
    strncpy(newEntry.name, name.c_str(), MAX_FILENAME_LENGTH - 1);
    newEntry.name[MAX_FILENAME_LENGTH - 1] = '\0';
    newEntry.inodeNumber = inodeNum;
    
    // Find a free slot in direct blocks
    for (unsigned int i = 0; i < DIRECT_BLOCKS; i++) {
        if (dirInode.blockAddresses[i] == 0) {
            // Allocate a new block
            unsigned int newBlock = allocateBlock();
            if (newBlock == 0) {
                std::cout << "Debug: Failed to allocate block for directory entry" << std::endl;
                return false; // No free blocks
            }
            
            dirInode.blockAddresses[i] = newBlock;
            writeInode(dirInodeNum, dirInode);
        }
        
        char* blockData = memory + dirInode.blockAddresses[i] * BLOCK_SIZE;
        unsigned int entriesPerBlock = BLOCK_SIZE / sizeof(DirectoryEntry);
        
        for (unsigned int j = 0; j < entriesPerBlock; j++) {
            DirectoryEntry* entry = reinterpret_cast<DirectoryEntry*>(blockData + j * sizeof(DirectoryEntry));
            
            if (entry->inodeNumber == 0) {
                // Found a free slot
                *entry = newEntry;
                
                // Update directory size
                dirInode.size += sizeof(DirectoryEntry);
                dirInode.modificationTime = time(nullptr);
                writeInode(dirInodeNum, dirInode);
                
                return true;
            }
        }
    }
    
    // No free slots in direct blocks, try indirect block
    if (dirInode.indirectBlock == 0) {
        // Allocate indirect block
        unsigned int indirectBlock = allocateBlock();
        if (indirectBlock == 0) {
            std::cout << "Debug: Failed to allocate indirect block for directory" << std::endl;
            return false; // No free blocks
        }
        
        dirInode.indirectBlock = indirectBlock;
        writeInode(dirInodeNum, dirInode);
    }
    
    unsigned int* indirectBlockData = reinterpret_cast<unsigned int*>(memory + dirInode.indirectBlock * BLOCK_SIZE);
    
    for (unsigned int i = 0; i < BLOCK_SIZE / sizeof(unsigned int); i++) {
        if (indirectBlockData[i] == 0) {
            // Allocate a new block
            unsigned int newBlock = allocateBlock();
            if (newBlock == 0) {
                std::cout << "Debug: Failed to allocate block for directory entry (indirect)" << std::endl;
                return false; // No free blocks
            }
            
            indirectBlockData[i] = newBlock;
        }
        
        char* blockData = memory + indirectBlockData[i] * BLOCK_SIZE;
        unsigned int entriesPerBlock = BLOCK_SIZE / sizeof(DirectoryEntry);
        
        for (unsigned int j = 0; j < entriesPerBlock; j++) {
            DirectoryEntry* entry = reinterpret_cast<DirectoryEntry*>(blockData + j * sizeof(DirectoryEntry));
            
            if (entry->inodeNumber == 0) {
                // Found a free slot
                *entry = newEntry;
                
                // Update directory size
                dirInode.size += sizeof(DirectoryEntry);
                dirInode.modificationTime = time(nullptr);
                writeInode(dirInodeNum, dirInode);
                
                return true;
            }
        }
    }
    
    std::cout << "Debug: No free slots for directory entry" << std::endl;
    return false; // No free slots
}

bool FileSystem::removeDirectoryEntry(unsigned int dirInodeNum, const std::string& name) {
    Inode dirInode = readInode(dirInodeNum);
    if (dirInode.type != 1) {
        return false; // Not a directory
    }
    
    // Search in direct blocks
    for (unsigned int i = 0; i < DIRECT_BLOCKS; i++) {
        if (dirInode.blockAddresses[i] == 0) {
            continue;
        }
        
        char* blockData = memory + dirInode.blockAddresses[i] * BLOCK_SIZE;
        unsigned int entriesPerBlock = BLOCK_SIZE / sizeof(DirectoryEntry);
        
        for (unsigned int j = 0; j < entriesPerBlock; j++) {
            DirectoryEntry* entry = reinterpret_cast<DirectoryEntry*>(blockData + j * sizeof(DirectoryEntry));
            
            if (entry->inodeNumber != 0 && strcmp(entry->name, name.c_str()) == 0) {
                // Found the entry to remove
                entry->inodeNumber = 0;
                
                // Update directory size
                dirInode.size -= sizeof(DirectoryEntry);
                dirInode.modificationTime = time(nullptr);
                writeInode(dirInodeNum, dirInode);
                
                return true;
            }
        }
    }
    
    // Search in indirect blocks
    if (dirInode.indirectBlock != 0) {
        unsigned int* indirectBlockData = reinterpret_cast<unsigned int*>(memory + dirInode.indirectBlock * BLOCK_SIZE);
        
        for (unsigned int i = 0; i < BLOCK_SIZE / sizeof(unsigned int); i++) {
            if (indirectBlockData[i] == 0) {
                continue;
            }
            
            char* blockData = memory + indirectBlockData[i] * BLOCK_SIZE;
            unsigned int entriesPerBlock = BLOCK_SIZE / sizeof(DirectoryEntry);
            
            for (unsigned int j = 0; j < entriesPerBlock; j++) {
                DirectoryEntry* entry = reinterpret_cast<DirectoryEntry*>(blockData + j * sizeof(DirectoryEntry));
                
                if (entry->inodeNumber != 0 && strcmp(entry->name, name.c_str()) == 0) {
                    // Found the entry to remove
                    entry->inodeNumber = 0;
                    
                    // Update directory size
                    dirInode.size -= sizeof(DirectoryEntry);
                    dirInode.modificationTime = time(nullptr);
                    writeInode(dirInodeNum, dirInode);
                    
                    return true;
                }
            }
        }
    }
    
    return false; // Entry not found
}

int FileSystem::findDirectoryEntry(unsigned int dirInodeNum, const std::string& name) {
    std::vector<DirectoryEntry> entries = readDirectoryEntries(dirInodeNum);
    
    for (const DirectoryEntry& entry : entries) {
        if (strcmp(entry.name, name.c_str()) == 0) {
            return entry.inodeNumber;
        }
    }
    
    return -1; // Entry not found
}

void FileSystem::initializeDirectory(unsigned int dirInodeNum, unsigned int parentInodeNum) {
    // Add . entry (self)
    addDirectoryEntry(dirInodeNum, ".", dirInodeNum);
    
    // Add .. entry (parent)
    addDirectoryEntry(dirInodeNum, "..", parentInodeNum);
}

std::vector<std::string> FileSystem::parsePath(const std::string& path) {
    std::vector<std::string> components;
    std::istringstream ss(path);
    std::string component;
    
    // Skip leading slash
    if (!path.empty() && path[0] == '/') {
        ss.ignore();
    }
    
    while (std::getline(ss, component, '/')) {
        if (!component.empty()) {
            components.push_back(component);
        }
    }
    
    return components;
}

int FileSystem::getInodeFromPath(const std::string& path) {
    if (path.empty()) {
        return currentInodeNumber;
    }
    
    int inodeNum;
    
    if (path[0] == '/') {
        // Absolute path
        inodeNum = 0; // Start from root
    } else {
        // Relative path
        inodeNum = currentInodeNumber;
    }
    
    std::vector<std::string> components = parsePath(path);
    
    for (const std::string& component : components) {
        if (component == ".") {
            continue;
        } else if (component == "..") {
            // Go up one directory
            if (inodeNum == 0) {
                continue; // Already at root
            }
            
            std::vector<DirectoryEntry> entries = readDirectoryEntries(inodeNum);
            bool found = false;
            
            for (const DirectoryEntry& entry : entries) {
                if (strcmp(entry.name, "..") == 0) {
                    inodeNum = entry.inodeNumber;
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                return -1; // Invalid path
            }
        } else {
            // Regular component
            int nextInode = findDirectoryEntry(inodeNum, component);
            if (nextInode == -1) {
                return -1; // Component not found
            }
            
            inodeNum = nextInode;
        }
    }
    
    return inodeNum;
}

std::pair<int, std::string> FileSystem::getParentInodeAndFilename(const std::string& path) {
    std::string dirname, basename;
    
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash == std::string::npos) {
        // No slash, file is in current directory
        dirname = ".";
        basename = path;
    } else if (lastSlash == 0) {
        // File is in root directory
        dirname = "/";
        basename = path.substr(1);
    } else {
        // File is in some directory
        dirname = path.substr(0, lastSlash);
        basename = path.substr(lastSlash + 1);
    }
    
    int parentInode = getInodeFromPath(dirname);
    
    return std::make_pair(parentInode, basename);
}

void FileSystem::run() {
    std::string command;
    
    while (true) {
        std::cout << "fs:" << currentPath << "> ";
        std::getline(std::cin, command);
        
        std::istringstream ss(command);
        std::string cmd;
        ss >> cmd;
        
        if (cmd == "exit") {
            break;
        } else if (cmd == "touch") {
            std::string filename;
            unsigned int size = 0;
            ss >> filename;
            ss >> size;
            cmdTouch(filename, size);
        } else if (cmd == "rm") {
            std::string filename;
            ss >> filename;
            cmdRm(filename);
        } else if (cmd == "mkdir") {
            std::string dirname;
            ss >> dirname;
            cmdMkdir(dirname);
        } else if (cmd == "rmdir") {
            std::string dirname;
            ss >> dirname;
            cmdRmdir(dirname);
        } else if (cmd == "cd") {
            std::string path;
            ss >> path;
            cmdCd(path);
        } else if (cmd == "ls") {
            std::string path;
            std::getline(ss, path);
            if (!path.empty() && path[0] == ' ') {
                path = path.substr(1);
            }
            cmdLs(path);
        } else if (cmd == "cp") {
            std::string src, dest;
            ss >> src >> dest;
            cmdCopyFile(src, dest);
        } else if (cmd == "sum") {
            cmdSum();
        } else if (cmd == "cat") {
            std::string filename;
            ss >> filename;
            cmdCat(filename);
        } else if (cmd == "debug") {
            // Added debug command
            cmdDebug();
        } else {
            std::cout << "Unknown command: " << cmd << "\n";
            std::cout << "Available commands: exit, touch, rm, mkdir, rmdir, cd, ls, cp, sum, cat, debug\n";
        }
    }
}

// Added debug command implementation
void FileSystem::cmdDebug() {
    SuperBlock* superBlock = reinterpret_cast<SuperBlock*>(memory);
    
    std::cout << "=== File System Debug Information ===" << std::endl;
    std::cout << "Block size: " << superBlock->blockSize << " bytes" << std::endl;
    std::cout << "Total blocks: " << superBlock->totalBlocks << std::endl;
    std::cout << "Free blocks: " << superBlock->freeBlocks << std::endl;
    std::cout << "First free block: " << superBlock->firstFreeBlock << std::endl;
    std::cout << "Total inodes: " << superBlock->maxInodes << std::endl;
    std::cout << "Free inodes: " << superBlock->freeInodes << std::endl;
    std::cout << "First free inode: " << superBlock->firstFreeInode << std::endl;
    
    // Check free block list integrity
    std::cout << "\nChecking free block list integrity..." << std::endl;
    unsigned int count = 0;
    unsigned int block = superBlock->firstFreeBlock;
    
    while (block != 0 && count < superBlock->freeBlocks) {
        if (block >= TOTAL_BLOCKS) {
            std::cout << "ERROR: Invalid block in free list: " << block << std::endl;
            break;
        }
        
        unsigned int* nextBlock = reinterpret_cast<unsigned int*>(memory + block * BLOCK_SIZE);
        block = *nextBlock;
        count++;
        
        if (count > superBlock->totalBlocks) {
            std::cout << "ERROR: Possible cycle in free block list" << std::endl;
            break;
        }
    }
    
    std::cout << "Counted " << count << " blocks in free list (should be " << superBlock->freeBlocks << ")" << std::endl;
    
    if (count != superBlock->freeBlocks) {
        std::cout << "WARNING: Free block count mismatch!" << std::endl;
    }
}

void FileSystem::cmdTouch(const std::string& filename, unsigned int size) {
    // Get parent directory and filename
    auto [parentInode, name] = getParentInodeAndFilename(filename);
    
    if (parentInode == -1) {
        std::cout << "Error: Invalid path\n";
        return;
    }
    
    // Check if file already exists
    if (findDirectoryEntry(parentInode, name) != -1) {
        std::cout << "Error: File already exists\n";
        return;
    }
    
    // FIXED: Check if file size is too large
    unsigned int blocksNeeded = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    unsigned int maxBlocks = DIRECT_BLOCKS + (BLOCK_SIZE / sizeof(unsigned int));
    
    if (blocksNeeded > maxBlocks) {
        std::cout << "Error: File size too large. Maximum size is " 
                  << maxBlocks * BLOCK_SIZE << " bytes\n";
        return;
    }
    
    // Check if we have enough free blocks
    SuperBlock* superBlock = reinterpret_cast<SuperBlock*>(memory);
    unsigned int indirectBlockNeeded = (blocksNeeded > DIRECT_BLOCKS) ? 1 : 0;
    if (superBlock->freeBlocks < blocksNeeded + indirectBlockNeeded) {
        std::cout << "Error: Not enough free blocks. Need " << blocksNeeded + indirectBlockNeeded
                  << ", have " << superBlock->freeBlocks << "\n";
        return;
    }
    
    // Allocate inode for the new file
    unsigned int newInode = allocateInode();
    if (newInode == MAX_INODES) {
        std::cout << "Error: No free inodes\n";
        return;
    }
    
    // Initialize the file inode
    Inode inode = readInode(newInode);
    inode.type = 0; // File
    inode.size = size;
    
    // Calculate number of blocks needed
    unsigned int directBlocks = std::min<unsigned int>(blocksNeeded, DIRECT_BLOCKS);
    
    // Allocate direct blocks with better error handling
    bool allocationFailed = false;
    for (unsigned int i = 0; i < directBlocks; i++) {
        unsigned int newBlock = allocateBlock();
        if (newBlock == 0) {
            allocationFailed = true;
            std::cout << "Debug: Failed to allocate direct block " << i << std::endl;
            break;
        }
        
        inode.blockAddresses[i] = newBlock;
    }
    
    // If direct block allocation failed, clean up and return
    if (allocationFailed) {
        for (unsigned int i = 0; i < directBlocks; i++) {
            if (inode.blockAddresses[i] != 0) {
                deallocateBlock(inode.blockAddresses[i]);
            }
        }
        deallocateInode(newInode);
        std::cout << "Error: Failed to allocate blocks for file\n";
        return;
    }
    
    // Allocate indirect blocks if needed
    if (blocksNeeded > DIRECT_BLOCKS) {
        // Allocate indirect block
        unsigned int indirectBlock = allocateBlock();
        if (indirectBlock == 0) {
            // Cleanup allocated blocks
            for (unsigned int i = 0; i < directBlocks; i++) {
                deallocateBlock(inode.blockAddresses[i]);
            }
            deallocateInode(newInode);
            std::cout << "Error: Failed to allocate indirect block\n";
            return;
        }
        
        inode.indirectBlock = indirectBlock;
        
        // Allocate blocks pointed to by indirect block
        unsigned int* indirectBlockData = reinterpret_cast<unsigned int*>(memory + indirectBlock * BLOCK_SIZE);
        unsigned int indirectBlocks = blocksNeeded - DIRECT_BLOCKS;
        
        for (unsigned int i = 0; i < indirectBlocks; i++) {
            unsigned int newBlock = allocateBlock();
            if (newBlock == 0) {
                // Cleanup allocated blocks
                for (unsigned int j = 0; j < i; j++) {
                    deallocateBlock(indirectBlockData[j]);
                }
                deallocateBlock(indirectBlock);
                for (unsigned int j = 0; j < directBlocks; j++) {
                    deallocateBlock(inode.blockAddresses[j]);
                }
                deallocateInode(newInode);
                std::cout << "Error: Failed to allocate indirect data block\n";
                return;
            }
            
            indirectBlockData[i] = newBlock;
        }
    }
    
    // Write the inode
    writeInode(newInode, inode);
    
    // Add entry to parent directory
    if (!addDirectoryEntry(parentInode, name, newInode)) {
        // Cleanup allocated blocks
        for (unsigned int i = 0; i < directBlocks; i++) {
            deallocateBlock(inode.blockAddresses[i]);
        }
        
        if (inode.indirectBlock != 0) {
            unsigned int* indirectBlockData = reinterpret_cast<unsigned int*>(memory + inode.indirectBlock * BLOCK_SIZE);
            unsigned int indirectBlocks = blocksNeeded - DIRECT_BLOCKS;
            
            for (unsigned int i = 0; i < indirectBlocks; i++) {
                if (indirectBlockData[i] != 0) {
                    deallocateBlock(indirectBlockData[i]);
                }
            }
            deallocateBlock(inode.indirectBlock);
        }
        
        deallocateInode(newInode);
        std::cout << "Error: Could not add directory entry\n";
        return;
    }
    
    std::cout << "Created file: " << filename << " (size: " << size << " bytes, blocks: " << blocksNeeded << ")\n";
}

void FileSystem::cmdRm(const std::string& filename) {
    // Get parent directory and filename
    auto [parentInode, name] = getParentInodeAndFilename(filename);
    
    if (parentInode == -1) {
        std::cout << "Error: Invalid path\n";
        return;
    }
    
    // Find the file
    int fileInode = findDirectoryEntry(parentInode, name);
    if (fileInode == -1) {
        std::cout << "Error: File not found\n";
        return;
    }
    
    // Check if it's a file
    Inode inode = readInode(fileInode);
    if (inode.type != 0) {
        std::cout << "Error: Not a file\n";
        return;
    }
    
    // Remove directory entry
    if (!removeDirectoryEntry(parentInode, name)) {
        std::cout << "Error: Could not remove directory entry\n";
        return;
    }
    
    // Free blocks
    for (unsigned int i = 0; i < DIRECT_BLOCKS; i++) {
        if (inode.blockAddresses[i] != 0) {
            deallocateBlock(inode.blockAddresses[i]);
        }
    }
    
    // Free indirect blocks
    if (inode.indirectBlock != 0) {
        unsigned int* indirectBlockData = reinterpret_cast<unsigned int*>(memory + inode.indirectBlock * BLOCK_SIZE);
        
        for (unsigned int i = 0; i < BLOCK_SIZE / sizeof(unsigned int); i++) {
            if (indirectBlockData[i] != 0) {
                deallocateBlock(indirectBlockData[i]);
            }
        }
        
        deallocateBlock(inode.indirectBlock);
    }
    
    // Free inode
    deallocateInode(fileInode);
    
    std::cout << "Removed file: " << filename << "\n";
}

void FileSystem::cmdMkdir(const std::string& dirname) {
    // Get parent directory and dirname
    auto [parentInode, name] = getParentInodeAndFilename(dirname);
    
    if (parentInode == -1) {
        std::cout << "Error: Invalid path\n";
        return;
    }
    
    // Check if directory already exists
    if (findDirectoryEntry(parentInode, name) != -1) {
        std::cout << "Error: Directory already exists\n";
        return;
    }
    
    // Check if we have enough free blocks
    SuperBlock* superBlock = reinterpret_cast<SuperBlock*>(memory);
    if (superBlock->freeBlocks < 1) {
        std::cout << "Error: Not enough free blocks\n";
        return;
    }
    
    // Allocate inode for the new directory
    unsigned int newInode = allocateInode();
    if (newInode == MAX_INODES) {
        std::cout << "Error: No free inodes\n";
        return;
    }
    
    // Initialize the directory inode
    Inode inode = readInode(newInode);
    inode.type = 1; // Directory
    inode.size = 0;
    
    // Allocate a block for the directory
    unsigned int newBlock = allocateBlock();
    if (newBlock == 0) {
        deallocateInode(newInode);
        std::cout << "Error: Not enough free blocks\n";
        return;
    }
    
    inode.blockAddresses[0] = newBlock;
    writeInode(newInode, inode);
    
    // Initialize directory with . and .. entries
    initializeDirectory(newInode, parentInode);
    
    // Add entry to parent directory
    if (!addDirectoryEntry(parentInode, name, newInode)) {
        // Cleanup
        deallocateBlock(newBlock);
        deallocateInode(newInode);
        std::cout << "Error: Could not add directory entry\n";
        return;
    }
    
    std::cout << "Created directory: " << dirname << "\n";
}

void FileSystem::cmdRmdir(const std::string& dirname) {
    // Get parent directory and dirname
    auto [parentInode, name] = getParentInodeAndFilename(dirname);
    
    if (parentInode == -1) {
        std::cout << "Error: Invalid path\n";
        return;
    }
    
    // Find the directory
    int dirInode = findDirectoryEntry(parentInode, name);
    if (dirInode == -1) {
        std::cout << "Error: Directory not found\n";
        return;
    }
    
    // Check if it's a directory
    Inode inode = readInode(dirInode);
    if (inode.type != 1) {
        std::cout << "Error: Not a directory\n";
        return;
    }
    
    // Check if directory is empty (only . and .. entries)
    std::vector<DirectoryEntry> entries = readDirectoryEntries(dirInode);
    if (entries.size() > 2) {
        std::cout << "Error: Directory not empty\n";
        return;
    }
    
    // Remove directory entry from parent
    if (!removeDirectoryEntry(parentInode, name)) {
        std::cout << "Error: Could not remove directory entry\n";
        return;
    }
    
    // Free blocks
    for (unsigned int i = 0; i < DIRECT_BLOCKS; i++) {
        if (inode.blockAddresses[i] != 0) {
            deallocateBlock(inode.blockAddresses[i]);
        }
    }
    
    // Free indirect blocks
    if (inode.indirectBlock != 0) {
        unsigned int* indirectBlockData = reinterpret_cast<unsigned int*>(memory + inode.indirectBlock * BLOCK_SIZE);
        
        for (unsigned int i = 0; i < BLOCK_SIZE / sizeof(unsigned int); i++) {
            if (indirectBlockData[i] != 0) {
                deallocateBlock(indirectBlockData[i]);
            }
        }
        
        deallocateBlock(inode.indirectBlock);
    }
    
    // Free inode
    deallocateInode(dirInode);
    
    std::cout << "Removed directory: " << dirname << "\n";
}

void FileSystem::cmdCd(const std::string& path) {
    if (path.empty()) {
        return;
    }
    
    int inodeNum = getInodeFromPath(path);
    if (inodeNum == -1) {
        std::cout << "Error: Invalid path\n";
        return;
    }
    
    // Check if it's a directory
    Inode inode = readInode(inodeNum);
    if (inode.type != 1) {
        std::cout << "Error: Not a directory\n";
        return;
    }
    
    // Update current directory
    currentInodeNumber = inodeNum;
    
    // Update current path
    if (path[0] == '/') {
        // Absolute path
        currentPath = path;
    } else {
        // Relative path
        if (currentPath == "/") {
            currentPath += path;
        } else {
            currentPath += "/" + path;
        }
    }
    
    // Normalize path (remove . and ..)
    std::vector<std::string> components = parsePath(currentPath);
    std::vector<std::string> normalizedComponents;
    
    for (const std::string& component : components) {
        if (component == ".") {
            continue;
        } else if (component == "..") {
            if (!normalizedComponents.empty()) {
                normalizedComponents.pop_back();
            }
        } else {
            normalizedComponents.push_back(component);
        }
    }
    
    currentPath = "/";
    for (const std::string& component : normalizedComponents) {
        if (!component.empty()) {
            currentPath += component + "/";
        }
    }
    
    // Remove trailing slash except for root
    if (currentPath.length() > 1 && currentPath.back() == '/') {
        currentPath.pop_back();
    }
}

void FileSystem::cmdLs(const std::string& path) {
    int inodeNum;
    
    if (path.empty()) {
        inodeNum = currentInodeNumber;
    } else {
        inodeNum = getInodeFromPath(path);
    }
    
    if (inodeNum == -1) {
        std::cout << "Error: Invalid path\n";
        return;
    }
    
    // Check if it's a directory
    Inode inode = readInode(inodeNum);
    if (inode.type != 1) {
        std::cout << "Error: Not a directory\n";
        return;
    }
    
    // Read directory entries
    std::vector<DirectoryEntry> entries = readDirectoryEntries(inodeNum);
    
    // Sort entries by name
    std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
        return strcmp(a.name, b.name) < 0;
    });
    
    // Print directory entries
    std::cout << "Contents of " << (path.empty() ? currentPath : path) << ":\n";
    std::cout << "Name                           Type       Size       Modified\n";
    std::cout << "------------------------------------------------------------\n";
    
    for (const DirectoryEntry& entry : entries) {
        Inode entryInode = readInode(entry.inodeNumber);
        
        // Format modification time
        char timeStr[20];
        struct tm* timeInfo = localtime(&entryInode.modificationTime);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeInfo);
        
        std::cout << std::left << std::setw(30) << entry.name;
        std::cout << std::left << std::setw(10) << (entryInode.type == 0 ? "File" : "Directory");
        std::cout << std::right << std::setw(10) << entryInode.size;
        std::cout << "  " << timeStr << "\n";
    }
}

void FileSystem::cmdCopyFile(const std::string& src, const std::string& dest) {
    // Get source file inode
    int srcInodeNum = getInodeFromPath(src);
    if (srcInodeNum == -1) {
        std::cout << "Error: Source file not found\n";
        return;
    }
    
    // Check if source is a file
    Inode srcInode = readInode(srcInodeNum);
    if (srcInode.type != 0) {
        std::cout << "Error: Source is not a file\n";
        return;
    }
    
    // Get destination parent directory and filename
    auto [destParentInode, destName] = getParentInodeAndFilename(dest);
    if (destParentInode == -1) {
        std::cout << "Error: Invalid destination path\n";
        return;
    }
    
    // Check if destination already exists
    if (findDirectoryEntry(destParentInode, destName) != -1) {
        std::cout << "Error: Destination file already exists\n";
        return;
    }
    
    // Calculate number of blocks needed
    unsigned int blocksNeeded = (srcInode.size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    unsigned int indirectBlockNeeded = (blocksNeeded > DIRECT_BLOCKS) ? 1 : 0;
    
    // Check if we have enough free blocks
    SuperBlock* superBlock = reinterpret_cast<SuperBlock*>(memory);
    if (superBlock->freeBlocks < blocksNeeded + indirectBlockNeeded) {
        std::cout << "Error: Not enough free blocks. Need " << blocksNeeded + indirectBlockNeeded 
                  << ", have " << superBlock->freeBlocks << "\n";
        return;
    }
    
    // Allocate inode for the destination file
    unsigned int destInodeNum = allocateInode();
    if (destInodeNum == MAX_INODES) {
        std::cout << "Error: No free inodes\n";
        return;
    }
    
    // Initialize the destination file inode
    Inode destInode = readInode(destInodeNum);
    destInode.type = 0; // File
    destInode.size = srcInode.size;
    
    // Calculate number of direct blocks
    unsigned int directBlocks = std::min<unsigned int>(blocksNeeded, DIRECT_BLOCKS);
    
    // Allocate direct blocks
    bool allocationFailed = false;
    for (unsigned int i = 0; i < directBlocks; i++) {
        unsigned int newBlock = allocateBlock();
        if (newBlock == 0) {
            allocationFailed = true;
            break;
        }
        
        destInode.blockAddresses[i] = newBlock;
        
        // Copy data
        if (srcInode.blockAddresses[i] != 0) {
            memcpy(memory + newBlock * BLOCK_SIZE, memory + srcInode.blockAddresses[i] * BLOCK_SIZE, BLOCK_SIZE);
        }
    }
    
    if (allocationFailed) {
        // Cleanup allocated blocks
        for (unsigned int i = 0; i < directBlocks; i++) {
            if (destInode.blockAddresses[i] != 0) {
                deallocateBlock(destInode.blockAddresses[i]);
            }
        }
        deallocateInode(destInodeNum);
        std::cout << "Error: Failed to allocate blocks for file copy\n";
        return;
    }
    
    // Allocate indirect blocks if needed
    if (blocksNeeded > DIRECT_BLOCKS) {
        // Allocate indirect block
        unsigned int indirectBlock = allocateBlock();
        if (indirectBlock == 0) {
            // Cleanup allocated blocks
            for (unsigned int i = 0; i < directBlocks; i++) {
                deallocateBlock(destInode.blockAddresses[i]);
            }
            deallocateInode(destInodeNum);
            std::cout << "Error: Failed to allocate indirect block for file copy\n";
            return;
        }
        
        destInode.indirectBlock = indirectBlock;
        
        // Copy indirect block data
        if (srcInode.indirectBlock != 0) {
            unsigned int* srcIndirectBlockData = reinterpret_cast<unsigned int*>(memory + srcInode.indirectBlock * BLOCK_SIZE);
            unsigned int* destIndirectBlockData = reinterpret_cast<unsigned int*>(memory + indirectBlock * BLOCK_SIZE);
            
            unsigned int indirectBlocks = blocksNeeded - DIRECT_BLOCKS;
            
            for (unsigned int i = 0; i < indirectBlocks; i++) {
                if (srcIndirectBlockData[i] != 0) {
                    unsigned int newBlock = allocateBlock();
                    if (newBlock == 0) {
                        // Cleanup allocated blocks
                        for (unsigned int j = 0; j < i; j++) {
                            if (destIndirectBlockData[j] != 0) {
                                deallocateBlock(destIndirectBlockData[j]);
                            }
                        }
                        deallocateBlock(indirectBlock);
                        for (unsigned int j = 0; j < directBlocks; j++) {
                            deallocateBlock(destInode.blockAddresses[j]);
                        }
                        deallocateInode(destInodeNum);
                        std::cout << "Error: Failed to allocate indirect data block for file copy\n";
                        return;
                    }
                    
                    destIndirectBlockData[i] = newBlock;
                    
                    // Copy data
                    memcpy(memory + newBlock * BLOCK_SIZE, memory + srcIndirectBlockData[i] * BLOCK_SIZE, BLOCK_SIZE);
                }
            }
        }
    }
    
    // Write the destination inode
    writeInode(destInodeNum, destInode);
    
    // Add entry to parent directory
    if (!addDirectoryEntry(destParentInode, destName, destInodeNum)) {
        // Cleanup allocated blocks
        for (unsigned int i = 0; i < directBlocks; i++) {
            deallocateBlock(destInode.blockAddresses[i]);
        }
        
        if (destInode.indirectBlock != 0) {
            unsigned int* destIndirectBlockData = reinterpret_cast<unsigned int*>(memory + destInode.indirectBlock * BLOCK_SIZE);
            unsigned int indirectBlocks = blocksNeeded - DIRECT_BLOCKS;
            
            for (unsigned int i = 0; i < indirectBlocks; i++) {
                if (destIndirectBlockData[i] != 0) {
                    deallocateBlock(destIndirectBlockData[i]);
                }
            }
            deallocateBlock(destInode.indirectBlock);
        }
        
        deallocateInode(destInodeNum);
        std::cout << "Error: Could not add directory entry\n";
        return;
    }
    
    std::cout << "Copied file: " << src << " -> " << dest << "\n";
}

void FileSystem::cmdSum() {
    SuperBlock* superBlock = reinterpret_cast<SuperBlock*>(memory);
    
    unsigned int totalBlocks = superBlock->totalBlocks;
    unsigned int freeBlocks = superBlock->freeBlocks;
    unsigned int usedBlocks = totalBlocks - freeBlocks;
    
    unsigned int totalInodes = superBlock->maxInodes;
    unsigned int freeInodes = superBlock->freeInodes;
    unsigned int usedInodes = totalInodes - freeInodes;
    
    unsigned int totalSpace = totalBlocks * BLOCK_SIZE;
    unsigned int freeSpace = freeBlocks * BLOCK_SIZE;
    unsigned int usedSpace = usedBlocks * BLOCK_SIZE;
    
    std::cout << "File System Summary:\n";
    std::cout << "-------------------\n";
    std::cout << "Total space: " << totalSpace << " bytes (" << totalBlocks << " blocks)\n";
    std::cout << "Used space: " << usedSpace << " bytes (" << usedBlocks << " blocks, " 
              << std::fixed << std::setprecision(1) << (usedBlocks * 100.0 / totalBlocks) << "%)\n";
    std::cout << "Free space: " << freeSpace << " bytes (" << freeBlocks << " blocks, " 
              << std::fixed << std::setprecision(1) << (freeBlocks * 100.0 / totalBlocks) << "%)\n";
    std::cout << "Inodes: " << usedInodes << " used, " << freeInodes << " free, " << totalInodes << " total\n";
}

void FileSystem::cmdCat(const std::string& filename) {
    // Get file inode
    int inodeNum = getInodeFromPath(filename);
    if (inodeNum == -1) {
        std::cout << "Error: File not found\n";
        return;
    }
    
    // Check if it's a file
    Inode inode = readInode(inodeNum);
    if (inode.type != 0) {
        std::cout << "Error: Not a file\n";
        return;
    }
    
    // Print file contents
    std::cout << "Contents of " << filename << " (" << inode.size << " bytes):\n";
    
    unsigned int remainingBytes = inode.size;
    
    // Read from direct blocks
    for (unsigned int i = 0; i < DIRECT_BLOCKS && remainingBytes > 0; i++) {
        if (inode.blockAddresses[i] == 0) {
            continue;
        }
        
        char* blockData = memory + inode.blockAddresses[i] * BLOCK_SIZE;
        unsigned int bytesToRead = std::min(remainingBytes, BLOCK_SIZE);
        
        // Print block data
        std::cout.write(blockData, bytesToRead);
        
        remainingBytes -= bytesToRead;
    }
    
    // Read from indirect blocks
    if (inode.indirectBlock != 0 && remainingBytes > 0) {
        unsigned int* indirectBlockData = reinterpret_cast<unsigned int*>(memory + inode.indirectBlock * BLOCK_SIZE);
        
        for (unsigned int i = 0; i < BLOCK_SIZE / sizeof(unsigned int) && remainingBytes > 0; i++) {
            if (indirectBlockData[i] == 0) {
                continue;
            }
            
            char* blockData = memory + indirectBlockData[i] * BLOCK_SIZE;
            unsigned int bytesToRead = std::min(remainingBytes, BLOCK_SIZE);
            
            // Print block data
            std::cout.write(blockData, bytesToRead);
            
            remainingBytes -= bytesToRead;
        }
    }
    
    std::cout << std::endl;
}

// Main function
int main() {
    FileSystem fs;
    fs.run();
    return 0;
}

