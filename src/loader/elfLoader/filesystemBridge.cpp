#if defined(_WIN32) || defined(__MINGW32__)
#include "../redirections/filesystem.h"
#include "filesystemBridge.hpp"
#include "libcBridge.hpp"
#include "memoryManager.hpp"
#include "networkBridge.hpp"
#include "randomDevice.hpp"
#include "symbolResolver.hpp"
#include "virtualDeviceRegistry.hpp"
#include "../graphics/sdlCalls.h"
#include "../hardware/namco/n2/n2VirtualDevices.h"
#include "../hardware/namco/es1/es1.h"
#include "../hardware/namco/es1/es1Title.h"
#include "../platform/platformBackend.h"
#include "../log/log.h"
#include <string>
#include <windows.h>
#include <io.h>
#include <conio.h>
#include <direct.h>
#include <cstdarg>
#include "../config/config.h"

extern std::string g_absoluteElfPath;

namespace Es1CompatBridge
{
bool isEventfd(int fd);
int readEventfd(int fd, void *buffer, size_t length);
int writeEventfd(int fd, const void *buffer, size_t length);
int closeEventfd(int fd);
bool isEpoll(int fd);
int closeEpoll(int fd);
void forgetDescriptor(int fd);
}

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace FileSystemBridge
{
    static int bridgeOpenDescriptor(const char *path, int flags, ...)
    {
        const auto result = VirtualDeviceRegistry::open(path, flags);
        if (result.claimed)
            return result.descriptor;

        int mode = 0;
        if (flags & 0x40) // Linux O_CREAT
        {
            va_list arguments;
            va_start(arguments, flags);
            mode = va_arg(arguments, int);
            va_end(arguments);
        }
        const int descriptor = sharedOpen(path, flags, mode);
        log_debug("open(\"%s\", flags=0x%x, mode=0%o) -> %d (errno=%d)",
                  path ? path : "NULL", flags, mode, descriptor,
                  descriptor < 0 ? errno : 0);
        return descriptor;
    }

    /* A long load presents no frame, which is all Windows needs to call the
     * window hung, so the message queue is drained from the reads the game
     * spends that time in. */
    static size_t bridgeFread(void *buffer, size_t size, size_t count, FILE *stream)
    {
        keepWindowResponsive();
        return sharedFread(buffer, size, count, stream);
    }

    static ssize_t bridgeReadDescriptor(int fd, void *buffer, size_t count)
    {
        keepWindowResponsive();
        if (Es1CompatBridge::isEventfd(fd))
            return Es1CompatBridge::readEventfd(fd, buffer, count);
        if (const auto *device = VirtualDeviceRegistry::find(fd))
            return device->read(fd, buffer, count);
        if (NetworkBridge::isSocketDescriptor(fd))
            return NetworkBridge::bridgeSocketRead(fd, buffer, count);
        return sharedRead(fd, buffer, count);
    }

    static ssize_t bridgeWriteDescriptor(int fd, const void *buffer, size_t count)
    {
        /* The guest's C++ streams reach the console as write() on 1 and 2. */
        if ((fd == 1 || fd == 2) && !logIsEnabled(LOG_GAME))
            return static_cast<ssize_t>(count);
        if (Es1CompatBridge::isEventfd(fd))
            return Es1CompatBridge::writeEventfd(fd, buffer, count);
        if (const auto *device = VirtualDeviceRegistry::find(fd))
            return device->write(fd, buffer, count);
        if (NetworkBridge::isSocketDescriptor(fd))
            return NetworkBridge::bridgeSocketWrite(fd, buffer, count);
        return sharedWrite(fd, buffer, count);
    }

    static int bridgeCloseDescriptor(int fd)
    {
        /* Linux drops a closed descriptor from every epoll set it was in, and
         * Boost.Asio leaves the removal to the kernel.  Do it here, before the
         * number can be handed out again. */
        Es1CompatBridge::forgetDescriptor(fd);
        if (Es1CompatBridge::isEventfd(fd))
            return Es1CompatBridge::closeEventfd(fd);
        if (Es1CompatBridge::isEpoll(fd))
            return Es1CompatBridge::closeEpoll(fd);
        if (const auto *device = VirtualDeviceRegistry::find(fd))
            return device->close(fd);
        if (NetworkBridge::isSocketDescriptor(fd))
            return NetworkBridge::bridgeSocketClose(fd);
        return sharedClose(fd);
    }

    static int bridgeIoctlDescriptor(int fd, unsigned long request, ...)
    {
        va_list arguments;
        va_start(arguments, request);
        void *argument = va_arg(arguments, void *);
        va_end(arguments);

        if (const auto *device = VirtualDeviceRegistry::find(fd))
            return device->ioctl(fd, request, argument);
        if (NetworkBridge::isSocketDescriptor(fd))
            return NetworkBridge::bridgeSocketIoctl(fd, request, argument);
        return sharedIoctl(fd, request, argument);
    }

    void initBridges()
    {
        log_info("Initializing FileSystemBridge...");

        /* Bridge symbols go in before initConfig() identifies the ELF, so
         * register every provider now and let claimsPath() decide later. */
        VirtualDeviceRegistry::clear();
        /* Not hardware: /dev/urandom is part of the platform every Linux
         * program assumes, and OpenSSL will not start a handshake without it. */
        registerRandomDevices();
        platformRegisterVirtualDevices();

        // Standard I/O functions
        MAP("fopen", sharedFopen);
        MAP("fread", bridgeFread);
        MAP("fwrite", bridgeFwrite);
        MAP("fseek", sharedFseek);
        MAP("ftell", sharedFtell);
        MAP("fclose", sharedFclose);
        MAP("ferror", bridgeFerror);
        MAP("rewind", sharedRewind);
        MAP("feof", bridgeFeof);
        MAP("fgets", sharedFgets);
        MAP("fgetc", bridgeFgetc);
        MAP("_IO_getc", bridgeFgetc);
        MAP("fflush", fflush);
        MAP("fputs", bridgeFputs);
        MAP("fputc", bridgeFputc);
        MAP("getc", bridgeGetc);
        MAP("ungetc", bridgeUngetc);

        MAP("flock", LibcBridge::bridgeStubSuccess);

        // LFS (Large File Support) functions
        MAP("fopen64", sharedFopen);
        MAP("fseeko64", sharedFseeko64);
        MAP("lseek64", bridgeLseek64);
        MAP("ftello64", sharedFtello64);

        MAP("fileno", bridgeFileno);
        MAP("_fileno", bridgeFileno);

        // POSIX low-level I/O functions
        MAP("read", bridgeReadDescriptor);
        MAP("write", bridgeWriteDescriptor);
        MAP("__write", bridgeWriteDescriptor);
        MAP("close", bridgeCloseDescriptor);
        MAP("lseek", bridgeLseek);
        MAP("open", bridgeOpenDescriptor);
        MAP("open64", bridgeOpenDescriptor);
        /* _FORTIFY_SOURCE rewrites two-argument open() calls to __open_2; it
         * differs only in rejecting a missing mode for O_CREAT, which the
         * variadic bridge already tolerates. */
        MAP("__open_2", bridgeOpenDescriptor);
        MAP("__open64_2", bridgeOpenDescriptor);
        MAP("readlink", bridgeReadlink);
        MAP("fdopen", bridgeFdopen);
        MAP("setvbuf", bridgeSetvbuf);
        MAP("writev", bridgeWritev);
        MAP("readv", bridgeReadv);
        MAP("dup", bridgeDup);

        // file IO
        MAP("fsync", bridgeFsync);
        MAP("sync", bridgeSync);
        MAP("fdatasync", bridgeFdatasync);
        MAP("access", bridgeAccess);
        MAP("chmod", bridgeChmod);
        MAP("chdir", bridgeChdir);


        MAP("ioctl", bridgeIoctlDescriptor);
        MAP("select", sharedSelect);
        MAP("creat", bridgeCreat);
        MAP("mkdir", sharedMkdir);
        MAP("getcwd", bridgeGetcwd);
        MAP("opendir", bridgeOpendir);
        MAP("readdir", bridgeReaddir);
        MAP("closedir", bridgeClosedir);
        MAP("rewinddir", bridgeRewinddir);
        MAP("clearerr", clearerr);

        MAP("unlink", bridgeUnlink);
        MAP("remove", sharedRemove);
        MAP("rename", bridgeRename);

        MAP("stat", bridgeStat);
        MAP("fstat", bridgeFstat);
        MAP("__xstat", bridgeXstat);
        MAP("__xstat64", bridgeXstat64);
        MAP("__lxstat", bridgeLxstat);
        MAP("__fxstat", bridgeFxstat);
        MAP("__fxstat64", bridgeFxstat64);
        MAP("__xmknod", bridgeXmknod);
        MAP("fcntl", bridgeFcntl);
        MAP("__lxstat64", bridgeLxstat64);
    }

    /* Same reason as the printf bridges: the guest's console output ignored the
     * log level. A real file still gets its bytes; only stdout and stderr are
     * quietened, and LL_LOG_LEVEL=game brings them back. */
    bool guestConsoleIsQuiet(FILE *stream)
    {
        return (stream == stdout || stream == stderr) && !logIsEnabled(LOG_GAME);
    }

    size_t bridgeFwrite(const void *ptr, size_t size, size_t count, FILE *stream)
    {
        if (guestConsoleIsQuiet(stream))
            return count;
        return fwrite(ptr, size, count, stream);
    }

    int bridgeFerror(FILE *stream)
    {
        log_trace("Intercepted ferror: %p", stream);
        return ferror(stream);
    }

    int bridgeFeof(FILE *stream)
    {
        log_trace("Intercepted feof: %p", stream);
        return feof(stream);
    }

    int bridgeFgetc(FILE *stream)
    {
        log_trace("Intercepted fgetc: %p", stream);
        /* Alchemy polls a non-blocking stdin once per frame, and the CRT
         * ignores the Linux fcntl flags, so a plain fgetc() would block the
         * render loop.  Keep the legacy path without ever waiting. */
        if (getConfig()->platform == ARCADE_PLATFORM_NAMCO_N2 && stream == stdin)
            return _kbhit() ? _getch() : EOF;
        return fgetc(stream);
    }

    int bridgeFileno(FILE *stream)
    {
        log_trace("Intercepted fileno: %p", stream);
        return fileno(stream);
    }

    long int bridgeLseek(int fd, long int offset, int whence)
    {
        log_trace("Intercepted lseek: %d %ld %d", fd, offset, whence);
        return lseek(fd, offset, whence);
    }

    long long bridgeLseek64(int fd, long long offset, int whence)
    {
        return _lseeki64(fd, offset, whence);
    }

    int bridgeReadlink(const char *path, char *buf, size_t bufsiz)
    {
        log_trace("Intercepted readlink: %s", path ? path : "NULL");
        if (!path || !buf || bufsiz == 0)
            return -1;

        if (strcmp(path, "/proc/self/exe") == 0)
        {
            const char *elfPath = g_absoluteElfPath.c_str();
            if (elfPath[0] == '\0')
            {
                log_warn("readlink: /proc/self/exe requested but ELF path not set");
                return -1;
            }
            size_t len = strlen(elfPath);
            if (len > bufsiz)
                len = bufsiz;
            memcpy(buf, elfPath, len);
            return (int)len;
        }
        log_error("readlink not implemented for %s", path);
        return -1;
    }
    
    FILE* bridgeFdopen(int fd, const char* mode)
    {
        log_trace("Intercepted fdopen");
        return fdopen(fd, mode);
    }

    int bridgeSetvbuf(FILE *stream, char *buf, int mode, size_t size)
    {
        log_trace("Intercepted setvbuf");
        return setvbuf(stream, buf, mode, size);
    }

    /* These go through the same dispatch as read() and write(); the CRT calls
     * they replaced reached none of the emulated descriptors. */
    ssize_t bridgeWritev(int fd, const struct iovec *iov, int iovcnt)
    {
        log_trace("Intercepted writev");
        ssize_t totalWritten = 0;
        for (int i = 0; i < iovcnt; ++i)
        {
            const ssize_t written = bridgeWriteDescriptor(fd, iov[i].iov_base, iov[i].iov_len);
            if (written < 0)
                return totalWritten == 0 ? -1 : totalWritten;
            totalWritten += written;
            if (static_cast<size_t>(written) < iov[i].iov_len)
                break;
        }
        return totalWritten;
    }

    ssize_t bridgeReadv(int fd, const struct iovec *iov, int iovcnt)
    {
        log_trace("Intercepted readv");
        ssize_t totalRead = 0;
        for (int i = 0; i < iovcnt; ++i)
        {
            const ssize_t bytesRead = bridgeReadDescriptor(fd, iov[i].iov_base, iov[i].iov_len);
            if (bytesRead < 0)
                return totalRead == 0 ? -1 : totalRead;
            if (bytesRead == 0)
                break;
            totalRead += bytesRead;
            if (static_cast<size_t>(bytesRead) < iov[i].iov_len)
                break;
        }
        return totalRead;
    }

    int bridgeDup(int fd)
    {
        log_trace("Intercepted dup: %d", fd);
        return dup(fd);
    }

    extern "C" int bridgeFsync(int fd)
    {
        return _commit(fd);
    }
    extern "C" int bridgeFdatasync(int fd)
    {
        return _commit(fd);
    }
    extern "C" void bridgeSync(void)
    {
        _flushall();
    }

    int bridgeAccess(const char *pathname, int mode)
    {
        if (pathname && strstr(pathname, "/dev/") != NULL)
        {
            return 0; // Success
        }
        if (mode == 1)
            mode = 0;

        char winPath[MAX_PATH];
        ConvertPath(winPath, redirectTempPath(pathname), MAX_PATH);
        return _access(winPath, mode);
    }

    int bridgeChdir(const char *path)
    {
        char winPath[MAX_PATH];
        ConvertPath(winPath, redirectTempPath(path), MAX_PATH);
        if (getConfig()->platform == ARCADE_PLATFORM_NAMCO_N2)
            log_warn("Namco N2: chdir %s (as %s)", path, winPath);
        log_debug("chdir: %s (as %s)", path, winPath);
        return _chdir(winPath);
    }

    int bridgeChmod(const char *filename, int pmode)
    {
        char winPath[MAX_PATH];
        ConvertPath(winPath, redirectTempPath(filename), MAX_PATH);
        return _chmod(winPath, pmode);
    }

    int bridgeCreat(const char *pathname, int mode)
    {
        pathname = redirectTempPath(pathname);

        char winPath[MAX_PATH];
        ConvertPath(winPath, pathname, MAX_PATH);
        return _creat(winPath, mode);
    }

    char *bridgeGetcwd(char *buf, size_t size)
    {
        if (buf != nullptr)
            return _getcwd(buf, (int)size);
        char *tmp = _getcwd(NULL, 0);
        if (!tmp)
            return nullptr;
        size_t len = strlen(tmp) + 1;
        if (size > 0 && size < len)
            len = size;
        char *aligned_ptr = static_cast<char *>(MemoryManager::customMalloc(len));
        if (aligned_ptr)
        {
            strncpy(aligned_ptr, tmp, len);
            aligned_ptr[len - 1] = '\0';
        }
        free(tmp);
        return aligned_ptr;
    }
} // namespace FileSystemBridge

