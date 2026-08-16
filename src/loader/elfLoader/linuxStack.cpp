#include <cstring>
#include "linuxStack.hpp"
#include <windows.h>

// Linux auxiliary vector type constants
#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_ENTRY  9
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14
#define AT_CLKTCK 17

uint32_t LinuxStack::Setup(uint32_t size, int argc, char** argv, uint32_t* outStackBase, uint32_t* outStackLimit, uint32_t reserveSize, const ElfAuxvInfo* auxvInfo)
{
    // 1. Align size to page boundary (4KB) just to be safe
    size = (size + 4095) & ~4095;

    void* stack = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    // 2. Calculate TIB limits (High/Low)
    uint32_t highAddr = (uint32_t)stack + size;
    uint32_t lowAddr = (uint32_t)stack;

    // 4. Return these to the caller
    if (outStackBase) *outStackBase = highAddr;
    if (outStackLimit) *outStackLimit = lowAddr;
    
    uint32_t esp = highAddr - reserveSize;
    
    uint32_t* ptr = (uint32_t*)esp;

    // --- Auxiliary vector (highest addresses, pushed first) ---
    *(--ptr) = 0;           // AT_NULL value
    *(--ptr) = AT_NULL;     // AT_NULL type

    if (auxvInfo)
    {
        *(--ptr) = 100;                 *(--ptr) = AT_CLKTCK;   // Clock ticks per second
        *(--ptr) = 0;                   *(--ptr) = AT_EGID;     // Effective GID
        *(--ptr) = 0;                   *(--ptr) = AT_GID;      // GID
        *(--ptr) = 0;                   *(--ptr) = AT_EUID;     // Effective UID
        *(--ptr) = 0;                   *(--ptr) = AT_UID;      // UID
        *(--ptr) = auxvInfo->AtEntry;   *(--ptr) = AT_ENTRY;    // Entry point
        *(--ptr) = auxvInfo->AtPageSz;  *(--ptr) = AT_PAGESZ;   // Page size
        *(--ptr) = auxvInfo->AtPhnum;   *(--ptr) = AT_PHNUM;    // Number of program headers
        *(--ptr) = auxvInfo->AtPhent;   *(--ptr) = AT_PHENT;    // Program header entry size
        *(--ptr) = auxvInfo->AtPhdr;    *(--ptr) = AT_PHDR;     // Program header address
    }

    *(--ptr) = 0;  // envp terminator

    *(--ptr) = 0;  // argv end (null terminator)

    for (int i = argc - 1; i >= 0; i--)
    {
        *(--ptr) = (uint32_t)argv[i];
    }
    
    *(--ptr) = argc;  // argc

    /*
     * The SysV i386 ABI wants esp 16-byte aligned at process entry, with argc
     * at [esp]. Nothing above guarantees that, and a guest built with SSE
     * spills through movaps, which raises a general protection fault on an
     * 8-aligned frame - Windows reports it as an access violation on address
     * ffffffff. WMMT5 faults in its first such frame; the older titles happen
     * to tolerate whatever lands.
     *
     * Slide the finished block down rather than counting what was pushed, so
     * this keeps working when the block above changes. Everything in it is a
     * scalar or a pointer to memory outside the block, so nothing needs fixing
     * up after the move.
     */
    uint32_t sp = (uint32_t)ptr;
    const uint32_t misalignment = sp & 15u;
    if (misalignment)
    {
        const uint32_t used = esp - sp;
        memmove((void *)(sp - misalignment), (void *)sp, used);
        sp -= misalignment;
    }

    return sp;
}
