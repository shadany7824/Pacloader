#include "guestTls.hpp"

#include "../log/log.h"
#include "../../minhook/src/hde/hde32.h"

#include <windows.h>
#include <winnt.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace
{
using NtSetLdtEntriesFn = LONG (NTAPI *)(ULONG, LDT_ENTRY, ULONG, LDT_ENTRY);
using NtSetInformationProcessFn = LONG (NTAPI *)(HANDLE, ULONG, void *, ULONG);

constexpr LONG StatusNotImplemented = static_cast<LONG>(0xc0000002u);
constexpr ULONG ProcessLdtInformation = 10;

struct ProcessLdtInformationBuffer
{
    ULONG startSelector;
    ULONG length;
    LDT_ENTRY entries[1];
};

struct LinuxTlsIndex
{
    uint32_t module;
    uint32_t offset;
};

struct GuestTlsState
{
    void *allocation = nullptr;
    uintptr_t threadPointer = 0;
    WORD selector = 0;
    WORD hostSelector = 0;
    uint8_t *instructionScratch = nullptr;
    uintptr_t continuation = 0;
    unsigned int adjustedRegister = 0;
    uint32_t originalRegister = 0;
    bool restoreRegister = false;
    bool emulatingInstruction = false;
};

std::once_flag g_ntSetLdtEntriesOnce;
NtSetLdtEntriesFn g_ntSetLdtEntries = nullptr;
NtSetInformationProcessFn g_ntSetInformationProcess = nullptr;
std::atomic<unsigned int> g_nextLdtIndex{1};
std::atomic<bool> g_useFsOverride{false};
thread_local GuestTlsState *g_currentState = nullptr;
std::once_flag g_exceptionHandlerOnce;
std::mutex g_trampolineMutex;
std::unordered_map<void *, void *> g_hostTrampolines;
std::atomic<bool> g_reportedUnhandledAccessViolation{false};
std::atomic<bool> g_reportedUnhandledFault{false};

struct GuestReturnStack
{
    uint32_t addresses[128]{};
    unsigned int depth = 0;
};

thread_local GuestReturnStack g_guestReturns;

void loadNtSetLdtEntries()
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll)
        ntdll = LoadLibraryA("ntdll.dll");
    if (ntdll)
    {
        g_ntSetLdtEntries = reinterpret_cast<NtSetLdtEntriesFn>(
            GetProcAddress(ntdll, "NtSetLdtEntries"));
        g_ntSetInformationProcess = reinterpret_cast<NtSetInformationProcessFn>(
            GetProcAddress(ntdll, "NtSetInformationProcess"));
    }
}

LDT_ENTRY makeGuestTlsDescriptor(uintptr_t threadPointer)
{
    const DWORD base = static_cast<DWORD>(threadPointer);
    LDT_ENTRY descriptor{};
    descriptor.LimitLow = 0xffff;
    descriptor.BaseLow = static_cast<WORD>(base & 0xffff);
    descriptor.HighWord.Bits.BaseMid = (base >> 16) & 0xff;
    descriptor.HighWord.Bits.Type = 0x3;       // present, writable data
    descriptor.HighWord.Bits.Dpl = 3;
    descriptor.HighWord.Bits.Pres = 1;
    descriptor.HighWord.Bits.LimitHi = 0xf;
    descriptor.HighWord.Bits.Default_Big = 1;  // 32-bit segment
    descriptor.HighWord.Bits.Granularity = 1;  // 4 GiB logical limit
    descriptor.HighWord.Bits.BaseHi = (base >> 24) & 0xff;
    return descriptor;
}

void loadGs(WORD selector)
{
    __asm__ volatile("movw %0, %%gs" : : "rm"(selector) : "memory");
}

extern "C" void pushGuestReturnAndEnterHost(uint32_t address)
{
    if (g_guestReturns.depth < sizeof(g_guestReturns.addresses) /
                                   sizeof(g_guestReturns.addresses[0]))
        g_guestReturns.addresses[g_guestReturns.depth++] = address;
    else
        log_fatal("Guest TLS: host-call trampoline return stack overflow");

    if (g_currentState && !g_useFsOverride.load(std::memory_order_relaxed))
        loadGs(g_currentState->hostSelector);
}

