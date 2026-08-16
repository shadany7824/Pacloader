#if defined(_WIN32) || defined(__MINGW32__)
#include "../redirections/filesystem.h"
#include "memoryManager.hpp"
#include "libcBridge.hpp"
#include "../redirections/libcShared.h"
#include "../log/log.h"
#include "gccBridge.hpp"
#include "networkBridge.hpp"
#include "symbolResolver.hpp"
#include "virtualDeviceRegistry.hpp"
#include "../platform/platformBackend.h"
#include "../config/config.h"
#include "../graphics/sdlCalls.h"
#include <atomic>
#include <csignal>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/time.h>
#include <sys/utime.h>
#include <wctype.h>
#include <windows.h>
#include <time.h>
#include <unistd.h>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

extern "C"
{
    // libgcc / compiler-rt internal integer division/conversion compiler builtins
    long long __divdi3(long long a, long long b);
    unsigned long long __udivdi3(unsigned long long a, unsigned long long b);
    unsigned long long __umoddi3(unsigned long long a, unsigned long long b);
    long long __moddi3(long long a, long long b);
    unsigned long long __fixunsdfdi(double a);
    unsigned long long __fixunssfdi(float a);

    // glibc internal string to number conversion functions
    double __strtod_internal(const char *nptr, char **endptr, int group);
    long int __strtol_internal(const char *nptr, char **endptr, int base, int group);
    unsigned long int __strtoul_internal(const char *nptr, char **endptr, int base, int group);
}

namespace LibcBridge
{
    char bridgeLibcSingleThreaded = 1; // 1 = true (skip pthread locks), 0 = false

    FILE *native_stdin = stdin;
    FILE *native_stdout = stdout;
    FILE *native_stderr = stderr;

    static double bridgeLog2(double value)
    {
        return std::log2(value);
    }

    void initBridges()
    {
        log_info("Initializing Libc Bridges...");

        // printf family
        MAP("printf", bridgePrintf);
        MAP("puts", bridgePuts);
        MAP("putchar", bridgePutchar);
        MAP("putc", bridgePutc);
        MAP("fprintf", bridgeFprintf);
        MAP("sprintf", bridgeSprintf);
        MAP("snprintf", bridgeSnprintf);
        MAP("vprintf", bridgeVprintf);
        MAP("vfprintf", bridgeVfprintf);
        MAP("vsprintf", bridgeVsprintf);
        MAP("vsnprintf", bridgeVsnprintf);

        /* _FORTIFY_SOURCE forms of the same calls; see the implementations. */
        MAP("__printf_chk", bridgePrintfChk);
        MAP("__fprintf_chk", bridgeFprintfChk);
        MAP("__sprintf_chk", bridgeSprintfChk);
        MAP("__snprintf_chk", bridgeSnprintfChk);
        MAP("__vprintf_chk", bridgeVprintfChk);
        MAP("__vfprintf_chk", bridgeVfprintfChk);
        MAP("__vsprintf_chk", bridgeVsprintfChk);
        MAP("__vsnprintf_chk", bridgeVsnprintfChk);
        MAP("__memcpy_chk", bridgeMemcpyChk);
        MAP("__memmove_chk", bridgeMemmoveChk);
        MAP("__memset_chk", bridgeMemsetChk);
        MAP("__strcpy_chk", bridgeStrcpyChk);
        MAP("__strncpy_chk", bridgeStrncpyChk);
        MAP("__strcat_chk", bridgeStrcatChk);
        MAP("__strncat_chk", bridgeStrncatChk);

        MAP("fscanf", bridgeFscanf);
        MAP("sscanf", bridgeSscanf);
        MAP("index", bridgeIndex);
        MAP("strtok", bridgeStrtok);
        MAP("strtok_r", bridgeStrtokR);
        MAP("__strtok_r", bridgeStrtokR);
        MAP("strerror_r", bridgeStrerrorR);

        MAP("stdin", &native_stdin);
        MAP("stdout", &native_stdout);
        MAP("stderr", &native_stderr);

        // some static libraries refer to glibc's internal IO structs
        MAP("_IO_2_1_stdin_", stdin);
        MAP("_IO_2_1_stdout_", stdout);
        MAP("_IO_2_1_stderr_", stderr);

        // time functions
        MAP("time", bridgeTime);
        MAP("gettimeofday", bridgeGettimeofday);
        MAP("settimeofday", bridgeSettimeofday);
        MAP("localtime", bridgeLocaltime);
        MAP("utime", bridgeUtime);
        MAP("usleep", bridgeUsleep);
        MAP("sleep", bridgeSleep);
        MAP("nanosleep", bridgeNanosleep);
        MAP("localtime_r", sharedLocaltime_R);
        MAP("clock_gettime", bridgeClockGettime);
        MAP("mktime", bridgeMktime);
        MAP("gmtime_r", bridgeGmtime_R);
        MAP("strftime", bridgeStrftime);
        MAP("ftime", bridgeFtime);

        // abort/exit
        MAP("abort", bridgeAbort);
        MAP("exit", bridgeExit);
        MAP("_exit", bridgeExit);

        // C++ ABI functions
        MAP("__cxa_atexit", bridgeCxaAtexit);
        MAP("__cxa_thread_atexit_impl", bridgeCxaThreadAtexitImpl);
        MAP("__register_frame_info_bases", bridgeRegisterFrameInfoBases);
        MAP("__register_frame_info", bridgeRegisterFrameInfo);

        MAP("__libc_single_threaded", &bridgeLibcSingleThreaded);

        // locale functions
        MAP("setlocale", bridgeSetlocale);
        MAP("newlocale", bridgeNewlocale);
        MAP("__newlocale", bridgeNewlocale);
        MAP("freelocale", bridgeFreelocale);
        MAP("__freelocale", bridgeFreelocale);
        MAP("uselocale", bridgeUselocale);
        MAP("__uselocale", bridgeUselocale);
        MAP("localeconv", bridgeLocaleconv);
        MAP("__localeconv", bridgeLocaleconv);

        // wide character functions
        MAP("__wctype_l", bridgeWctypeL);
        MAP("__iswctype_l", bridgeIswctypeL);
        MAP("mbsrtowcs", bridgeMbsrtowcs);

        // gettext
        MAP("gettext", bridgeGettext);
        MAP("dgettext", bridgeDgettext);

        // Stubs
        MAP("sysctl", bridgeSysctl);
        MAP("iopl", bridgeStubSuccess);

        // From other loader
        MAP("__divdi3", __divdi3);
        MAP("__udivdi3", __udivdi3);
        MAP("__umoddi3", __umoddi3);
        MAP("__moddi3", __moddi3);
        MAP("__fixunsdfdi", __fixunsdfdi);
        MAP("__fixunssfdi", __fixunssfdi);

        // system
        MAP("system", bridgeSystem);

        MAP("getpid", _getpid);
        MAP("getuid", bridgeGetuid);
        MAP("waitpid", bridgeWaitpid);
        MAP("fork", bridgeFork);
        MAP("vfork", bridgeVfork);
        MAP("daemon", bridgeDaemon);
        MAP("execlp", bridgeExeclp);
        MAP("kill", bridgeKill);
        MAP("wait", bridgeWait);
        MAP("getenv", sharedGetenv);
        MAP("setenv", sharedSetenv);
        MAP("unsetenv", sharedUnsetenv);

        MAP("syslog", bridgeSyslog);
        MAP("openlog", bridgeOpenlog);
        MAP("closelog", bridgeCloselog);

        MAP("rand", bridgeRand);
        MAP("random", bridgeRandom);
        MAP("rand_r", bridgeRand_r);
        MAP("srand", bridgeSrand);
        MAP("srandom", bridgeSrandom);
        MAP("signal", bridgeSignal);
        MAP("raise", bridgeRaise);
        MAP("sigfillset", bridgeSigfillset);
        MAP("sigemptyset", bridgeSigemptyset);
        MAP("sigaddset", bridgeSigaddset);
        MAP("sigprocmask", bridgeSigprocmask);
        MAP("pthread_sigmask", bridgeSigprocmask);
        MAP("sigaction", bridgeSigaction);
        MAP("setitimer", bridgeSetitimer);
        MAP("sigwait", bridgeSigwait);
        MAP("alarm", bridgeStubSuccess);
        MAP("qsort", bridgeQsort);
        MAP("poll", bridgePoll);
        MAP("bsearch", bridgeBsearch);
        MAP("realpath", bridgeRealpath);
        MAP("popen", bridgePopen);
        MAP("pclose", bridgePclose);
        MAP("perror", bridgePerror);

        MAP("isinf", bridgeIsinf);
        MAP("isnan", bridgeIsnan);
        MAP("log2", bridgeLog2);
        MAP("wcscoll_l", bridgeWcscoll_l);
        MAP("wcsxfrm_l", bridgeWcsxfrm_l);
        MAP("towlower_l", bridgeTowlower_l);
        MAP("towupper_l", bridgeTowupper_l);
        MAP("nl_langinfo", bridgeNlLanginfo);

        // Math
        MAP("atoi", atoi);
        MAP("atof", atof);
        MAP("isalnum", bridgeIsalnum);
        MAP("isalpha", bridgeIsalpha);
        MAP("iscntrl", bridgeIscntrl);
        MAP("isdigit", bridgeIsdigit);
        MAP("isgraph", bridgeIsgraph);
        MAP("islower", bridgeIslower);
        MAP("isprint", bridgeIsprint);
        MAP("ispunct", bridgeIspunct);
        MAP("isspace", bridgeIsspace);
        MAP("isupper", bridgeIsupper);
        MAP("isxdigit", bridgeIsxdigit);
        MAP("tolower", bridgeTolower);
        MAP("toupper", bridgeToupper);

        // Memory
        MAP("getpagesize", bridgeGetpagesize);
        MAP("mmap", bridgeMmap);
        MAP("munmap", bridgeMunmap);
        MAP("bzero", bridgeBzero);
        MAP("sbrk", bridgeSbrk);
        MAP("pipe", bridgePipe);
        MAP("_setjmp", bridgeSetjmp);
        MAP("__sigsetjmp", bridgeSigsetjmp);
        MAP("longjmp", bridgeLongjmp);
        MAP("_longjmp", bridgeLongjmp);
        /* The _FORTIFY_SOURCE variant differs only in the checking glibc does
         * on its own jump buffer, which we do not model. */
        MAP("__longjmp_chk", bridgeLongjmp);

        MAP("__stack_chk_fail", bridgeStackChkFail);
        MAP("_IO_putc", bridgePutcUnlocked);
        MAP("nice", bridgeNice);
        MAP("getrlimit", bridgeGetrlimit);
        MAP("getrusage", bridgeGetrusage);
        MAP("sched_getscheduler", bridgeSchedGetscheduler);
        MAP("ftruncate64", bridgeFtruncate64);
        MAP("posix_fallocate64", bridgePosixFallocate64);
        MAP("utimes", bridgeUtimes);
        MAP("sincosf", bridgeSincosf);
        MAP("strnlen", bridgeStrnlen);
        MAP("strtoll", bridgeStrtoll);
        MAP("strtoul", bridgeStrtoul);
        MAP("strtoull", bridgeStrtoull);

        // Library handles
        MAP("dlopen", sharedDlopen);
        MAP("dlsym", sharedDlsym);
        MAP("dlclose", sharedDlclose);
        MAP("dlerror", sharedDlerror);

        MAP("kswap_collect", sharedKswap_collect);

        // Wide char string functions
        MAP("wmemcmp", bridgeWmemcmp);
        MAP("wmemcpy", bridgeWmemcpy);
        MAP("wmemset", bridgeWmemset);
        MAP("wmemchr", bridgeWmemchr);
        MAP("wcslen", bridgeWcslen);
        MAP("wcscpy", bridgeWcscpy);
        MAP("wcsncpy", bridgeWcsncpy);
        MAP("wcscmp", bridgeWcscmp);
        MAP("wcscoll", bridgeWcscoll);
        MAP("wcsncmp", bridgeWcsncmp);
        MAP("wcschr", bridgeWcschr);
        MAP("wcsrchr", bridgeWcsrchr);
        MAP("wcsstr", bridgeWcsstr);
        MAP("wcstod", bridgeWcstod);
        MAP("wcstol", bridgeWcstol);
        MAP("wctob", bridgeWctob);
        MAP("wctype", bridgeWctype);
        MAP("wcsrtombs", bridgeWcsrtombs);
        MAP("wcrtomb", bridgeWcrtomb);
        MAP("wcsftime", bridgeWcsftime);
        MAP("wcsxfrm", bridgeWcsxfrm);
        MAP("wcstombs", bridgeWcstombs);
        MAP("mbrtowc", bridgeMbrtowc);
        MAP("mbtowc", bridgeMbtowc);
        MAP("mbstowcs", bridgeMbstowcs);
        MAP("btowc", bridgeBtowc);
        MAP("putwc", bridgePutwc);
        MAP("getwc", bridgeGetwc);
        MAP("ungetwc", bridgeUngetwc);
        MAP("fgetwc", bridgeGetwc);
        MAP("fputwc", bridgePutwc);
    }

