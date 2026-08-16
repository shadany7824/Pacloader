#pragma once

/* Guest code is SysV i386, which promises esp is 16-byte aligned at every call;
 * without that a guest function spilling through movaps faults. */

inline void callGuestVoid(void (*guestFunction)(void))
{
    __asm__ __volatile__("movl %%esp, %%ebx\n\t"
                         "andl $-16, %%esp\n\t"
                         "call *%0\n\t"
                         "movl %%ebx, %%esp\n\t"
                         :
                         : "r"(guestFunction)
                         : "ebx", "eax", "ecx", "edx", "memory", "cc");
}

inline int callGuestMain(int (*guestMain)(int, char **, char **), int argc, char **argv,
                         char **envp)
{
    /* The arguments travel in one block reached through esi, which SysV keeps
     * callee-saved; four register operands do not fit on i386. */
    void *block[4] = {reinterpret_cast<void *>(argc), argv, envp,
                      reinterpret_cast<void *>(guestMain)};
    int result = 0;
    __asm__ __volatile__("movl %%esp, %%ebx\n\t"
                         "andl $-16, %%esp\n\t"
                         "subl $4, %%esp\n\t"
                         "pushl 8(%%esi)\n\t"
                         "pushl 4(%%esi)\n\t"
                         "pushl (%%esi)\n\t"
                         "call *12(%%esi)\n\t"
                         "movl %%ebx, %%esp\n\t"
                         : "=a"(result)
                         : "S"(block)
                         : "ebx", "ecx", "edx", "memory", "cc");
    return result;
}