extern "C" uint32_t popGuestReturnAndEnterGuest()
{
    if (g_guestReturns.depth == 0)
    {
        log_fatal("Guest TLS: host-call trampoline return stack underflow");
        return 0;
    }

    const uint32_t address = g_guestReturns.addresses[--g_guestReturns.depth];
    if (g_currentState && !g_useFsOverride.load(std::memory_order_relaxed))
        loadGs(g_currentState->selector);
    return address;
}

DWORD getHostTeb()
{
    DWORD teb = 0;
    __asm__ volatile("movl %%fs:0x18, %0" : "=r"(teb));
    return teb;
}

DWORD *contextRegister(CONTEXT *context, unsigned int index)
{
    switch (index)
    {
        case 0: return &context->Eax;
        case 1: return &context->Ecx;
        case 2: return &context->Edx;
        case 3: return &context->Ebx;
        case 4: return &context->Esp;
        case 5: return &context->Ebp;
        case 6: return &context->Esi;
        case 7: return &context->Edi;
        default: return nullptr;
    }
}

bool isInstructionPrefix(uint8_t byte)
{
    return byte == PREFIX_LOCK || byte == PREFIX_REPNZ || byte == PREFIX_REPX ||
           byte == PREFIX_SEGMENT_CS || byte == PREFIX_SEGMENT_SS ||
           byte == PREFIX_SEGMENT_DS || byte == PREFIX_SEGMENT_ES ||
           byte == PREFIX_SEGMENT_FS || byte == PREFIX_SEGMENT_GS ||
           byte == PREFIX_OPERAND_SIZE || byte == PREFIX_ADDRESS_SIZE;
}

bool registerIsWrittenByInstruction(const hde32s &instruction, unsigned int base)
{
    if ((instruction.flags & F_MODRM) == 0 || instruction.modrm_reg != base)
        return false;

    switch (instruction.opcode)
    {
        case 0x03: case 0x0b: case 0x13: case 0x1b:
        case 0x23: case 0x2b: case 0x33: case 0x8b:
            return true;
        default:
            return false;
    }
}