    void bridgeAbort()
    {
        log_fatal("abort() called by ELF!");
        ::abort();
    }

    void bridgeBzero(void *destination, size_t length)
    {
        memset(destination, 0, length);
    }

    void *bridgeSbrk(intptr_t increment)
    {
        static const size_t heapCandidates[] = {
            1024ull * 1024 * 1024,
            768ull * 1024 * 1024,
            512ull * 1024 * 1024,
        };
        static size_t heapCapacity = 0;
        static SRWLOCK heapLock = SRWLOCK_INIT;
        static uint8_t *heapBase = nullptr;
        static intptr_t heapOffset = 0;
        static size_t committedSize = 0;

        AcquireSRWLockExclusive(&heapLock);

        if (!heapBase)
        {
            for (size_t candidate : heapCandidates)
            {
                heapBase = static_cast<uint8_t *>(VirtualAlloc(nullptr, candidate, MEM_RESERVE, PAGE_READWRITE));
                if (heapBase)
                {
                    heapCapacity = candidate;
                    log_info("sbrk: reserved a %zu MB program break", candidate / (1024 * 1024));
                    break;
                }
            }
        }

        if (!heapBase || increment < -heapOffset ||
            increment > static_cast<intptr_t>(heapCapacity) - heapOffset)
        {
            log_error("sbrk: unable to move break by %ld bytes (used=%ld, capacity=%zu, base=%p)",
                      static_cast<long>(increment), static_cast<long>(heapOffset),
                      heapCapacity, heapBase);
            ReleaseSRWLockExclusive(&heapLock);
            return reinterpret_cast<void *>(-1);
        }

        const intptr_t newOffset = heapOffset + increment;
        if (newOffset > static_cast<intptr_t>(committedSize))
        {
            SYSTEM_INFO systemInfo;
            GetSystemInfo(&systemInfo);
            const size_t pageSize = systemInfo.dwPageSize;
            const size_t requiredSize =
                (static_cast<size_t>(newOffset) + pageSize - 1) & ~(pageSize - 1);
            const size_t commitSize = requiredSize - committedSize;

            if (commitSize != 0 &&
                !VirtualAlloc(heapBase + committedSize, commitSize, MEM_COMMIT, PAGE_READWRITE))
            {
                log_error("sbrk: VirtualAlloc commit failed (used=%ld, required=%zu, error=%lu)",
                          static_cast<long>(heapOffset), requiredSize, GetLastError());
                ReleaseSRWLockExclusive(&heapLock);
                return reinterpret_cast<void *>(-1);
            }

            committedSize = requiredSize;
        }

        uint8_t *previousBreak = heapBase + heapOffset;
        heapOffset = newOffset;
        ReleaseSRWLockExclusive(&heapLock);
        return previousBreak;
    }

    int bridgePipe(int descriptors[2])
    {
        return NetworkBridge::bridgeSocketPair(descriptors);
    }

    __attribute__((naked, returns_twice)) int bridgeSetjmp(void *)
    {
        __asm__ volatile(
            "mov 4(%esp), %eax\n\t"
            "mov %ebx, 0(%eax)\n\t"
            "mov %esi, 4(%eax)\n\t"
            "mov %edi, 8(%eax)\n\t"
            "mov %ebp, 12(%eax)\n\t"
            "lea 4(%esp), %edx\n\t"
            "mov %edx, 16(%eax)\n\t"
            "mov (%esp), %edx\n\t"
            "mov %edx, 20(%eax)\n\t"
            "xor %eax, %eax\n\t"
            "ret\n\t");
    }

    __attribute__((naked, returns_twice)) int bridgeSigsetjmp(void *, int)
    {
        __asm__ volatile(
            "mov 4(%esp), %eax\n\t"
            "mov %ebx, 0(%eax)\n\t"
            "mov %esi, 4(%eax)\n\t"
            "mov %edi, 8(%eax)\n\t"
            "mov %ebp, 12(%eax)\n\t"
            "lea 4(%esp), %edx\n\t"
            "mov %edx, 16(%eax)\n\t"
            "mov (%esp), %edx\n\t"
            "mov %edx, 20(%eax)\n\t"
            "xor %eax, %eax\n\t"
            "ret\n\t");
    }

    __attribute__((naked, noreturn)) void bridgeLongjmp(void *, int)
    {
        __asm__ volatile(
            "mov 4(%esp), %eax\n\t"
            "mov 8(%esp), %edx\n\t"
            "test %edx, %edx\n\t"
            "jnz 1f\n\t"
            "inc %edx\n\t"
            "1:\n\t"
            "mov 20(%eax), %ecx\n\t"
            "mov 0(%eax), %ebx\n\t"
            "mov 4(%eax), %esi\n\t"
            "mov 8(%eax), %edi\n\t"
            "mov 12(%eax), %ebp\n\t"
            "mov 16(%eax), %esp\n\t"
            "mov %edx, %eax\n\t"
            "jmp *%ecx\n\t");
    }

    int bridgeIsalnum(int character) { return isalnum(static_cast<unsigned char>(character)); }
    int bridgeIsalpha(int character) { return isalpha(static_cast<unsigned char>(character)); }
    int bridgeIscntrl(int character) { return iscntrl(static_cast<unsigned char>(character)); }
    int bridgeIsdigit(int character) { return isdigit(static_cast<unsigned char>(character)); }
    int bridgeIsgraph(int character) { return isgraph(static_cast<unsigned char>(character)); }
    int bridgeIslower(int character) { return islower(static_cast<unsigned char>(character)); }
    int bridgeIsprint(int character) { return isprint(static_cast<unsigned char>(character)); }
    int bridgeIspunct(int character) { return ispunct(static_cast<unsigned char>(character)); }
    int bridgeIsspace(int character) { return isspace(static_cast<unsigned char>(character)); }
    int bridgeIsupper(int character) { return isupper(static_cast<unsigned char>(character)); }
    int bridgeIsxdigit(int character) { return isxdigit(static_cast<unsigned char>(character)); }
    int bridgeTolower(int character) { return tolower(static_cast<unsigned char>(character)); }
    int bridgeToupper(int character) { return toupper(static_cast<unsigned char>(character)); }

    void bridgeExit(int status)
    {
        log_fatal("exit(%d) called by ELF!", status);
        ::exit(status);
    }

    int bridgeCxaAtexit(void (*func)(void *), void *arg, void *dso_handle)
    {
        log_debug("__cxa_atexit called");
        return 0;
    }

