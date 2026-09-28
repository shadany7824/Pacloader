#pragma once

#ifdef __cplusplus
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <cstdint>

class ElfLoader;

class SymbolResolver
{
  public:
    static SymbolResolver &GetInstance()
    {
        static SymbolResolver instance;
        return instance;
    }

    SymbolResolver(SymbolResolver const &) = delete;
    void operator=(SymbolResolver const &) = delete;

    // Initialize library search paths
    void InitSearchPaths(const std::string &libraryPathParam, const std::string &gameElfPath);

    // Register a new mapped library (e.g. "openal32.dll" representing "libopenal.so")
    void RegisterLibrary(const std::string &linuxName, const std::string &windowsName);

    // Load a library mapped by RegisterLibrary
    void LoadNeededLibrary(const std::string &linuxName);
    // dlopen: without RTLD_GLOBAL the library and whatever it newly pulls in
    // get a local symbol scope, which only they resolve against.
    void LoadDlopenedLibrary(const std::string &linuxName, bool global);

    void *GetLibraryHandle(const std::string &linuxName);
    void *ResolveSymbolInModule(void *handle, const std::string &symbolName);

    // Resolve a function/symbol from the loaded chain of libraries or internal bridges.
    // `requester` is the module being relocated; its local scope is searched
    // after the global one. Without one, local scopes are a last resort.
    void *ResolveSymbol(const std::string &symbolName, std::string *outModuleName,
                        const ElfLoader *requester = nullptr);

    // Resolve a symbol by searching ONLY native loaded shared objects (excludes main EXE).
    // Used for R_386_COPY relocations which must copy from the SO, not the EXE's own BSS.
    void *ResolveSymbolInSharedLibs(const std::string &symbolName);

    // Provide the generated VTable for a class/structure
    void *GetVTable(const std::string &className);
    void RegisterVTable(const std::string &className, void *vtablePtr, void **originalSymbolPtr = nullptr);

    // Register a symbol exported by a dynamically loaded Linux ELF
    void RegisterNativeSymbol(const std::string &symbolName, void *symbolPtr,
                              const ElfLoader *owner = nullptr, bool isStatic = false);
    size_t PatchNativeJumpStubs(const std::string &prefix, void *(*resolver)(const char *));

    // Global relocation and initialization passes
    bool ProcessAllRelocations();
    void PatchAllSOs();
    bool RunAllInits();

  private:
    SymbolResolver();
    ~SymbolResolver()
    {
    }

    std::unordered_map<std::string, std::string> m_LibraryMap;
    std::unordered_map<std::string, void *> m_VTables;
    std::unordered_map<std::string, void **> m_OriginalSymbolPtrs;
    std::unordered_map<std::string, void *> m_NativeSymbols;
    // Exports of RTLD_LOCAL dlopens, per scope; static symbols kept apart so
    // they are only ever found by name, never by a relocation.
    std::unordered_map<int, std::unordered_map<std::string, void *>> m_ScopeSymbols;
    std::unordered_map<int, std::unordered_map<std::string, void *>> m_ScopeStatics;
    int m_LoadScope = 0;
    int m_NextScope = 0;
    std::vector<void *> m_LoadedLibraries;        // Stores handles to loaded DLLs
    std::vector<ElfLoader *> m_NativeLoaders;     // Stores loaders for purely native Linux shared objects
    std::vector<std::string> m_LoadedNativeNames; // Tracks already loaded Linux SO file paths
    std::unordered_map<std::string, void *> m_HandlesByName; // Every name a module can be dlopened by
    std::vector<std::pair<uintptr_t, std::string>> m_PendingSOPatches; // Deferred SO patches (base, path)

    // Library search paths
    std::vector<std::string> m_LibrarySearchPaths; // Ordered list of paths to search for libraries
};
#endif

#ifdef __cplusplus
extern "C"
{
#endif
    void *bridgeResolveSymbol(const char *symbolName);
    void bridgeLoadNeededLibrary(const char *filename, int linuxFlags);
    void *bridgeResolveSymbolOptional(const char *symbolName);
    void *bridgeLibraryHandle(const char *filename);
    void *bridgeResolveSymbolInModule(void *handle, const char *symbolName);
    void EnsureLibGccLoaded();
    void *GetLibGccSymbol(const char *name);
#ifdef __cplusplus
}
#endif