LONG CALLBACK emulateGuestGsInstruction(EXCEPTION_POINTERS *exception)
{
    if (!exception || !exception->ExceptionRecord || !exception->ContextRecord ||
        !g_currentState || !g_useFsOverride.load(std::memory_order_relaxed))
        return EXCEPTION_CONTINUE_SEARCH;

    CONTEXT *context = exception->ContextRecord;
    GuestTlsState &state = *g_currentState;
    const DWORD exceptionCode = exception->ExceptionRecord->ExceptionCode;

    if (exceptionCode == EXCEPTION_SINGLE_STEP && state.emulatingInstruction)
    {
        if (state.restoreRegister)
        {
            DWORD *reg = contextRegister(context, state.adjustedRegister);
            if (reg)
                *reg = state.originalRegister;
        }
        context->Eip = static_cast<DWORD>(state.continuation);
        context->EFlags &= ~0x100u;
        state.emulatingInstruction = false;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    /*
     * Anything fatal that is not an access violation used to end the process
     * with no output at all, because only the AV path reports. Bringing a new
     * title up is mostly reading these, so say where it died before letting it
     * go; the fault itself is still not handled.
     */
    if (exceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO ||
        exceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION ||
        exceptionCode == EXCEPTION_PRIV_INSTRUCTION ||
        exceptionCode == EXCEPTION_INT_OVERFLOW ||
        exceptionCode == EXCEPTION_STACK_OVERFLOW)
    {
        if (!g_reportedUnhandledFault.exchange(true))
        {
            MEMORY_BASIC_INFORMATION codeInfo{};
            ULONG_PTR moduleBase = 0;
            if (VirtualQuery(reinterpret_cast<const void *>(context->Eip), &codeInfo,
                             sizeof(codeInfo)))
                moduleBase = reinterpret_cast<ULONG_PTR>(codeInfo.AllocationBase);

            char message[4096]{};
            int length = _snprintf(
                message, sizeof(message) - 1,
                "Guest TLS: fatal exception %08lx at eip=%08lx (base=%08lx +%08lx) "
                "esp=%08lx ebp=%08lx eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx "
                "esi=%08lx edi=%08lx\r\n",
                static_cast<unsigned long>(exceptionCode),
                static_cast<unsigned long>(context->Eip),
                static_cast<unsigned long>(moduleBase),
                static_cast<unsigned long>(context->Eip - moduleBase),
                static_cast<unsigned long>(context->Esp),
                static_cast<unsigned long>(context->Ebp),
                static_cast<unsigned long>(context->Eax),
                static_cast<unsigned long>(context->Ebx),
                static_cast<unsigned long>(context->Ecx),
                static_cast<unsigned long>(context->Edx),
                static_cast<unsigned long>(context->Esi),
                static_cast<unsigned long>(context->Edi));

            /* The stack is what names the caller, which is the whole point of
             * reporting a fault in a title nobody has brought up yet. */
            if (length > 0)
                length += _snprintf(message + length, sizeof(message) - length - 1, "stack:");
            const auto *stack = reinterpret_cast<const DWORD *>(context->Esp);
            MEMORY_BASIC_INFORMATION stackInfo{};
            if (length > 0 && VirtualQuery(stack, &stackInfo, sizeof(stackInfo)) &&
                stackInfo.State == MEM_COMMIT)
            {
                for (unsigned int i = 0;
                     i < 96 && length < static_cast<int>(sizeof(message) - 16); ++i)
                    length += _snprintf(message + length, sizeof(message) - length - 1,
                                        " %08lx", static_cast<unsigned long>(stack[i]));
            }
            /*
             * Name the modules the return addresses belong to. A bare address
             * says nothing about who called into the guest, and that is the
             * question every unbrought-up title asks.
             */
            if (length > 0)
                length += _snprintf(message + length, sizeof(message) - length - 1,
                                    "callers:");
            if (length > 0 && VirtualQuery(stack, &stackInfo, sizeof(stackInfo)) &&
                stackInfo.State == MEM_COMMIT)
            {
                int named = 0;
                for (unsigned int i = 0;
                     i < 96 && named < 8 && length < static_cast<int>(sizeof(message) - 128); ++i)
                {
                    const DWORD candidate = stack[i];
                    MEMORY_BASIC_INFORMATION info{};
                    if (!VirtualQuery(reinterpret_cast<const void *>(candidate), &info,
                                      sizeof(info)) ||
                        info.State != MEM_COMMIT ||
                        (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0)
                        continue;

                    char moduleName[MAX_PATH]{};
                    if (!GetModuleFileNameA(reinterpret_cast<HMODULE>(info.AllocationBase),
                                            moduleName, sizeof(moduleName)))
                        _snprintf(moduleName, sizeof(moduleName), "guest");
                    const char *leaf = strrchr(moduleName, '\\');
                    length += _snprintf(message + length, sizeof(message) - length - 1,
                                        " %08lx=%s", static_cast<unsigned long>(candidate),
                                        leaf ? leaf + 1 : moduleName);
                    ++named;
                }
            }
            if (length > 0)
                length += _snprintf(message + length, sizeof(message) - length - 1, "\r\n");

            if (length > 0)
            {
                DWORD written = 0;
                WriteFile(GetStdHandle(STD_ERROR_HANDLE), message,
                          static_cast<DWORD>(length), &written, nullptr);
            }
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (exceptionCode != EXCEPTION_ACCESS_VIOLATION || state.emulatingInstruction ||
        !state.instructionScratch)
        return EXCEPTION_CONTINUE_SEARCH;

    const auto *source = reinterpret_cast<const uint8_t *>(context->Eip);
    hde32s instruction{};
    const unsigned int length = hde32_disasm(source, &instruction);
    if (length == 0 || length > 15 || (instruction.flags & F_ERROR) != 0 ||
        instruction.p_seg != PREFIX_SEGMENT_GS || instruction.p_67 != 0)
    {
        if (!g_reportedUnhandledAccessViolation.exchange(true))
        {
            char message[4096]{};
            const ULONG_PTR accessType = exception->ExceptionRecord->NumberParameters > 0
                                             ? exception->ExceptionRecord->ExceptionInformation[0]
                                             : 0;
            const ULONG_PTR accessAddress = exception->ExceptionRecord->NumberParameters > 1
                                                ? exception->ExceptionRecord->ExceptionInformation[1]
                                                : 0;
            /* Guest shared objects are mapped by the loader, so Windows has no
             * module name for the faulting address.  Report the allocation base
             * and the offset into it, plus the instruction bytes: together with
             * the "mapped at" line each SO logs when it loads, that is enough to
             * find the instruction with objdump. */
            MEMORY_BASIC_INFORMATION codeInfo{};
            ULONG_PTR moduleBase = 0;
            if (VirtualQuery(reinterpret_cast<const void *>(context->Eip), &codeInfo,
                             sizeof(codeInfo)))
                moduleBase = reinterpret_cast<ULONG_PTR>(codeInfo.AllocationBase);

            int messageLength = _snprintf(
                message, sizeof(message) - 1,
                "Guest TLS: unhandled AV eip=%08lx (base=%08lx +%08lx) esp=%08lx ebp=%08lx "
                "type=%lu address=%08lx "
                "eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\r\n",
                static_cast<unsigned long>(context->Eip),
                static_cast<unsigned long>(moduleBase),
                static_cast<unsigned long>(context->Eip - moduleBase),
                static_cast<unsigned long>(context->Esp),
                static_cast<unsigned long>(context->Ebp),
                static_cast<unsigned long>(accessType),
                static_cast<unsigned long>(accessAddress),
                static_cast<unsigned long>(context->Eax),
                static_cast<unsigned long>(context->Ebx),
                static_cast<unsigned long>(context->Ecx),
                static_cast<unsigned long>(context->Edx),
                static_cast<unsigned long>(context->Esi),
                static_cast<unsigned long>(context->Edi));

            if (messageLength > 0)
            {
                messageLength += _snprintf(message + messageLength,
                                           sizeof(message) - messageLength - 1, "code:");
                const auto *code = reinterpret_cast<const uint8_t *>(context->Eip) - 16;
                MEMORY_BASIC_INFORMATION probe{};
                if (VirtualQuery(code, &probe, sizeof(probe)) && probe.State == MEM_COMMIT)
                {
                    for (unsigned int i = 0; i < 32; ++i)
                        messageLength +=
                            _snprintf(message + messageLength,
                                      sizeof(message) - messageLength - 1, "%s%02x",
                                      i == 16 ? " >" : " ", code[i]);
                }
                messageLength += _snprintf(message + messageLength,
                                           sizeof(message) - messageLength - 1, "\r\nstack:");
            }
            const auto *stack = reinterpret_cast<const DWORD *>(context->Esp);
            MEMORY_BASIC_INFORMATION stackInfo{};
            if (messageLength > 0 && VirtualQuery(stack, &stackInfo, sizeof(stackInfo)) &&
                stackInfo.State == MEM_COMMIT)
            {
                for (unsigned int i = 0; i < 160 && messageLength < static_cast<int>(sizeof(message) - 16); ++i)
                    messageLength += _snprintf(message + messageLength,
                                               sizeof(message) - messageLength - 1,
                                               " %08lx", static_cast<unsigned long>(stack[i]));
            }
            /*
             * Walk the frame pointers rather than scanning the stack: a scan
             * reports every leftover word that happens to point at code, which
             * is worse than nothing when the question is who called the guest.
             */
            if (messageLength > 0)
                messageLength += _snprintf(message + messageLength,
                                           sizeof(message) - messageLength - 1, " frames:");
            {
                DWORD frame = context->Ebp;
                for (int depth = 0;
                     depth < 12 && messageLength > 0 &&
                     messageLength < static_cast<int>(sizeof(message) - 160);
                     ++depth)
                {
                    MEMORY_BASIC_INFORMATION frameInfo{};
                    if (!VirtualQuery(reinterpret_cast<const void *>(frame), &frameInfo,
                                      sizeof(frameInfo)) ||
                        frameInfo.State != MEM_COMMIT)
                        break;

                    const DWORD *slot = reinterpret_cast<const DWORD *>(frame);
                    const DWORD caller = slot[1];
                    const DWORD next = slot[0];

                    MEMORY_BASIC_INFORMATION callerInfo{};
                    if (!VirtualQuery(reinterpret_cast<const void *>(caller), &callerInfo,
                                      sizeof(callerInfo)) ||
                        callerInfo.State != MEM_COMMIT)
                        break;

                    char moduleName[MAX_PATH]{};
                    if (!GetModuleFileNameA(reinterpret_cast<HMODULE>(callerInfo.AllocationBase),
                                            moduleName, sizeof(moduleName)))
                        _snprintf(moduleName, sizeof(moduleName), "guest+%08lx",
                                  static_cast<unsigned long>(
                                      caller - reinterpret_cast<ULONG_PTR>(
                                                   callerInfo.AllocationBase)));
                    const char *leaf = strrchr(moduleName, '\\');
                    messageLength += _snprintf(message + messageLength,
                                               sizeof(message) - messageLength - 1, " %08lx=%s",
                                               static_cast<unsigned long>(caller),
                                               leaf ? leaf + 1 : moduleName);

                    if (next <= frame)
                        break;
                    frame = next;
                }
            }
            if (messageLength > 0 && messageLength < static_cast<int>(sizeof(message) - 3))
            {
                message[messageLength++] = '\r';
                message[messageLength++] = '\n';
                message[messageLength] = '\0';
            }
            DWORD written = 0;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), message,
                      messageLength > 0 ? static_cast<DWORD>(messageLength) : 0,
                      &written, nullptr);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    std::memcpy(state.instructionScratch, source, length);
    unsigned int prefixLength = 0;
    bool replacedGs = false;
    while (prefixLength < length && isInstructionPrefix(source[prefixLength]))
    {
        if (source[prefixLength] == PREFIX_SEGMENT_GS)
        {
            state.instructionScratch[prefixLength] = PREFIX_SEGMENT_DS;
            replacedGs = true;
        }
        ++prefixLength;
    }
    if (!replacedGs)
        return EXCEPTION_CONTINUE_SEARCH;

    state.restoreRegister = false;
    const uint32_t threadPointer = static_cast<uint32_t>(state.threadPointer);

    if (instruction.opcode >= 0xa0 && instruction.opcode <= 0xa3)
    {
        if (prefixLength + 5 > length)
            return EXCEPTION_CONTINUE_SEARCH;
        uint32_t address = 0;
        std::memcpy(&address, state.instructionScratch + prefixLength + 1,
                    sizeof(address));
        address += threadPointer;
        std::memcpy(state.instructionScratch + prefixLength + 1, &address,
                    sizeof(address));
    }
    else if ((instruction.flags & F_MODRM) != 0)
    {
        unsigned int opcodeLength = instruction.opcode == 0x0f ? 2u : 1u;
        if (instruction.opcode == 0x0f &&
            (instruction.opcode2 == 0x38 || instruction.opcode2 == 0x3a))
            opcodeLength = 3;
        const unsigned int modrmOffset = prefixLength + opcodeLength;
        if (modrmOffset >= length)
            return EXCEPTION_CONTINUE_SEARCH;

        unsigned int cursor = modrmOffset + 1;
        bool hasBase = true;
        unsigned int base = instruction.modrm_rm;
        if ((instruction.flags & F_SIB) != 0)
        {
            if (cursor >= length)
                return EXCEPTION_CONTINUE_SEARCH;
            ++cursor;
            base = instruction.sib_base;
            if (instruction.modrm_mod == 0 && base == 5)
                hasBase = false;
        }
        else if (instruction.modrm_mod == 0 && base == 5)
        {
            hasBase = false;
        }

        if (hasBase)
        {
            /* Altering ESP would move the exception-return stack itself. */
            if (base == 4)
                return EXCEPTION_CONTINUE_SEARCH;
            DWORD *reg = contextRegister(context, base);
            if (!reg)
                return EXCEPTION_CONTINUE_SEARCH;

            const bool writesBase = registerIsWrittenByInstruction(instruction, base);
            if ((instruction.opcode == 0x89 && instruction.modrm_reg == base) ||
                (writesBase && instruction.opcode != 0x8b))
                return EXCEPTION_CONTINUE_SEARCH;

            state.adjustedRegister = base;
            state.originalRegister = *reg;
            state.restoreRegister = !writesBase;
            *reg += threadPointer;
        }
        else
        {
            if ((instruction.flags & F_DISP32) == 0 || cursor + 4 > length)
                return EXCEPTION_CONTINUE_SEARCH;
            uint32_t displacement = 0;
            std::memcpy(&displacement, state.instructionScratch + cursor,
                        sizeof(displacement));
            displacement += threadPointer;
            std::memcpy(state.instructionScratch + cursor, &displacement,
                        sizeof(displacement));
        }
    }
    else
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    state.instructionScratch[length] = 0xcc;
    FlushInstructionCache(GetCurrentProcess(), state.instructionScratch, length + 1);
    state.continuation = static_cast<uintptr_t>(context->Eip) + length;
    state.emulatingInstruction = true;
    context->Eip = static_cast<DWORD>(reinterpret_cast<uintptr_t>(state.instructionScratch));
    context->EFlags |= 0x100u;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void installGsExceptionHandler()
{
    if (!AddVectoredExceptionHandler(1, emulateGuestGsInstruction))
        log_error("Guest TLS: failed to install the GS instruction exception handler");
}

bool initializeSoftwareTls(void *allocation, uintptr_t threadPointer)
{
    std::call_once(g_exceptionHandlerOnce, installGsExceptionHandler);
    auto *scratch = static_cast<uint8_t *>(VirtualAlloc(
        nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!scratch)
        return false;

    g_currentState = new GuestTlsState{
        allocation, threadPointer, 0, 0, scratch};
    return true;
}

WORD getHostFsSelector()
{
    WORD selector = 0;
    __asm__ volatile("movw %%fs, %0" : "=rm"(selector));
    return selector;
}

WORD getHostGsSelector()
{
    WORD selector = 0;
    __asm__ volatile("movw %%gs, %0" : "=rm"(selector));
    return selector;
}

LONG setLdtEntry(WORD selector, const LDT_ENTRY &descriptor)
{
    if (g_ntSetLdtEntries)
    {
        const LONG status = g_ntSetLdtEntries(selector, descriptor, 0, LDT_ENTRY{});
        if (status != StatusNotImplemented)
            return status;
    }

    if (!g_ntSetInformationProcess)
        return StatusNotImplemented;

    ProcessLdtInformationBuffer information{};
    information.startSelector = selector & ~0x7u;
    information.length = sizeof(LDT_ENTRY);
    information.entries[0] = descriptor;
    return g_ntSetInformationProcess(GetCurrentProcess(), ProcessLdtInformation,
                                      &information, sizeof(information));
}
}

namespace GuestTls
{
bool InstallForCurrentThread()
{
    if (g_currentState)
    {
        if (!g_useFsOverride.load(std::memory_order_relaxed))
            loadGs(g_currentState->selector);
        return true;
    }

    std::call_once(g_ntSetLdtEntriesOnce, loadNtSetLdtEntries);
    if (!g_ntSetLdtEntries && !g_ntSetInformationProcess)
    {
        log_error("Guest TLS: NtSetLdtEntries is unavailable; guest GS cannot be installed");
        return false;
    }

    /* i386 TLS relocations use offsets on both sides of the thread pointer.
     * Keeping TP one page from the allocation start let sufficiently
     * negative offsets write into an adjacent Windows heap region.  Leave a
     * generous symmetric area for all loaded ES1 modules and their runtime
     * TLS bookkeeping. */
    constexpr SIZE_T AllocationSize = 0x100000;
    constexpr SIZE_T ThreadPointerOffset = AllocationSize / 2;
    void *allocation = VirtualAlloc(nullptr, AllocationSize,
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!allocation)
    {
        log_error("Guest TLS: failed to allocate guest TLS block (error %lu)",
                  static_cast<unsigned long>(GetLastError()));
        return false;
    }

    const uintptr_t threadPointer = reinterpret_cast<uintptr_t>(allocation) +
                                    ThreadPointerOffset;
    if (g_useFsOverride.load(std::memory_order_relaxed))
        return initializeSoftwareTls(allocation, threadPointer);

    const unsigned int index = g_nextLdtIndex.fetch_add(1);
    if (index > 0x1fff)
    {
        log_error("Guest TLS: exhausted LDT selectors");
        VirtualFree(allocation, 0, MEM_RELEASE);
        return false;
    }

    /* An LDT selector has TI=1 and RPL=3. NtSetLdtEntries takes this exact
     * selector value, e.g. 0x0f for LDT entry index 1. */
    const WORD selector = static_cast<WORD>((index << 3) | 0x7);
    const LDT_ENTRY descriptor = makeGuestTlsDescriptor(threadPointer);
    const LONG status = setLdtEntry(selector, descriptor);
    if (status < 0)
    {
        MEMORY_BASIC_INFORMATION memoryInfo{};
        VirtualQuery(reinterpret_cast<void *>(getHostTeb() - 0x1000),
                     &memoryInfo, sizeof(memoryInfo));
        /* WOW64 exposes no writable LDT, so this is the ordinary path on any
         * 64-bit host rather than a failure; only the fallback failing is one. */
        log_debug("Guest TLS: NtSetLdtEntries refused selector 0x%04x (status 0x%08lx)",
                  selector, static_cast<unsigned long>(status));
        log_debug("Guest TLS: host FS selector=0x%04x TEB=%p preceding region=%p size=0x%lx state=0x%lx",
                  getHostFsSelector(), reinterpret_cast<void *>(getHostTeb()),
                  memoryInfo.BaseAddress, static_cast<unsigned long>(memoryInfo.RegionSize),
                  static_cast<unsigned long>(memoryInfo.State));
        /* Current WOW64 does not expose a writable LDT and silently refuses
         * to retain FS as GS.  Keep the private TLS allocation and execute a
         * GS-prefixed instruction from a per-thread scratch buffer, with its
         * address rebased to this thread's Linux TLS pointer. */
        if (getHostFsSelector() != 0)
        {
            g_useFsOverride.store(true, std::memory_order_relaxed);
            if (initializeSoftwareTls(allocation, threadPointer))
            {
                log_debug("Guest TLS: using per-thread WOW64 GS instruction emulation");
                return true;
            }
        }
        log_error("Guest TLS: the host offers neither an LDT selector nor GS "
                  "emulation; the guest cannot be given thread-local storage");
        VirtualFree(allocation, 0, MEM_RELEASE);
        return false;
    }

    g_currentState = new GuestTlsState{
        allocation, threadPointer, selector, getHostGsSelector()};
    log_info("Guest TLS: installed GS selector 0x%04x at %p", selector,
             reinterpret_cast<void *>(threadPointer));
    loadGs(selector);
    return true;
}

bool IsInstalled()
{
    return g_currentState != nullptr;
}

bool UsesFsOverride()
{
    return g_useFsOverride.load(std::memory_order_relaxed);
}

void EnterHostCall()
{
    if (g_currentState && !UsesFsOverride())
        loadGs(g_currentState->hostSelector);
}

void EnterGuestCode()
{
    if (g_currentState && !UsesFsOverride())
        loadGs(g_currentState->selector);
}

HostCallScope::HostCallScope()
{
    EnterHostCall();
}

HostCallScope::~HostCallScope()
{
    EnterGuestCode();
}

void *WrapHostFunction(void *target)
{
    if (!target)
        return nullptr;

    /* Software GS emulation changes only guest GS-prefixed instructions.
     * Host code therefore runs with its normal Windows segment state and a
     * trampoline would only obscure the guest return address from exception
     * unwinding. */
    if (UsesFsOverride())
        return target;

    std::lock_guard<std::mutex> lock(g_trampolineMutex);
    const auto existing = g_hostTrampolines.find(target);
    if (existing != g_hostTrampolines.end())
        return existing->second;

    /* A plain call would push a return address between the guest's and argument
     * 1.  Instead the guest's is saved on a per-thread shadow stack and replaced
     * in place, then we tail-jump, so the target sees the stack the guest built. */
    constexpr SIZE_T CodeSize = 64;
    auto *code = static_cast<uint8_t *>(VirtualAlloc(
        nullptr, CodeSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!code)
        return target;

    const uint32_t continuationAddress = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(code + 30));
    const uint32_t targetSlot = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(code + 48));
    const uint32_t enterHostSlot = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(code + 52));
    const uint32_t enterGuestSlot = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(code + 56));
    const uint32_t targetAddress = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(target));
    const uint32_t enterHostAddress = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&pushGuestReturnAndEnterHost));
    const uint32_t enterGuestAddress = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&popGuestReturnAndEnterGuest));

    size_t cursor = 0;
    code[cursor++] = 0x8b; // mov eax, [esp]
    code[cursor++] = 0x04;
    code[cursor++] = 0x24;
    code[cursor++] = 0x9c; // pushfd
    code[cursor++] = 0x60; // pushad
    code[cursor++] = 0x50; // push eax (guest return address)
    code[cursor++] = 0xff;
    code[cursor++] = 0x15; // call dword ptr [abs32]
    std::memcpy(code + cursor, &enterHostSlot, sizeof(enterHostSlot));
    cursor += sizeof(enterHostSlot);
    code[cursor++] = 0x83; // add esp, 4
    code[cursor++] = 0xc4;
    code[cursor++] = 0x04;
    code[cursor++] = 0x61; // popad
    code[cursor++] = 0x9d; // popfd
    code[cursor++] = 0xc7; // mov dword ptr [esp], continuation
    code[cursor++] = 0x04;
    code[cursor++] = 0x24;
    std::memcpy(code + cursor, &continuationAddress, sizeof(continuationAddress));
    cursor += sizeof(continuationAddress);
    code[cursor++] = 0xff;
    code[cursor++] = 0x25; // jmp dword ptr [abs32]
    std::memcpy(code + cursor, &targetSlot, sizeof(targetSlot));
    cursor += sizeof(targetSlot);

    /* Continuation starts at offset 30 and ends before the pointer slots. */
    code[cursor++] = 0x52; // push edx
    code[cursor++] = 0x50; // push eax
    code[cursor++] = 0xff;
    code[cursor++] = 0x15; // call dword ptr [abs32]
    std::memcpy(code + cursor, &enterGuestSlot, sizeof(enterGuestSlot));
    cursor += sizeof(enterGuestSlot);
    code[cursor++] = 0x89; // mov ecx, eax (guest return address)
    code[cursor++] = 0xc1;
    code[cursor++] = 0x58; // pop eax
    code[cursor++] = 0x5a; // pop edx
    code[cursor++] = 0x51; // push ecx
    code[cursor++] = 0xc3; // ret

    std::memcpy(code + 48, &targetAddress, sizeof(targetAddress));
    std::memcpy(code + 52, &enterHostAddress, sizeof(enterHostAddress));
    std::memcpy(code + 56, &enterGuestAddress, sizeof(enterGuestAddress));
    FlushInstructionCache(GetCurrentProcess(), code, CodeSize);
    g_hostTrampolines.emplace(target, code);
    return code;
}

void *GetAddress(const void *tlsIndex)
{
    if (!tlsIndex || !InstallForCurrentThread())
        return nullptr;

    const auto *index = static_cast<const LinuxTlsIndex *>(tlsIndex);
    const intptr_t offset = static_cast<int32_t>(index->offset);
    return reinterpret_cast<void *>(g_currentState->threadPointer + offset);
}
}