    int bridgeCxaThreadAtexitImpl(void (*func)(void *), void *arg, void *dso_handle)
    {
        log_debug("__cxa_thread_atexit_impl called");
        return 0;
    }

    void bridgeRegisterFrameInfoBases(void *begin, void *ob, void *tbase, void *dbase)
    {
    }

    void bridgeRegisterFrameInfo(void *begin, void *ob)
    {
    }

    void *bridgeDeregisterFrameInfo(void *begin)
    {
        return nullptr;
    }

    void *bridgeCxaAllocateException(size_t thrown_size)
    {
        return MemoryManager::customMalloc(thrown_size);
    }

    void bridgeCxaFreeException(void *thrown_exception)
    {
        MemoryManager::customFree(thrown_exception);
    }

    char *bridgeSetlocale(int category, const char *locale)
    {
        constexpr int linuxLcAll = 6;
        static char cLocale[] = "C";

        if (category < 0 || category > linuxLcAll)
        {
            log_debug("setlocale: unknown category %d", category);
            return nullptr;
        }

        if (locale && *locale && strcmp(locale, "C") != 0 && strcmp(locale, "POSIX") != 0)
            log_debug("setlocale: \"%s\" is not available; staying in the C locale", locale);

        return cLocale;
    }

    struct FakeLocaleStruct
    {
        void *localeData[13];         // __locale_data* per LC_* category (unused, keep NULL)
        const unsigned short *ctypeB; // == __ctype_b
        const int32_t *ctypeTolower;  // == __ctype_tolower
        const int32_t *ctypeToUpper;  // == __ctype_toupper
        const char *names[13];        // locale category name strings (unused)
    };

    static FakeLocaleStruct g_FakeClassicLocale = {};
    static bool g_FakeLocaleInitialized = false;

    static FakeLocaleStruct *GetFakeLocale()
    {
        if (!g_FakeLocaleInitialized)
        {
            memset(&g_FakeClassicLocale, 0, sizeof(g_FakeClassicLocale));
            g_FakeClassicLocale.ctypeB = GccBridge::GetCtypeBPtr();
            g_FakeClassicLocale.ctypeTolower = GccBridge::GetCtypeTolowerPtr();
            g_FakeClassicLocale.ctypeToUpper = GccBridge::GetCtypeToUpperPtr();
            g_FakeLocaleInitialized = true;
        }
        return &g_FakeClassicLocale;
    }

    void *bridgeNewlocale(int category_mask, const char *locale, void *base)
    {
        log_debug("bridgeNewlocale: category_mask=%d, locale=%s", category_mask, locale ? locale : "(null)");
        return GetFakeLocale();
    }

    void bridgeFreelocale(void *loc)
    {
    }

    void *bridgeUselocale(void *loc)
    {
        return GetFakeLocale();
    }

    struct lconv *bridgeLocaleconv(void)
    {
        static struct lconv l;
        static bool initialized = false;
        if (!initialized)
        {
            l.decimal_point = (char *)".";
            l.thousands_sep = (char *)",";
            l.grouping = (char *)"";
            l.int_curr_symbol = (char *)"";
            l.currency_symbol = (char *)"";
            l.mon_decimal_point = (char *)".";
            l.mon_thousands_sep = (char *)",";
            l.mon_grouping = (char *)"";
            l.positive_sign = (char *)"";
            l.negative_sign = (char *)"";
            l.int_frac_digits = 127;
            l.frac_digits = 127;
            l.p_cs_precedes = 127;
            l.p_sep_by_space = 127;
            l.n_cs_precedes = 127;
            l.n_sep_by_space = 127;
            l.p_sign_posn = 127;
            l.n_sign_posn = 127;
            initialized = true;
        }
        return &l;
    }

    uint32_t bridgeWctypeL(const char *property, void *locale)
    {
        return (uint32_t)wctype(property);
    }

    int bridgeIswctypeL(int wc, uint32_t desc, void *locale)
    {
        return iswctype(wc, (wctype_t)desc);
    }

    size_t bridgeMbsrtowcs(uint32_t *dst, const char **src, size_t len, void *ps)
    {
        if (!src || !*src)
            return (size_t)-1;

        const char *s = *src;
        size_t count = 0;
        mbstate_t localState;
        memset(&localState, 0, sizeof(localState));

        if (dst == nullptr)
        {
            while (*s)
            {
                wchar_t wc;
                size_t nb = mbrtowc(&wc, s, MB_CUR_MAX, &localState);
                if (nb == (size_t)-1 || nb == (size_t)-2)
                    return (size_t)-1;
                if (nb == 0)
                    break;
                s += nb;
                count++;
            }
            return count;
        }

        while (count < len)
        {
            if (*s == '\0')
            {
                dst[count] = 0;
                *src = nullptr;
                return count;
            }

            wchar_t wc;
            size_t nb = mbrtowc(&wc, s, MB_CUR_MAX, &localState);
            if (nb == (size_t)-1 || nb == (size_t)-2)
            {
                errno = EILSEQ;
                return (size_t)-1;
            }

            dst[count] = (uint32_t)wc;
            s += (nb == 0) ? 1 : nb;
            count++;
        }

        *src = s;
        return count;
    }

    int bridgeIsinf(double x)
    {
        return __builtin_isinf(x);
    }

    int bridgeIsnan(double x)
    {
        return __builtin_isnan(x);
    }

    int bridgeWcscoll_l(const uint32_t *s1, const uint32_t *s2, void *locale)
    {
        size_t len1 = 0, len2 = 0;
        while (s1[len1])
            len1++;
        while (s2[len2])
            len2++;

        wchar_t *w1 = (wchar_t *)alloca((len1 + 1) * sizeof(wchar_t));
        wchar_t *w2 = (wchar_t *)alloca((len2 + 1) * sizeof(wchar_t));

        for (size_t i = 0; i <= len1; i++)
            w1[i] = (wchar_t)s1[i];
        for (size_t i = 0; i <= len2; i++)
            w2[i] = (wchar_t)s2[i];

        return wcscoll(w1, w2);
    }

    size_t bridgeWcsxfrm_l(uint32_t *dst, const uint32_t *src, size_t n, void *locale)
    {
        size_t srcLen = 0;
        while (src[srcLen])
            srcLen++;

        wchar_t *wSrc = (wchar_t *)alloca((srcLen + 1) * sizeof(wchar_t));
        for (size_t i = 0; i <= srcLen; i++)
            wSrc[i] = (wchar_t)src[i];

        if (dst == nullptr || n == 0)
            return wcsxfrm(nullptr, wSrc, 0);

        wchar_t *wDst = (wchar_t *)alloca((n + 1) * sizeof(wchar_t));
        size_t result = wcsxfrm(wDst, wSrc, n);

        size_t copyLen = (result < n) ? result : n - 1;
        for (size_t i = 0; i < copyLen; i++)
            dst[i] = (uint32_t)wDst[i];
        if (n > 0)
            dst[copyLen] = 0;

        return result;
    }

    int bridgeTowlower_l(int wc, void *locale)
    {
        return (int)towlower((wint_t)wc);
    }

    int bridgeTowupper_l(int wc, void *locale)
    {
        return (int)towupper((wint_t)wc);
    }

#define LINUX_CODESET 14

    char *bridgeNlLanginfo(int item)
    {
        static char codeset[] = "ANSI_X3.4-1968";
        static char empty[] = "";

        if (item == LINUX_CODESET)
            return codeset;
        return empty;
    }

    char *bridgeGettext(const char *msgid)
    {
        return const_cast<char *>(msgid);
    }

    char *bridgeDgettext(const char *domainname, const char *msgid)
    {
        return const_cast<char *>(msgid);
    }

    /*
     * The guest's own stdout - model loads, texture swaps, a line per frame -
     * used to go straight to the console and so ignored the log level entirely.
     * It is the bulk of what fills the terminal: 20,493 of one WMMT3 run's
     * 22,539 lines. LOG_GAME exists for it, so LL_LOG_LEVEL=game brings it back.
     * The text is still formatted when suppressed, because callers use the
     * returned length.
     */
    int guestConsoleVprintf(void *stream, const char *format, va_list args)
    {
        char text[2048];
        const int length = ::vsnprintf(text, sizeof(text), format ? format : "", args);
        if (length <= 0)
            return length;
        if (logIsEnabled(LOG_GAME))
        {
            FILE *target = stream ? (FILE *)stream : stdout;
            const size_t written = static_cast<size_t>(length) < sizeof(text)
                                       ? static_cast<size_t>(length)
                                       : sizeof(text) - 1;
            ::fwrite(text, 1, written, target);
        }
        return length;
    }

    /* Only the console is quietened; a real file still gets its bytes. */
    bool isGuestConsole(void *stream)
    {
        return stream == nullptr || stream == stdout || stream == stderr;
    }

    /*
     * The host CRT's qsort and bsearch call the comparison function the guest
     * gave them, and they call it the Windows way: nothing guarantees esp is
     * 16-byte aligned. Guest code is Linux SysV i386, where that alignment is
     * promised, so a comparator that spills through movaps takes a general
     * protection fault - seen on WMMT5, whose comparator does exactly that.
     *
     * Route the callback through a thunk of our own, which realigns and then
     * calls the guest with the alignment it expects. The saved pointer is
     * per-thread and restored afterwards so a comparator may sort again.
     */
    static __thread int (*t_guestCompare)(const void *, const void *) = nullptr;

    __attribute__((force_align_arg_pointer))
    static int alignedGuestCompare(const void *a, const void *b)
    {
        return t_guestCompare ? t_guestCompare(a, b) : 0;
    }