extern "C"
{
    void *bridgeOpendir(const char *name)
    {
        log_debug("opendir(\"%s\")", name);

        if(strncmp(name, "/proc", 5) == 0)
            return 0;
            
        char winPath[MAX_PATH];
        ConvertPath(winPath, redirectTempPath(name), MAX_PATH);

        if (strcmp(winPath, "\\home\\disk1\\rankingdata") == 0 &&
            (getConfig()->gameGroup == GROUP_OUTRUN || getConfig()->gameGroup == GROUP_OUTRUN_TEST))
        {
            strcpy(winPath, ".\\rankingdata");
        }

        std::string searchPath = winPath;
        if (searchPath.empty())
            return NULL;

        if (searchPath.back() == '/' || searchPath.back() == '\\')
        {
            searchPath += "*";
        }
        else
        {
            searchPath += "\\*";
        }

        DIR_Impl *dir = new DIR_Impl();
        dir->path = name; // logging uses original name
        dir->hFind = FindFirstFileA(searchPath.c_str(), &dir->findData);

        if (dir->hFind == INVALID_HANDLE_VALUE)
        {
            delete dir;
            return NULL;
        }

        dir->first_read = true;
        dir->finished = false;
        return (void *)dir;
    }

    struct linux_dirent *bridgeReaddirPosix(void *dirp)
    {
        if (!dirp)
        {
            errno = EINVAL;
            return NULL;
        }
        DIR_Impl *dir = (DIR_Impl *)dirp;

        // POSIX readdir reports end-of-directory with a null result and no
        // error.  Clear stale CRT errno values so Boost.Filesystem does not
        // turn a normal FindNextFile EOF into an EINVAL exception.
        errno = 0;

        if (dir->finished)
            return NULL;

        if (dir->first_read)
        {
            dir->first_read = false;
        }
        else
        {
            if (!FindNextFileA(dir->hFind, &dir->findData))
            {
                dir->finished = true;
                return NULL;
            }
        }

        memset(&dir->ent, 0, sizeof(linux_dirent));
        dir->ent.d_ino = 1;
        dir->ent.d_off = 0;

        strncpy(dir->ent.d_name, dir->findData.cFileName,
                sizeof(dir->ent.d_name) - 1);
        dir->ent.d_name[sizeof(dir->ent.d_name) - 1] = '\0';
        dir->ent.d_reclen = (unsigned short)strlen(dir->ent.d_name);

        if (dir->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            dir->ent.d_type = 4; // DT_DIR
        }
        else
        {
            dir->ent.d_type = 8; // DT_REG
        }

        return &dir->ent;
    }

    /* Some enumerators leave the scan only on a non-zero errno and loop forever
     * otherwise, so end of directory is reported as EBADF.  Only for plain
     * readdir(), and only where the title's record asks for it. */
    struct linux_dirent *bridgeReaddir(void *dirp)
    {
        struct linux_dirent *entry = bridgeReaddirPosix(dirp);
        if (!entry && dirp && es1TitleQuirks()->readdirEndOfDirectoryIsError)
            errno = EBADF;
        return entry;
    }

    int bridgeClosedir(void *dirp)
    {
        if (!dirp)
            return -1;
        DIR_Impl *dir = (DIR_Impl *)dirp;
        if (dir->hFind != INVALID_HANDLE_VALUE)
        {
            FindClose(dir->hFind);
        }
        delete dir;
        return 0;
    }


    /* Restart from the top: DIR_Impl keeps its directory name, and a Win32
     * find handle cannot be rewound. */
    void bridgeRewinddir(void *dirp)
    {
        if (!dirp)
            return;
        DIR_Impl *dir = (DIR_Impl *)dirp;

        char winPath[MAX_PATH];
        ConvertPath(winPath, redirectTempPath(dir->path.c_str()), MAX_PATH);

        std::string searchPath = winPath;
        if (searchPath.empty())
            return;
        if (searchPath.back() == '/' || searchPath.back() == '\\')
            searchPath += "*";
        else
            searchPath += "\\*";

        if (dir->hFind != INVALID_HANDLE_VALUE)
            FindClose(dir->hFind);

        dir->hFind = FindFirstFileA(searchPath.c_str(), &dir->findData);
        dir->first_read = true;
        dir->finished = dir->hFind == INVALID_HANDLE_VALUE;
    }


    int bridgeUnlink(const char *pathname)
    {
        pathname = redirectTempPath(pathname);

        char winPath[MAX_PATH];
        ConvertPath(winPath, pathname, MAX_PATH);
        return _unlink(winPath);
    }

    int bridgeRename(const char *oldpath, const char *newpath)
    {
        log_debug("rename(\"%s\", \"%s\")", oldpath, newpath);

        char winOld[MAX_PATH];
        char winNew[MAX_PATH];
        ConvertPath(winOld, redirectTempPath(oldpath), MAX_PATH);
        ConvertPath(winNew, redirectTempPath(newpath), MAX_PATH);

        /* POSIX rename() replaces the destination atomically where the CRT's
         * fails with EEXIST, and saves are written temp-then-swap. */
        if (!MoveFileExA(winOld, winNew, MOVEFILE_REPLACE_EXISTING))
        {
            log_warn("rename(\"%s\", \"%s\") failed (Win32 error %lu)", winOld, winNew, GetLastError());
            return -1;
        }
        return 0;
    }

    /* The CRT reports st_ino as 0 for every file, and POSIX code treats
     * (st_dev, st_ino) as identity - SQLite keys its lock table on it.  Report
     * the volume serial and NTFS file reference number instead. */
    struct FileIdentity
    {
        uint64_t dev;
        uint64_t ino;
        bool valid;
    };

    static FileIdentity IdentityFromHandle(HANDLE handle)
    {
        FileIdentity identity{0, 0, false};
        BY_HANDLE_FILE_INFORMATION info{};

        if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &info))
            return identity;

        identity.dev = info.dwVolumeSerialNumber;
        identity.ino = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
        identity.valid = true;
        return identity;
    }

    static FileIdentity IdentityFromFd(int fd)
    {
        const intptr_t osHandle = _get_osfhandle(fd);
        if (osHandle == -1)
            return FileIdentity{0, 0, false};
        return IdentityFromHandle(reinterpret_cast<HANDLE>(osHandle));
    }

    static FileIdentity IdentityFromPath(const char *winPath)
    {
        if (!winPath)
            return FileIdentity{0, 0, false};

        /* FILE_READ_ATTRIBUTES with full sharing never disturbs other openers,
         * and the backup semantics flag lets directories resolve too. */
        HANDLE handle = CreateFileA(winPath, FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return FileIdentity{0, 0, false};

        const FileIdentity identity = IdentityFromHandle(handle);
        CloseHandle(handle);
        return identity;
    }

    /* An i386 struct stat only has room for a 32-bit inode, so fold the file
     * reference number rather than truncating away its high half. */
    static uint32_t FoldInode(uint64_t ino)
    {
        return (uint32_t)(ino ^ (ino >> 32));
    }

    static void CopyStatVer3(const struct _stat64 &src, void *dst_ptr,
                             const FileIdentity &identity = FileIdentity{0, 0, false})
    {
        struct linux_stat_ver3 *dst = (struct linux_stat_ver3 *)dst_ptr;
        memset(dst, 0, sizeof(struct linux_stat_ver3));

        dst->st_dev = identity.valid ? identity.dev : (uint64_t)src.st_dev;
        dst->st_ino = identity.valid ? FoldInode(identity.ino) : (uint32_t)src.st_ino;
        dst->st_nlink = src.st_nlink;
        dst->st_uid = src.st_uid;
        dst->st_gid = src.st_gid;
        dst->st_rdev = (uint64_t)src.st_rdev;
        dst->st_size = (int32_t)src.st_size; // 32-bit size
        dst->st_blksize = 4096;
        dst->st_blocks = (int32_t)((src.st_size + 511) / 512);
        dst->st_atime = (uint32_t)src.st_atime;
        dst->st_mtime = (uint32_t)src.st_mtime;
        dst->st_ctime = (uint32_t)src.st_ctime;

        dst->st_mode = 0;
        if (src.st_mode & _S_IFDIR)
            dst->st_mode |= LINUX_S_IFDIR;
        if (src.st_mode & _S_IFREG)
            dst->st_mode |= LINUX_S_IFREG;
        if (src.st_mode & _S_IFCHR)
            dst->st_mode |= LINUX_S_IFCHR;
        if (src.st_mode & _S_IFIFO)
            dst->st_mode |= LINUX_S_IFIFO;
        dst->st_mode |= (src.st_mode & 0777);
    }

    static void CopyStat64(const struct _stat64 &src, void *dst_ptr,
                           const FileIdentity &identity = FileIdentity{0, 0, false})
    {
        struct linux_stat64_safe dst;
        memset(&dst, 0, sizeof(dst));

        dst.st_dev = identity.valid ? identity.dev : (unsigned long long)src.st_dev;
        dst.__st_ino = identity.valid ? FoldInode(identity.ino) : (uint32_t)src.st_ino;
        dst.st_ino = identity.valid ? identity.ino : (unsigned long long)src.st_ino;
        dst.st_nlink = src.st_nlink;
        dst.st_uid = src.st_uid;
        dst.st_gid = src.st_gid;
        dst.st_rdev = (unsigned long long)src.st_rdev;
        dst.st_size = src.st_size;
        dst.st_blksize = 4096;
        dst.st_blocks = (src.st_size + 511) / 512;
        dst.st_atime = (unsigned long)src.st_atime;
        dst.st_mtime = (unsigned long)src.st_mtime;
        dst.st_ctime = (unsigned long)src.st_ctime;

        dst.st_mode = 0;
        if (src.st_mode & _S_IFDIR)
            dst.st_mode |= LINUX_S_IFDIR;
        if (src.st_mode & _S_IFREG)
            dst.st_mode |= LINUX_S_IFREG;
        if (src.st_mode & _S_IFCHR)
            dst.st_mode |= LINUX_S_IFCHR;
        if (src.st_mode & _S_IFIFO)
            dst.st_mode |= LINUX_S_IFIFO;
        dst.st_mode |= (src.st_mode & 0777);

        memcpy(dst_ptr, &dst, sizeof(dst));
    }

    /* A virtual device descriptor has no Windows handle, so _fstat64() fails and
     * callers such as OpenSSL's RAND_poll() write the file off.  Answer with a
     * character device keyed by descriptor, so two never look like one file. */
    static bool statVirtualDescriptor(int fd, void *buf, bool use_stat64)
    {
        if (!buf || !VirtualDeviceRegistry::find(fd))
            return false;

        if (use_stat64)
        {
            struct linux_stat64_safe *s = (struct linux_stat64_safe *)buf;
            memset(s, 0, sizeof(*s));
            s->st_mode = LINUX_S_IFCHR | 0666;
            s->st_dev = 1;
            s->st_rdev = 1;
            s->st_nlink = 1;
            s->__st_ino = (uint32_t)fd;
            s->st_ino = (unsigned long long)fd;
            s->st_blksize = 4096;
        }
        else
        {
            struct linux_stat_ver3 *s = (struct linux_stat_ver3 *)buf;
            memset(s, 0, sizeof(*s));
            s->st_mode = LINUX_S_IFCHR | 0666;
            s->st_dev = 1;
            s->st_rdev = 1;
            s->st_nlink = 1;
            s->st_ino = (uint32_t)fd;
            s->st_blksize = 4096;
        }
        return true;
    }

    static int myStatImpl(const char *path, void *buf, bool use_stat64)
    {
        path = redirectTempPath(path);

        if (path && (strstr(path, "/dev/") != NULL || strstr(path, "i2c/") != NULL || strstr(path, "ttyS") != NULL))
        {
            log_debug("stat: Spoofing virtual device for %s", path);

            if (use_stat64)
            {
                struct linux_stat64_safe *s = (struct linux_stat64_safe *)buf;
                memset(s, 0, sizeof(*s));
                s->st_mode = LINUX_S_IFCHR | 0666;
                s->st_rdev = 1;
                s->st_nlink = 1;
            }
            else
            {
                struct linux_stat_ver3 *s = (struct linux_stat_ver3 *)buf;
                memset(s, 0, sizeof(*s));
                s->st_mode = LINUX_S_IFCHR | 0666;
                s->st_rdev = 1;
                s->st_nlink = 1;
            }
            return 0;
        }

        struct _stat64 win_stat;
        char winPath[MAX_PATH];
        ConvertPath(winPath, path, MAX_PATH);

        /* Linux stats a directory with or without its trailing slash; the CRT
         * rejects the trailing one outright. */
        size_t winLength = strlen(winPath);
        while (winLength > 1 && winPath[winLength - 1] == '\\' &&
               winPath[winLength - 2] != ':')
            winPath[--winLength] = '\0';

        log_debug("stat: %s (as %s)", path, winPath);
        if (_stat64(winPath, &win_stat) != 0)
        {
            log_debug("stat failed: %s", path);
            errno = ENOENT;
            return -1;
        }

        const FileIdentity identity = IdentityFromPath(winPath);
        if (use_stat64)
        {
            CopyStat64(win_stat, buf, identity);
        }
        else
        {
            CopyStatVer3(win_stat, buf, identity);
        }

        return 0;
    }

    int bridgeStat(const char *path, struct linux_stat64 *buf)
    {
        return myStatImpl(path, buf, false);
    }

    int bridgeFstat(int fd, struct linux_stat64 *buf)
    {
        log_debug("fstat called: fd=%d", fd);
        if (statVirtualDescriptor(fd, buf, false))
            return 0;
        struct _stat64 win_stat;
        if (_fstat64(fd, &win_stat) != 0)
            return -1;
        CopyStatVer3(win_stat, buf, IdentityFromFd(fd));
        return 0;
    }

    int bridgeXstat(int ver, const char *path, struct linux_stat *buf)
    {
        log_debug("__xstat called: ver=%d, path=\"%s\"", ver, path);
        return myStatImpl(path, buf, false);
    }

    int bridgeXstat64(int ver, const char *path, struct linux_stat64 *buf)
    {
        log_debug("__xstat64 called: ver=%d, path=\" % s\"", ver, path);
        return myStatImpl(path, buf, true);
    }

    int bridgeLxstat(int ver, const char *path, struct linux_stat *buf)
    {
        log_debug("__lxstat called: ver=%d, path=\" % s\"", ver, path);
        return bridgeXstat(ver, path, buf);
    }

    int bridgeLxstat64(int ver, const char *path, struct linux_stat64 *buf)
    {
        log_debug("__lxstat64 called: ver=%d, path=\" % s\"", ver, path);
        return bridgeXstat64(ver, path, buf);
    }

    int bridgeFxstat(int ver, int fd, struct linux_stat *buf)
    {
        log_debug("__fxstat called: ver=%d, fd=%d", ver, fd);
        if (!buf)
            return -1;
        if (statVirtualDescriptor(fd, buf, false))
            return 0;
        struct _stat64 win_stat;
        if (_fstat64(fd, &win_stat) != 0)
            return -1;

        CopyStatVer3(win_stat, buf, IdentityFromFd(fd));
        return 0;
    }

    int bridgeFxstat64(int ver, int fd, struct linux_stat64 *buf)
    {
        if (!buf)
            return -1;
        if (statVirtualDescriptor(fd, buf, true))
            return 0;
        struct _stat64 win_stat;
        if (_fstat64(fd, &win_stat) != 0)
            return -1;

        /* ver is the stat ABI revision, not a choice of layout: glibc passes 3
         * to both and picks the struct by function name.  The wrong one puts
         * st_blksize where stat64 keeps the high half of st_size. */
        const FileIdentity identity = IdentityFromFd(fd);
        CopyStat64(win_stat, buf, identity);
        log_debug("__fxstat64: ver=%d fd=%d size=%lld dev=%llu ino=%llu", ver, fd,
                  static_cast<long long>(win_stat.st_size),
                  static_cast<unsigned long long>(identity.dev),
                  static_cast<unsigned long long>(identity.ino));
        return 0;
    }

    int bridgeXmknod(int ver, const char *path, unsigned int mode, void *dev)
    {
        log_debug("__xmknod called: ver=%d, path=\"%s\", mode=0x%x", ver, path, mode);
        return 0;
    }

    int bridgeFcntl(int fd, int cmd, ...)
    {
        log_debug("fcntl(fd=%d, cmd=%d)", fd, cmd);

        /* The ES1 serial ports are virtual descriptors with no host file object,
         * and their reads return EAGAIN when the board has nothing queued. */
        if (VirtualDeviceRegistry::find(fd))
        {
            constexpr int LinuxFGetFl = 3;
            constexpr int LinuxFSetFl = 4;
            constexpr int LinuxOReadWrite = 0x2;
            constexpr int LinuxONonblock = 0x800;
            if (cmd == LinuxFGetFl)
                return LinuxOReadWrite | LinuxONonblock;
            if (cmd == LinuxFSetFl)
                return 0;
        }
        return 0;
    }

    int bridgeFputc(int c, FILE *stream)
    {
        if (!stream)
            return EOF;
        if (FileSystemBridge::guestConsoleIsQuiet(stream))
            return c;

        int ret = EOF;
        ret = fputc(c, stream);

        if (ret == EOF)
        {
            return c;
        }
        return ret;
    }

    int bridgeFputs(const char *str, FILE *stream)
    {
        if (!stream)
            return EOF;
        if (FileSystemBridge::guestConsoleIsQuiet(stream))
            return 0;

        int ret = EOF;
        ret = fputs(str, stream);

        if (ret == EOF)
        {
            return 0;
        }
        return ret;
    }

    int bridgeGetc(FILE *stream)
    {
        log_trace("Intercepted getc: %p", stream);
        return fgetc(stream);
    }

    int bridgeUngetc(int c, FILE *stream)
    {
        log_trace("Intercepted ungetc: %d %p", c, stream);
        return ungetc(c, stream);
    }

}

#endif