    void bridgeQsort(void *base, size_t count, size_t size,
                     int (*compare)(const void *, const void *))
    {
        if (!compare)
            return;
        int (*saved)(const void *, const void *) = t_guestCompare;
        t_guestCompare = compare;
        ::qsort(base, count, size, alignedGuestCompare);
        t_guestCompare = saved;
    }

    void *bridgeBsearch(const void *key, const void *base, size_t count, size_t size,
                        int (*compare)(const void *, const void *))
    {
        if (!compare)
            return nullptr;
        int (*saved)(const void *, const void *) = t_guestCompare;
        t_guestCompare = compare;
        void *result = ::bsearch(key, base, count, size, alignedGuestCompare);
        t_guestCompare = saved;
        return result;
    }

    int bridgePrintf(const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        const int ret = guestConsoleVprintf(nullptr, format, args);
        va_end(args);
        return ret;
    }

    int bridgePuts(const char *str)
    {
        if (!logIsEnabled(LOG_GAME))
            return 0;
        return ::puts(str);
    }

    int bridgeFprintf(void *stream, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        const int ret = isGuestConsole(stream)
                            ? guestConsoleVprintf(stream, format, args)
                            : ::vfprintf((FILE *)stream, format, args);
        va_end(args);
        return ret;
    }

    int bridgeSprintf(char *buffer, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        int ret = ::vsprintf(buffer, format, args);
        va_end(args);
        return ret;
    }

    int bridgeSnprintf(char *buffer, size_t count, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        if (format == NULL)
            return 0;
        int ret = _vsnprintf(buffer, count, format, args);
        va_end(args);
        return ret;
    }

    int bridgeVprintf(const char *format, va_list args)
    {
        return guestConsoleVprintf(nullptr, format, args);
    }

    int bridgeVfprintf(void *stream, const char *format, va_list args)
    {
        if (isGuestConsole(stream))
            return guestConsoleVprintf(stream, format, args);
        return ::vfprintf((FILE *)stream, format, args);
    }

    int bridgeVsprintf(char *buffer, const char *format, va_list args)
    {
        return ::vsprintf(buffer, format, args);
    }

    int bridgeVsnprintf(char *buffer, size_t count, const char *format, va_list args)
    {
        return ::vsnprintf(buffer, count, format, args);
    }

    /* glibc's _FORTIFY_SOURCE wrappers, where `flag` is the fortification level
     * and `slen` the destination size.  The bounds are honoured, or checked
     * outright where the plain function takes no size. */
    int bridgePrintfChk(int, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        const int result = guestConsoleVprintf(nullptr, format, args);
        va_end(args);
        return result;
    }

    int bridgeFprintfChk(void *stream, int, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        const int result = ::vfprintf(stream ? (FILE *)stream : stdout, format, args);
        va_end(args);
        return result;
    }

    int bridgeSprintfChk(char *buffer, int, size_t destinationSize, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        /* Unlike sprintf() the destination size is known here, so use it. */
        const int result = destinationSize == (size_t)-1
                               ? ::vsprintf(buffer, format, args)
                               : ::vsnprintf(buffer, destinationSize, format, args);
        va_end(args);
        return result;
    }

    int bridgeSnprintfChk(char *buffer, size_t count, int, size_t destinationSize,
                          const char *format, ...)
    {
        if (destinationSize < count)
        {
            log_fatal("__snprintf_chk: %zu bytes requested into a %zu byte buffer", count,
                      destinationSize);
            count = destinationSize;
        }
        va_list args;
        va_start(args, format);
        const int result = ::vsnprintf(buffer, count, format, args);
        va_end(args);
        return result;
    }

    int bridgeVprintfChk(int, const char *format, va_list args)
    {
        return guestConsoleVprintf(nullptr, format, args);
    }

    int bridgeVfprintfChk(void *stream, int, const char *format, va_list args)
    {
        if (isGuestConsole(stream))
            return guestConsoleVprintf(stream, format, args);
        return ::vfprintf((FILE *)stream, format, args);
    }

    int bridgeVsprintfChk(char *buffer, int, size_t destinationSize, const char *format,
                          va_list args)
    {
        return destinationSize == (size_t)-1 ? ::vsprintf(buffer, format, args)
                                             : ::vsnprintf(buffer, destinationSize, format, args);
    }

    int bridgeVsnprintfChk(char *buffer, size_t count, int, size_t destinationSize,
                           const char *format, va_list args)
    {
        if (destinationSize < count)
            count = destinationSize;
        return ::vsnprintf(buffer, count, format, args);
    }

    void *bridgeMemcpyChk(void *destination, const void *source, size_t count,
                          size_t destinationSize)
    {
        if (count > destinationSize)
            log_fatal("__memcpy_chk: %zu bytes into a %zu byte buffer", count, destinationSize);
        return ::memcpy(destination, source, count);
    }

    void *bridgeMemmoveChk(void *destination, const void *source, size_t count,
                           size_t destinationSize)
    {
        if (count > destinationSize)
            log_fatal("__memmove_chk: %zu bytes into a %zu byte buffer", count, destinationSize);
        return ::memmove(destination, source, count);
    }

    void *bridgeMemsetChk(void *destination, int value, size_t count, size_t destinationSize)
    {
        if (count > destinationSize)
            log_fatal("__memset_chk: %zu bytes into a %zu byte buffer", count, destinationSize);
        return ::memset(destination, value, count);
    }

    char *bridgeStrcpyChk(char *destination, const char *source, size_t destinationSize)
    {
        if (::strlen(source) + 1 > destinationSize)
            log_fatal("__strcpy_chk: %zu bytes into a %zu byte buffer", ::strlen(source) + 1,
                      destinationSize);
        return ::strcpy(destination, source);
    }

    char *bridgeStrncpyChk(char *destination, const char *source, size_t count,
                           size_t destinationSize)
    {
        if (count > destinationSize)
            log_fatal("__strncpy_chk: %zu bytes into a %zu byte buffer", count, destinationSize);
        return ::strncpy(destination, source, count);
    }

    char *bridgeStrcatChk(char *destination, const char *source, size_t destinationSize)
    {
        if (::strlen(destination) + ::strlen(source) + 1 > destinationSize)
            log_fatal("__strcat_chk: result exceeds a %zu byte buffer", destinationSize);
        return ::strcat(destination, source);
    }

    char *bridgeStrncatChk(char *destination, const char *source, size_t count,
                           size_t destinationSize)
    {
        if (::strlen(destination) + count + 1 > destinationSize)
            log_fatal("__strncat_chk: result exceeds a %zu byte buffer", destinationSize);
        return ::strncat(destination, source, count);
    }

    int bridgeFscanf(FILE *stream, const char *format, ...)
    {
        if (!stream)
            return 0;

        va_list args;
        va_start(args, format);
        int ret = 0;
        ret = vfscanf(stream, format, args);
        va_end(args);
        return ret;
    }

    int bridgeSscanf(const char *str, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        int ret = vsscanf(str, format, args);
        va_end(args);
        return ret;
    }

    char *bridgeIndex(const char *str, int c)
    {
        log_trace("Intercepted index");
        return strchr(str, c);
    }

    namespace
    {
        constexpr size_t MaxDelimiterLength = 256;

        bool guestReadable(const void *address, bool writable)
        {
            if (!address)
                return false;

            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
                info.State != MEM_COMMIT)
                return false;

            const DWORD protect = info.Protect & 0xff;
            if (protect == PAGE_NOACCESS || (info.Protect & PAGE_GUARD))
                return false;

            if (!writable)
                return protect == PAGE_READONLY || protect == PAGE_READWRITE ||
                       protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE ||
                       protect == PAGE_EXECUTE_READ ||
                       protect == PAGE_EXECUTE_READWRITE ||
                       protect == PAGE_EXECUTE_WRITECOPY;

            return protect == PAGE_READWRITE || protect == PAGE_WRITECOPY ||
                   protect == PAGE_EXECUTE_READWRITE ||
                   protect == PAGE_EXECUTE_WRITECOPY;
        }

        bool copyGuestString(const char *source, char (&destination)[MaxDelimiterLength],
                             size_t &length)
        {
            if (!source)
                return false;

            for (size_t i = 0; i < MaxDelimiterLength; ++i)
            {
                if (!guestReadable(source + i, false))
                    return false;
                destination[i] = source[i];
                if (destination[i] == '\0')
                {
                    length = i;
                    return true;
                }
            }
            return false;
        }

        bool isDelimiter(char value, const char *delimiters, size_t length)
        {
            for (size_t i = 0; i < length; ++i)
            {
                if (value == delimiters[i])
                    return true;
            }
            return false;
        }

        char *nextGuestToken(char *cursor, char *delimiters, size_t delimiterLength,
                             char **saveptr)
        {
            if (!cursor || !saveptr || !guestReadable(saveptr, true))
                return nullptr;

            while (guestReadable(cursor, true) && *cursor &&
                   isDelimiter(*cursor, delimiters, delimiterLength))
                ++cursor;

            if (!guestReadable(cursor, true) || *cursor == '\0')
            {
                *saveptr = nullptr;
                return nullptr;
            }

            char *token = cursor;
            while (guestReadable(cursor, true) && *cursor &&
                   !isDelimiter(*cursor, delimiters, delimiterLength))
                ++cursor;

            if (!guestReadable(cursor, true))
            {
                *saveptr = nullptr;
                return nullptr;
            }

            if (*cursor == '\0')
                *saveptr = nullptr;
            else
            {
                *cursor = '\0';
                *saveptr = cursor + 1;
            }
            return token;
        }
    }

    char *bridgeStrtok(char *str, const char *delim)
    {
        static thread_local char *next = nullptr;
        char delimiters[MaxDelimiterLength]{};
        size_t delimiterLength = 0;
        if (!copyGuestString(delim, delimiters, delimiterLength))
            return nullptr;

        if (str)
            next = str;
        if (!next)
            return nullptr;

        return nextGuestToken(next, delimiters, delimiterLength, &next);
    }

    char *bridgeStrtokR(char *str, const char *delim, char **saveptr)
    {
        char delimiters[MaxDelimiterLength]{};
        size_t delimiterLength = 0;
        if (!saveptr || !copyGuestString(delim, delimiters, delimiterLength))
            return nullptr;

        char *next = str ? str : *saveptr;
        return nextGuestToken(next, delimiters, delimiterLength, saveptr);
    }

    /* An N2 cabinet runs TZ=UTC while its clock holds local time, and the game
     * converts with gmtime_r().  So the clock sources report local time and
     * localtime() behaves as UTC, keeping gmtime()/timegm() round-tripping. */
    long hostUtcOffsetSeconds()
    {
        static long cachedOffset = 0;
        static DWORD nextRefresh = 0;

        const DWORD now = GetTickCount();
        if (nextRefresh == 0 || static_cast<LONG>(now - nextRefresh) >= 0)
        {
            const time_t utcNow = time(nullptr);
            struct tm utcParts = {};
            if (gmtime_s(&utcParts, &utcNow) == 0)
            {
                // Re-reading the UTC calendar time as a local one yields an
                // instant shifted by the offset; tm_isdst = -1 lets the CRT
                // resolve daylight saving for the current date.
                utcParts.tm_isdst = -1;
                const time_t utcReadAsLocal = mktime(&utcParts);
                if (utcReadAsLocal != static_cast<time_t>(-1))
                    cachedOffset = static_cast<long>(utcNow - utcReadAsLocal);
            }
            nextRefresh = now + 60000;
        }
        return cachedOffset;
    }

    static long cabinetClockOffset()
    {
        return getConfig()->platform == ARCADE_PLATFORM_NAMCO_N2 ? hostUtcOffsetSeconds() : 0;
    }

    // Wall clock as microseconds since the Unix epoch, already carrying the
    // cabinet offset so every clock source the game can read agrees.
    static unsigned long long cabinetClockMicroseconds()
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        unsigned long long ticks = (unsigned long long)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
        ticks -= 116444736000000000ULL; // 1601-01-01 to 1970-01-01, in 100ns units
        return ticks / 10 + (unsigned long long)cabinetClockOffset() * 1000000ULL;
    }

    int32_t bridgeTime(int32_t *tloc)
    {
        log_trace("Intercepted time");
        time_t t = time(NULL) + cabinetClockOffset();
        if (tloc)
        {
            *tloc = (int32_t)t;
        }
        return (int32_t)t;
    }

    int bridgeUtime(const char *filename, const struct linux_utimbuf *times)
    {
        log_trace("Intercepted utime");
        char winPath[MAX_PATH];
        ConvertPath(winPath, filename, MAX_PATH);
        return _utime(winPath, (struct _utimbuf *)times);
    }

    void bridgeStackChkFail(void)
    {
        /* Reaching this means the guest detected a smashed stack canary.  The
         * process cannot be trusted from here, and glibc does not return. */
        log_fatal("__stack_chk_fail: guest stack protector tripped");
        std::abort();
    }

    int bridgePutcUnlocked(int character, FILE *stream)
    {
        if (isGuestConsole(stream) && !logIsEnabled(LOG_GAME))
            return character;
        return fputc(character, stream);
    }

    /* The guest writes its lines with fputs and their newline with putchar, so
     * leaving these ungated printed a blank line for every suppressed line. */
    int bridgePutchar(int character)
    {
        if (!logIsEnabled(LOG_GAME))
            return character;
        return ::putchar(character);
    }

    int bridgePutc(int character, FILE *stream)
    {
        if (isGuestConsole(stream) && !logIsEnabled(LOG_GAME))
            return character;
        return ::putc(character, stream);
    }

    int bridgeNice(int increment)
    {
        /* Scheduling priority is not emulated; report the unchanged niceness
         * rather than an error, which callers treat as a failure. */
        (void)increment;
        return 0;
    }

    int bridgeGetrlimit(int resource, void *limit)
    {
        /* i386 struct rlimit is two 32-bit words.  Report RLIM_INFINITY so a
         * guest sizing itself against the limit picks its own default. */
        log_trace("getrlimit(%d) -> unlimited", resource);
        if (!limit)
            return -1;
        uint32_t *values = static_cast<uint32_t *>(limit);
        values[0] = 0xFFFFFFFFu; // rlim_cur
        values[1] = 0xFFFFFFFFu; // rlim_max
        return 0;
    }

    int bridgeGetrusage(int who, void *usage)
    {
        /* Callers use this for coarse timing/diagnostics; zeros keep them from
         * dividing by an unset field. */
        (void)who;
        if (!usage)
            return -1;
        memset(usage, 0, 72); // sizeof(struct rusage) on i386
        return 0;
    }

    int bridgeSchedGetscheduler(int pid)
    {
        (void)pid;
        return 0; // SCHED_OTHER
    }

    int bridgeFtruncate64(int descriptor, int64_t length)
    {
        return _chsize_s(descriptor, length) == 0 ? 0 : -1;
    }

    int bridgePosixFallocate64(int descriptor, int64_t offset, int64_t length)
    {
        /* Windows has no fallocate; growing the file to the requested end has
         * the same observable effect for the callers that use this. */
        const int64_t end = offset + length;
        const int64_t current = _filelengthi64(descriptor);
        if (current < 0)
            return EBADF;
        if (current >= end)
            return 0;
        return _chsize_s(descriptor, end) == 0 ? 0 : EIO;
    }

    int bridgeUtimes(const char *path, const struct timeval times[2])
    {
        char winPath[MAX_PATH];
        ConvertPath(winPath, path, MAX_PATH);
        if (!times)
            return _utime(winPath, nullptr);

        struct _utimbuf buffer{};
        buffer.actime = static_cast<time_t>(times[0].tv_sec);
        buffer.modtime = static_cast<time_t>(times[1].tv_sec);
        return _utime(winPath, &buffer);
    }

    void bridgeSincosf(float angle, float *sine, float *cosine)
    {
        if (sine)
            *sine = sinf(angle);
        if (cosine)
            *cosine = cosf(angle);
    }

    size_t bridgeStrnlen(const char *text, size_t limit)
    {
        if (!text)
            return 0;
        size_t length = 0;
        while (length < limit && text[length] != '\0')
            ++length;
        return length;
    }

    long long bridgeStrtoll(const char *text, char **end, int base)
    {
        return strtoll(text, end, base);
    }

    unsigned long bridgeStrtoul(const char *text, char **end, int base)
    {
        return strtoul(text, end, base);
    }

    unsigned long long bridgeStrtoull(const char *text, char **end, int base)
    {
        return strtoull(text, end, base);
    }

    int bridgeGettimeofday(struct timeval *tv, void *tz)
    {
        log_trace("Intercepted gettimeofday");
        /* clKickback's waits poll cabinet time from the main thread rather than
         * sleeping, so pump here too or Windows calls the window hung and the
         * TEST key never arrives. */
        keepWindowResponsive();
        if (tv)
        {
            const unsigned long long t = cabinetClockMicroseconds();
            tv->tv_sec = (int32_t)(t / 1000000);
            tv->tv_usec = (int32_t)(t % 1000000);
        }
        return 0;
    }

    int bridgeSettimeofday(const struct timeval *tv, const void *tz)
    {
        log_trace("Intercepted settimeofday");
        // Convert Windows FILETIME to seconds since epoch
        ULARGE_INTEGER uli;
        uli.QuadPart = ((unsigned __int64)tv->tv_sec * 10000000 + (unsigned __int64)tv->tv_usec * 10) + 116444736000000000ULL;

        FILETIME ft;
        ft.dwLowDateTime = uli.LowPart;
        ft.dwHighDateTime = uli.HighPart;

        SYSTEMTIME st;
        if (FileTimeToSystemTime(&ft, &st))
        {
            SetSystemTime(&st);
            return 0;
        }
        return -1;
    }

    int bridgeUsleep(uint32_t microseconds)
    {
        log_trace("Intercepted usleep");

        struct timespec request;
        request.tv_sec = microseconds / 1000000u;
        request.tv_nsec = (long)(microseconds % 1000000u) * 1000L;
        return bridgeNanosleep(&request, nullptr);
    }

    int bridgeSleep(uint32_t seconds)
    {
        log_trace("Intercepted sleep");
        keepWindowResponsive();
        Sleep(seconds * 1000);
        return 0;
    }

    int bridgeNanosleep(const struct timespec *req, struct timespec *rem)
    {
        /* A thread waiting out a load usually waits here, and if it owns the
         * window this is the only chance to keep its queue moving. */
        keepWindowResponsive();

        long long duration_ns = (long long)req->tv_sec * 1000000000LL + req->tv_nsec;
        if (duration_ns <= 0)
            return 0;

        long long freq;
        QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
        long long wait_ticks = (duration_ns * freq) / 1000000000LL;

        long long start, current;
        QueryPerformanceCounter((LARGE_INTEGER *)&start);

        /* Use a high-resolution timer for sub-millisecond guest sleeps. */
        static thread_local HANDLE timer = CreateWaitableTimerExW(
            nullptr, nullptr,
            CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_MODIFY_STATE | SYNCHRONIZE);
        constexpr long long spinMarginUs = 300;

        while (true)
        {
            QueryPerformanceCounter((LARGE_INTEGER *)&current);
            long long elapsed = current - start;
            if (elapsed >= wait_ticks)
                break;

            long long remaining_us = ((wait_ticks - elapsed) * 1000000LL) / freq;

            if (remaining_us <= spinMarginUs)
            {
                YieldProcessor();
                continue;
            }

            if (timer)
            {
                LARGE_INTEGER due;
                due.QuadPart = -((remaining_us - spinMarginUs) * 10LL); /* 100 ns units. */
                if (due.QuadPart >= 0)
                    due.QuadPart = -1;
                if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
                {
                    WaitForSingleObject(timer, INFINITE);
                    continue;
                }
            }

            Sleep((DWORD)((remaining_us - spinMarginUs) / 1000));
        }

        return 0;
    }

    tm32 *bridgeLocaltime(const int32_t *timer)
    {
        static tm32 t32;
        time_t t = (time_t)*timer;
        // TZ=UTC on the cabinet, and the epoch already carries the offset.
        struct tm *tm_ptr = getConfig()->platform == ARCADE_PLATFORM_NAMCO_N2 ? gmtime(&t) : localtime(&t);

        if (tm_ptr)
        {
            t32.tm_sec = tm_ptr->tm_sec;
            t32.tm_min = tm_ptr->tm_min;
            t32.tm_hour = tm_ptr->tm_hour;
            t32.tm_mday = tm_ptr->tm_mday;
            t32.tm_mon = tm_ptr->tm_mon;
            t32.tm_year = tm_ptr->tm_year;
            t32.tm_wday = tm_ptr->tm_wday;
            t32.tm_yday = tm_ptr->tm_yday;
            t32.tm_isdst = tm_ptr->tm_isdst;
            t32.tm_gmtoff = 0;
            t32.tm_zone = 0;
            return &t32;
        }
        return nullptr;
    }

    int bridgeClockGettime(int clk_id, struct timespec *tp)
    {
        log_trace("Intercepted clock_gettime");
        keepWindowResponsive();
        if (!tp)
        {
            errno = EINVAL;
            return -1;
        }
        if (clk_id == CLOCK_REALTIME)
        {
            const unsigned long long t = cabinetClockMicroseconds();
            tp->tv_sec = (long)(t / 1000000);
            tp->tv_nsec = (long)(t % 1000000) * 1000;
            return 0;
        }
        /* Linux CLOCK_MONOTONIC is 1; RAW and BOOTTIME are 4 and 7.  ES1's
         * Boost.Asio reactor uses this epoch to arm timerfd with an absolute
         * deadline, so it must match the steady clock used by timerfd. */
        if (clk_id == 1 || clk_id == 4 || clk_id == 7)
        {
            const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            tp->tv_sec = static_cast<long>(nanoseconds / 1000000000ll);
            tp->tv_nsec = static_cast<long>(nanoseconds % 1000000000ll);
            return 0;
        }
        errno = EINVAL;
        return -1;
    }

    time_t bridgeMktime(struct tm *tm)
    {
        log_trace("Intercepted mktime");
        return mktime(tm);
    }

    struct tm32 *bridgeGmtime_R(const time_t *timep, struct tm32 *result)
    {
        log_trace("Intercepted gmtime_r");
        time_t t = *timep;
        struct tm *tm_ptr = gmtime(&t);
        if (tm_ptr)
        {
            result->tm_sec = tm_ptr->tm_sec;
            result->tm_min = tm_ptr->tm_min;
            result->tm_hour = tm_ptr->tm_hour;
            result->tm_mday = tm_ptr->tm_mday;
            result->tm_mon = tm_ptr->tm_mon;
            result->tm_year = tm_ptr->tm_year;
            result->tm_wday = tm_ptr->tm_wday;
            result->tm_yday = tm_ptr->tm_yday;
            result->tm_isdst = tm_ptr->tm_isdst;
            return result;
        }
        return nullptr;
    }

    size_t bridgeStrftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr)
    {
        log_trace("Intercepted strftime");
        return strftime(s, maxsize, format, timeptr);
    }

    void bridgeFtime(struct timeb *tp)
    {
        log_trace("Intercepted ftime");
        ftime(tp);
    }

    int bridgeSysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen)
    {
        log_trace("Intercepted sysctl");
        return -1;
    }

    int bridgeStubSuccess()
    {
        log_trace("Intercepted stub success");
        return 0;
    }

    int bridgeSystem(const char *command)
    {
        log_debug("system(\"%s\")", command);
        int platformResult = platformHandleSystemCommand(command);
        if (platformResult >= 0)
            return platformResult;
        if (platformIsDetected())
            log_warn("%s: passing unsupported system command to the host: %s",
                     platformName(), command ? command : "(null)");

        if (strcmp(command, "touch /var/tmp/mwlogo") == 0)
            command = "type nul > .\\tmp\\mwlogo";

        if (strcmp(command, "cd /tmp/segaboot > /dev/null") == 0)
            return -1;

        if (strcmp(command, "mkdir /tmp/segaboot > /dev/null") == 0)
            command = "md .\\tmp\\segaboot > nul";

        if (strcmp(command, "touch /tmp/segaboot/test") == 0)
            command = "type nul > .\\tmp\\segaboot\\test";

        if (strcmp(command, "touch /var/tmp/atr_init") == 0)
            command = "type nul > .\\tmp\\atr_init";

        if (strcmp(command, "touch /var/tmp/atr_err") == 0)
            command = "type nul > .\\tmp\\atr_err";

        if (strncmp(command, "touch /var/tmp/warning", 20) == 0)
            command = "type nul > .\\tmp\\warning";

        if (strncmp(command, "ifconfig eth0", 11) == 0)
            return 0;

        log_info("Intercepted system: %s", command ? command : "(null)");
        return system(command);
    }

    int bridgeSyslog(int priority, const char *format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        log_info("syslog: %s", buffer);
        return 0;
    }

    void bridgeOpenlog(const char *ident, int option, int facility)
    {
        log_info("openlog: %s %d %d", ident ? ident : "NULL", option, facility);
    }

    void bridgeCloselog()
    {
        log_info("closelog");
    }

    int bridgeWaitpid(int pid, int *wstatus, int options)
    {
        /* fork() is faked, so the child never existed and can never exit; a poll
         * has to answer "no state change" or the caller concludes its helper
         * process died. */
        constexpr int LINUX_WNOHANG = 1;
        if (options & LINUX_WNOHANG)
        {
            static bool reported = false;
            if (!reported)
            {
                reported = true;
                log_debug("waitpid(%d, %p, WNOHANG) -> 0; the faked child stays alive",
                          pid, wstatus);
            }
            return 0;
        }

        /* A blocking wait has nothing to wait for, so reap immediately rather
         * than deadlock the caller. */
        log_info("Intercepted blocking waitpid(%d, %p, %d)", pid, wstatus, options);
        if (wstatus)
            *wstatus = 0; // Normal termination, exit code 0
        return pid;
    }

    pid_t bridgeGetuid(void)
    {
        log_info("Intercepted getuid");
        return 0;
    }

    int bridgeFork(void)
    {
        log_debug("fork() called - returning fake parent PID 1000");
        return 1000;
    }

    int bridgeVfork(void)
    {
        log_debug("vfork() called - returning fake parent PID 1000");
        return bridgeFork();
    }

    int bridgeDaemon(int nochdir, int noclose)
    {
        log_debug("daemon(%d, %d) called - stubbed success", nochdir, noclose);
        return 0;
    }

    int bridgeExeclp(const char *file, const char *arg, ...)
    {
        log_info("execlp(\"%s\", \"%s\", ...)", file, arg);
        return 0;
    }

    namespace
    {
        std::mutex randomMutex;
        uint32_t randomState = 1;

        uint32_t nextRandom31(uint32_t &state)
        {
            // Advance all 32 state bits, then expose a Linux-sized 31-bit value.
            state = state * 1103515245u + 12345u;
            return state >> 1;
        }
    }

    int bridgeRand(void)
    {
        std::lock_guard<std::mutex> lock(randomMutex);
        return static_cast<int>(nextRandom31(randomState));
    }

    long bridgeRandom(void)
    {
        return static_cast<long>(bridgeRand());
    }

    int bridgeRand_r(unsigned int *seedp)
    {
        if (!seedp)
            return 0;
        uint32_t state = static_cast<uint32_t>(*seedp);
        const int result = static_cast<int>(nextRandom31(state));
        *seedp = state;
        return result;
    }

    void bridgeSrand(unsigned int seed)
    {
        std::lock_guard<std::mutex> lock(randomMutex);
        randomState = static_cast<uint32_t>(seed);
    }

    void bridgeSrandom(unsigned int seed)
    {
        bridgeSrand(seed);
    }

    void (*bridgeSignal(int signum, void (*handler)(int)))(int)
    {
        log_debug("signal() called");

        // Map Linux signals to Windows signals where possible
        int win_sig = -1;
        switch (signum)
        {
            case 2: // SIGINT
                win_sig = SIGINT;
                break;
            case 5:                // SIGTRAP
                win_sig = SIGABRT; // Map to SIGABRT as Windows lacks SIGTRAP
                break;
            case 15: // SIGTERM
                win_sig = SIGTERM;
                break;
            case 6: // SIGABRT
                win_sig = SIGABRT;
                break;
            case 8: // SIGFPE
                win_sig = SIGFPE;
                break;
            case 4: // SIGILL
                win_sig = SIGILL;
                break;
            case 11: // SIGSEGV
                win_sig = SIGSEGV;
                break;
            default:
                log_info("signal: unsupported signal %d", signum);
                return (void (*)(int))-1; // SIG_ERR
        }

        return signal(win_sig, handler);
    }

    int bridgeRaise(int sig)
    {
        log_info("raise(%d) called", sig);
        int win_sig = -1;
        switch (sig)
        {
            case 2: // SIGINT
                win_sig = SIGINT;
                break;
            case 5: // SIGTRAP
                // Windows doesn't have a direct equivalent in csignal, mapping to SIGABRT
                win_sig = SIGABRT;
                break;
            case 15: // SIGTERM
                win_sig = SIGTERM;
                break;
            case 6: // SIGABRT
                win_sig = SIGABRT;
                break;
            case 8: // SIGFPE
                win_sig = SIGFPE;
                break;
            case 4: // SIGILL
                win_sig = SIGILL;
                break;
            case 11: // SIGSEGV
                win_sig = SIGSEGV;
                break;
            default:
                log_info("raise: unsupported signal %d", sig);
                return -1;
        }
        return ::raise(win_sig);
    }

    int bridgeSigfillset(void *set)
    {
        log_debug("sigfillset() called");
        memset(set, 0xFF, 128);
        return 0;
    }

    int bridgeSigemptyset(void *set)
    {
        log_debug("sigemptyset() called");
        memset(set, 0, 128);
        return 0;
    }

    int bridgeSigaddset(void *set, int signum)
    {
        if (!set || signum < 1 || signum > 1024)
            return -1;
        uint32_t *words = static_cast<uint32_t *>(set);
        const unsigned int bit = static_cast<unsigned int>(signum - 1);
        words[bit / 32] |= 1U << (bit % 32);
        return 0;
    }

    int bridgeSigprocmask(int how, const void *set, void *oldset)
    {
        static uint8_t currentMask[128] = {};
        if (oldset)
            memcpy(oldset, currentMask, sizeof(currentMask));
        if (!set)
            return 0;

        const uint8_t *requested = static_cast<const uint8_t *>(set);
        if (how == 0) // SIG_BLOCK
        {
            for (size_t i = 0; i < sizeof(currentMask); ++i)
                currentMask[i] |= requested[i];
        }
        else if (how == 1) // SIG_UNBLOCK
        {
            for (size_t i = 0; i < sizeof(currentMask); ++i)
                currentMask[i] &= static_cast<uint8_t>(~requested[i]);
        }
        else if (how == 2) // SIG_SETMASK
        {
            memcpy(currentMask, requested, sizeof(currentMask));
        }
        else
        {
            return -1;
        }
        return 0;
    }

    /* N2 runs its I/O service off an interval timer: a 20 ms ITIMER_REAL and a
     * sigwait() loop, one JVIO exchange per tick.  Windows has no POSIX
     * signals, so the timer lives here and sigwait() waits out the period. */
    constexpr int linuxSigalrm = 14;

    struct LinuxTimeval
    {
        int32_t seconds;
        int32_t microseconds;
    };

    struct LinuxItimerval
    {
        LinuxTimeval interval;
        LinuxTimeval value;
    };

    std::mutex intervalTimerMutex;
    int64_t intervalTimerPeriodUs = 0;  // 0 while the timer is disarmed
    int64_t intervalTimerNextUs = 0;

    int64_t monotonicMicroseconds()
    {
        static LARGE_INTEGER frequency = []() {
            LARGE_INTEGER value;
            QueryPerformanceFrequency(&value);
            return value;
        }();

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return now.QuadPart * 1000000 / frequency.QuadPart;
    }

    int bridgeSetitimer(int which, const void *newValue, void *oldValue)
    {
        const LinuxItimerval *value = static_cast<const LinuxItimerval *>(newValue);
        LinuxItimerval *old = static_cast<LinuxItimerval *>(oldValue);

        std::lock_guard<std::mutex> lock(intervalTimerMutex);

        if (old)
        {
            old->interval.seconds = static_cast<int32_t>(intervalTimerPeriodUs / 1000000);
            old->interval.microseconds = static_cast<int32_t>(intervalTimerPeriodUs % 1000000);
            old->value = old->interval;
        }

        if (!value)
            return 0;

        // Only ITIMER_REAL raises SIGALRM; the profiling timers have no caller.
        if (which != 0)
        {
            log_debug("setitimer: ignoring timer %d", which);
            return 0;
        }

        const int64_t first = static_cast<int64_t>(value->value.seconds) * 1000000 +
                              value->value.microseconds;
        const int64_t period = static_cast<int64_t>(value->interval.seconds) * 1000000 +
                               value->interval.microseconds;

        intervalTimerPeriodUs = period;
        intervalTimerNextUs = first > 0 ? monotonicMicroseconds() + first : 0;
        log_info("setitimer: %lld us interval timer armed", static_cast<long long>(period));
        return 0;
    }

    int bridgeSigwait(const void *set, int *sig)
    {
        int64_t waitUntil = 0;
        {
            std::lock_guard<std::mutex> lock(intervalTimerMutex);
            if (intervalTimerNextUs == 0)
            {
                /* Nothing armed.  The service loop retries on any non-zero
                 * return, so pace it at the cabinet's period and report the
                 * alarm rather than spinning on an error. */
                intervalTimerPeriodUs = intervalTimerPeriodUs ? intervalTimerPeriodUs : 20000;
                intervalTimerNextUs = monotonicMicroseconds() + intervalTimerPeriodUs;
            }
            waitUntil = intervalTimerNextUs;

            /* Advance from the previous deadline so ticks do not drift, but
             * resynchronise when catching up would fire a burst of them. */
            const int64_t period = intervalTimerPeriodUs > 0 ? intervalTimerPeriodUs : 20000;
            const int64_t now = monotonicMicroseconds();
            intervalTimerNextUs =
                waitUntil + period < now ? now + period : waitUntil + period;
        }

        for (;;)
        {
            const int64_t remaining = waitUntil - monotonicMicroseconds();
            if (remaining <= 0)
                break;
            Sleep(static_cast<DWORD>(remaining / 1000 > 0 ? remaining / 1000 : 1));
        }

        (void)set;
        if (sig)
            *sig = linuxSigalrm;
        return 0;
    }

    int bridgeSigaction(int signum, const struct linux_sigaction *act, struct linux_sigaction *oldact)
    {
        log_debug("sigaction(%d) called", signum);

        int win_sig = -1;
        switch (signum)
        {
            case 2: // SIGINT
                win_sig = SIGINT;
                break;
            case 5:                // SIGTRAP
                win_sig = SIGABRT; // Map to SIGABRT
                break;
            case 15: // SIGTERM
                win_sig = SIGTERM;
                break;
            case 6: // SIGABRT
                win_sig = SIGABRT;
                break;
            case 8: // SIGFPE
                win_sig = SIGFPE;
                break;
            case 4: // SIGILL
                win_sig = SIGILL;
                break;
            case 11: // SIGSEGV
                win_sig = SIGSEGV;
                break;
            default:
                log_info("sigaction: unsupported signal %d", signum);
                return -1;
        }

        void (*old_handler)(int) = SIG_DFL;

        if (act)
        {
            old_handler = signal(win_sig, act->sa_handler);
            if (old_handler == SIG_ERR)
                return -1;
        }
        else if (oldact)
        {
            old_handler = signal(win_sig, SIG_DFL);
            signal(win_sig, old_handler);
        }

        if (oldact)
        {
            memset(oldact, 0, sizeof(struct linux_sigaction));
            oldact->sa_handler = old_handler;
        }

        return 0;
    }

    int bridgeKill(int pid, int sig)
    {
        int my_pid = _getpid();
        log_info("kill(%d, %d) called", pid, sig);

        if (pid == my_pid || pid == 0)
        {
            // Signal 0 is existence check
            if (sig == 0)
                return 0;

            // SIGKILL (9) or SIGTERM (15) -> Terminate self
            if (sig == 9 || sig == 15)
            {
                log_info("kill: Process requested self-termination");
                exit(0);
            }
        }

        return 0;
    }

    int bridgeWait(int *wstatus)
    {
        log_info("wait() called: No child processes to wait for. Returning ECHILD.");

        Sleep(10);

        if (wstatus)
            *wstatus = 0;
        errno = ECHILD;
        return -1;
    }

    int bridgeGetpagesize()
    {
        log_trace("Intercepted getpagesize");
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        return sysInfo.dwPageSize;
    }

#define L_MAP_FAILED ((void *)-1)
#define L_MAP_ANONYMOUS 0x20
#define L_PROT_EXEC 0x4

    void *bridgeMmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
    {
        log_trace("Intercepted mmap: addr=%p, len=%zu, prot=%d, flags=%d, fd=%d, offset=%ld", addr, length, prot, flags, fd, (long)offset);

        if (fd >= 0)
        {
            if (void *deviceMapping = VirtualDeviceRegistry::map(fd, addr, length, prot, flags, offset))
                return deviceMapping;
        }

        if (flags & L_MAP_ANONYMOUS)
        {
            DWORD winProt = PAGE_READWRITE;
            if (prot & L_PROT_EXEC)
                winProt = PAGE_EXECUTE_READWRITE;

            void *ret = VirtualAlloc(addr, length, MEM_COMMIT | MEM_RESERVE, winProt);
            if (!ret)
                return L_MAP_FAILED;
            return ret;
        }

        // Basic fallback for file mapping
        void *ret = VirtualAlloc(addr, length, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (ret && fd >= 0)
        {
            log_info("mmap: Emulating file mapping by reading into VirtualAlloc memory (fd=%d)", fd);
            _lseek(fd, offset, SEEK_SET);
            _read(fd, ret, length);
        }
        return ret ? ret : L_MAP_FAILED;
    }

    int bridgeMunmap(void *addr, size_t length)
    {
        log_trace("Intercepted munmap: addr=%p, len=%zu", addr, length);
        if (VirtualFree(addr, 0, MEM_RELEASE))
        {
            return 0;
        }
        return -1;
    }

    int bridgePoll(struct pollfd *fds, int nfds, int timeout)
    {
        return NetworkBridge::bridgePoll(fds, nfds, timeout);
    }

    int bridgeWmemcmp(const uint32_t *s1, const uint32_t *s2, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            if (s1[i] < s2[i])
                return -1;
            if (s1[i] > s2[i])
                return 1;
        }
        return 0;
    }

    uint32_t *bridgeWmemcpy(uint32_t *dest, const uint32_t *src, size_t n)
    {
        for (size_t i = 0; i < n; i++)
            dest[i] = src[i];
        return dest;
    }

    uint32_t *bridgeWmemset(uint32_t *s, uint32_t c, size_t n)
    {
        for (size_t i = 0; i < n; i++)
            s[i] = c;
        return s;
    }

    uint32_t *bridgeWmemchr(const uint32_t *s, uint32_t c, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            if (s[i] == c)
                return (uint32_t *)(s + i);
        }
        return nullptr;
    }

    size_t bridgeWcslen(const uint32_t *s)
    {
        size_t len = 0;
        while (s[len])
            len++;
        return len;
    }

    uint32_t *bridgeWcscpy(uint32_t *dest, const uint32_t *src)
    {
        size_t i = 0;
        while ((dest[i] = src[i]) != 0)
            i++;
        return dest;
    }

    uint32_t *bridgeWcsncpy(uint32_t *dest, const uint32_t *src, size_t n)
    {
        size_t i;
        for (i = 0; i < n && src[i] != 0; i++)
            dest[i] = src[i];
        for (; i < n; i++)
            dest[i] = 0;
        return dest;
    }

    int bridgeWcscmp(const uint32_t *s1, const uint32_t *s2)
    {
        while (*s1 && (*s1 == *s2))
        {
            s1++;
            s2++;
        }
        return (*s1 > *s2) - (*s1 < *s2);
    }

    int bridgeWcscoll(const uint32_t *s1, const uint32_t *s2)
    {
        return bridgeWcscmp(s1, s2); // Basic fallback
    }

    int bridgeWcsncmp(const uint32_t *s1, const uint32_t *s2, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            if (s1[i] != s2[i])
                return (s1[i] > s2[i]) - (s1[i] < s2[i]);
            if (s1[i] == 0)
                break;
        }
        return 0;
    }

    uint32_t *bridgeWcschr(const uint32_t *s, uint32_t c)
    {
        while (*s != c)
        {
            if (!*s++)
                return nullptr;
        }
        return (uint32_t *)s;
    }

    uint32_t *bridgeWcsrchr(const uint32_t *s, uint32_t c)
    {
        const uint32_t *last = nullptr;
        do
        {
            if (*s == c)
                last = s;
        } while (*s++);
        return (uint32_t *)last;
    }

    uint32_t *bridgeWcsstr(const uint32_t *haystack, const uint32_t *needle)
    {
        if (!*needle)
            return (uint32_t *)haystack;
        for (; *haystack; haystack++)
        {
            if (*haystack == *needle)
            {
                const uint32_t *h = haystack, *n = needle;
                while (*h && *n && *h == *n)
                {
                    h++;
                    n++;
                }
                if (!*n)
                    return (uint32_t *)haystack;
            }
        }
        return nullptr;
    }

    double bridgeWcstod(const uint32_t *nptr, uint32_t **endptr)
    {
        char buf[256];
        size_t i = 0;
        while (nptr[i] && i < 255)
        {
            buf[i] = (char)nptr[i];
            i++;
        }
        buf[i] = 0;
        char *end = nullptr;
        double res = strtod(buf, &end);
        if (endptr)
            *endptr = (uint32_t *)(nptr + (end - buf));
        return res;
    }

    long bridgeWcstol(const uint32_t *nptr, uint32_t **endptr, int base)
    {
        char buf[256];
        size_t i = 0;
        while (nptr[i] && i < 255)
        {
            buf[i] = (char)nptr[i];
            i++;
        }
        buf[i] = 0;
        char *end = nullptr;
        long res = strtol(buf, &end, base);
        if (endptr)
            *endptr = (uint32_t *)(nptr + (end - buf));
        return res;
    }

    int bridgeWctob(uint32_t c)
    {
        return (c < 128) ? (int)c : -1;
    }

    uint32_t bridgeWctype(const char *property)
    {
        return (uint32_t)wctype(property);
    }

    size_t bridgeWcsrtombs(char *dst, const uint32_t **src, size_t len, void *ps)
    {
        if (!src || !*src)
            return (size_t)-1;
        const uint32_t *s = *src;
        size_t count = 0;
        if (!dst)
        {
            while (*s)
            {
                if (*s > 255)
                {
                    errno = EILSEQ;
                    return (size_t)-1;
                }
                count++;
                s++;
            }
            return count;
        }
        while (count < len)
        {
            if (*s == 0)
            {
                dst[count] = 0;
                *src = nullptr;
                return count;
            }
            if (*s > 255)
            {
                errno = EILSEQ;
                return (size_t)-1;
            }
            dst[count++] = (char)*s++;
        }
        *src = s;
        return count;
    }

    size_t bridgeWcrtomb(char *s, uint32_t wc, void *ps)
    {
        if (!s)
            return 1;
        if (wc > 255)
        {
            errno = EILSEQ;
            return (size_t)-1;
        }
        *s = (char)wc;
        return 1;
    }

    size_t bridgeWcsftime(uint32_t *s, size_t maxsize, const uint32_t *format, const struct tm *timeptr)
    {
        char fmt[256];
        char out[256];
        size_t i = 0;
        while (format[i] && i < 255)
        {
            fmt[i] = (char)format[i];
            i++;
        }
        fmt[i] = 0;
        size_t cap = (maxsize < 256) ? maxsize : 256;
        size_t res = strftime(out, cap, fmt, timeptr);
        for (i = 0; i < res; i++)
            s[i] = (uint32_t)out[i];
        if (maxsize > res)
            s[res] = 0;
        return res;
    }

    size_t bridgeWcsxfrm(uint32_t *dst, const uint32_t *src, size_t n)
    {
        size_t i = 0;
        while (src[i] && i < n)
        {
            if (dst)
                dst[i] = src[i];
            i++;
        }
        if (dst && i < n)
            dst[i] = 0;
        return bridgeWcslen(src);
    }

    size_t bridgeWcstombs(char *dst, const uint32_t *src, size_t len)
    {
        log_trace("Intercepted wcstombs(dst=%p, src=%p, len=%zu)", dst, src, len);
        if (!src)
        {
            errno = EILSEQ;
            return (size_t)-1;
        }
        size_t count = 0;
        if (!dst)
        {
            // Count only: determine how many bytes would be written
            while (src[count])
            {
                if (src[count] > 255)
                {
                    errno = EILSEQ;
                    return (size_t)-1;
                }
                count++;
            }
            return count;
        }
        while (count < len && src[count])
        {
            if (src[count] > 255)
            {
                errno = EILSEQ;
                return (size_t)-1;
            }
            dst[count] = (char)src[count];
            count++;
        }
        if (count < len)
            dst[count] = 0;
        return count;
    }

    size_t bridgeMbrtowc(uint32_t *pwc, const char *s, size_t n, void *ps)
    {
        if (!s)
            return 0;
        if (n == 0)
            return (size_t)-2;
        if (pwc)
            *pwc = (uint32_t)(unsigned char)*s;
        return (*s == 0) ? 0 : 1;
    }

    int bridgeMbtowc(uint32_t *pwc, const char *s, size_t n)
    {
        if (!s)
            return 0;
        if (n == 0)
            return -1;
        if (pwc)
            *pwc = (uint32_t)(unsigned char)*s;
        return (*s == 0) ? 0 : 1;
    }

    size_t bridgeMbstowcs(uint32_t *dest, const char *src, size_t n)
    {
        if (!src)
        {
            errno = EILSEQ;
            return (size_t)-1;
        }
        size_t i = 0;
        if (!dest)
            return strlen(src);
        for (; i < n && src[i]; i++)
        {
            dest[i] = (uint32_t)(unsigned char)src[i];
        }
        if (i < n)
            dest[i] = 0;
        return i;
    }

    uint32_t bridgeBtowc(int c)
    {
        if (c == EOF)
            return (uint32_t)-1;
        return (uint32_t)(unsigned char)c;
    }

    uint32_t bridgePutwc(uint32_t wc, FILE *stream)
    {
        int r = fputc((int)(wc & 0xFF), stream);
        return (r == EOF) ? (uint32_t)-1 : wc;
    }

    uint32_t bridgeGetwc(FILE *stream)
    {
        int c = fgetc(stream);
        return (c == EOF) ? (uint32_t)-1 : (uint32_t)c;
    }

    uint32_t bridgeUngetwc(uint32_t wc, FILE *stream)
    {
        int r = ungetc((int)(wc & 0xFF), stream);
        return (r == EOF) ? (uint32_t)-1 : wc;
    }

    FILE *bridgePopen(const char *command, const char *type)
    {
        log_info("Intercepted popen: %s %s", command, type);
        return nullptr;
    }

    int bridgePclose(FILE *stream)
    {
        log_info("Intercepted pclose: %p", stream);
        return -1;
    }

    void bridgePerror(const char *s)
    {
        log_info("Intercepted perror: %s", s);
    }

    char *bridgeStrerrorR(int errorNumber, char *buffer, size_t length)
    {
        const char *message = strerror(errorNumber);
        if (!message)
            message = "Unknown error";
        if (!buffer || length == 0)
            return const_cast<char *>(message);

        snprintf(buffer, length, "%s", message);
        return buffer;
    }

    char *bridgeRealpath(const char *path, char *resolved_path)
    {
        if (!path || !resolved_path)
            return nullptr;

        char winPath[MAX_PATH];
        ConvertPath(winPath, path, MAX_PATH);

        char *result = _fullpath(resolved_path, winPath, MAX_PATH);
        if (!result)
            return nullptr;

        // Convert backslashes to forward slashes for Linux compatibility
        for (char *p = result; *p; p++)
        {
            if (*p == '\\')
                *p = '/';
        }

        return result;
    }

} // namespace LibcBridge

#endif
