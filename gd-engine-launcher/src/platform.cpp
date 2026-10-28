// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "platform.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <pwd.h>
#include <unistd.h>
#endif
#ifdef __ANDROID__
#include <jni.h>
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace platform {

namespace {

fs::path toPath(const std::string& utf8) { return fs::u8path(utf8); }

#ifdef _WIN32
// A full path with backslashes: LOAD_WITH_ALTERED_SEARCH_PATH looks for the
// library's own DLLs in its folder only when the path uses them.
std::wstring winPath(std::string s) {
    std::replace(s.begin(), s.end(), '/', '\\');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}
std::string narrow(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
#endif

#ifdef __ANDROID__
// GdulActivity's static methods (android/app/src/main/java/.../GdulActivity.java).
struct Java {
    JNIEnv* env = nullptr;
    jclass cls = nullptr;
    Java() {
        env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (env && activity) cls = env->GetObjectClass(activity);
        if (env && activity) env->DeleteLocalRef(activity);
    }
    ~Java() {
        if (cls) env->DeleteLocalRef(cls);
    }
    Java(const Java&) = delete;
    Java& operator=(const Java&) = delete;
    jmethodID method(const char* name, const char* sig) {
        if (!cls) return nullptr;
        jmethodID m = env->GetStaticMethodID(cls, name, sig);
        if (!m) env->ExceptionClear();
        return m;
    }
    std::string string(jstring s) {
        if (!s) return "";
        const char* c = env->GetStringUTFChars(s, nullptr);
        std::string out = c ? c : "";
        if (c) env->ReleaseStringUTFChars(s, c);
        env->DeleteLocalRef(s);
        return out;
    }
    std::string callString(const char* name, const std::string& arg) {
        jmethodID m = method(name, "(Ljava/lang/String;)Ljava/lang/String;");
        if (!m) return "";
        jstring a = env->NewStringUTF(arg.c_str());
        auto r = static_cast<jstring>(env->CallStaticObjectMethod(cls, m, a));
        env->DeleteLocalRef(a);
        if (env->ExceptionCheck()) env->ExceptionClear(), r = nullptr;
        return string(r);
    }
};
#endif

}  // namespace

bool isDocument(const std::string& path) {
#ifdef __ANDROID__
    return path.rfind("content://", 0) == 0;
#else
    (void)path;
    return false;
#endif
}

int openDocument(const std::string& uri, std::string& error) {
#ifdef __ANDROID__
    Java java;
    jmethodID open = java.method("openDocument", "(Ljava/lang/String;)I");
    if (!open) {
        error = "the launcher's Java side is missing (GdulActivity.openDocument)";
        return -1;
    }
    jstring a = java.env->NewStringUTF(uri.c_str());
    int fd = java.env->CallStaticIntMethod(java.cls, open, a);
    java.env->DeleteLocalRef(a);
    if (java.env->ExceptionCheck()) java.env->ExceptionClear(), fd = -1;
    if (fd < 0) {
        if (jmethodID last = java.method("lastError", "()Ljava/lang/String;")) {
            auto s = static_cast<jstring>(java.env->CallStaticObjectMethod(java.cls, last));
            if (java.env->ExceptionCheck()) java.env->ExceptionClear(), s = nullptr;
            error = java.string(s);
        }
        if (error.empty()) error = "Android could not open it";
        return -1;
    }
    // The games seek around in the disc: it must be a real file on the
    // device (a cloud document comes as a pipe).
    struct stat st {};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        error = "it is not a file on this device's storage (a cloud document?): copy it to the device first";
        return -1;
    }
    return fd;
#else
    (void)uri;
    error = "not on Android";
    return -1;
#endif
}

void closeDocument(int fd) {
#ifdef __ANDROID__
    if (fd >= 0) ::close(fd);
#else
    (void)fd;
#endif
}

std::string documentName(const std::string& uri) {
#ifdef __ANDROID__
    if (!isDocument(uri)) return "";
    Java java;
    return java.callString("documentName", uri);
#else
    (void)uri;
    return "";
#endif
}

std::string displayName(const std::string& path) {
    if (isDocument(path)) {
        std::string name = documentName(path);
        if (!name.empty()) return name;
        // "content://.../document/primary%3ADownload%2Fgame.iso" -> "game.iso"
        std::string decoded;
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size() && std::isxdigit(static_cast<unsigned char>(path[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(path[i + 2]))) {
                decoded += char(std::strtol(path.substr(i + 1, 2).c_str(), nullptr, 16));
                i += 2;
            } else {
                decoded += path[i];
            }
        }
        size_t cut = decoded.find_last_of("/:");
        return cut == std::string::npos ? decoded : decoded.substr(cut + 1);
    }
    return fileName(path);
}

std::string baseDir() {
#ifdef __ANDROID__
    return "";  // relative paths: SDL reads them from the APK's assets
#else
    const char* b = SDL_GetBasePath();
    return b ? b : "./";
#endif
}

std::string nativeLibDir() {
#ifdef __ANDROID__
    // Where this library (libmain.so) was loaded from: the APK's native
    // libraries are extracted there (useLegacyPackaging in android/app/build.gradle).
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&nativeLibDir), &info) && info.dli_fname) return parentDir(info.dli_fname);
#endif
    return "";
}

std::string saveDir() {
    char* p = SDL_GetPrefPath("gd-engine", "gdul");
    std::string s = p ? p : baseDir();
    SDL_free(p);
#ifdef __ANDROID__
    // Android's SDL gives everyone the app's files/ folder: the launcher's
    // own is files/gd-engine/gdul/, as the games' are files/gd-engine/<game>/
    // (gd_module_shim). Launchers before 0.1.6 kept config.txt in files/,
    // where gd01 and gd02 wrote theirs too: its lines come along once.
    static const std::string dir = [&] {
        const std::string mine = s + "gd-engine/gdul/";
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(mine.c_str(), &info)) {
            SDL_CreateDirectory(mine.c_str());
            SDL_CopyFile((s + "config.txt").c_str(), (mine + "config.txt").c_str());
        }
        return mine;
    }();
    return dir;
#else
    return s;
#endif
}

std::string userName() {
#ifdef _WIN32
    wchar_t buf[257];
    DWORD n = 257;
    if (GetUserNameW(buf, &n)) return narrow(buf);
    if (const char* u = SDL_getenv("USERNAME")) return u;
#else
    if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_name) return pw->pw_name;
    for (const char* v : {"USER", "LOGNAME"})
        if (const char* u = SDL_getenv(v); u && *u) return u;
#endif
    return "unknown";
}

std::string isoNow() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S%z", &tm);
    std::string s = buf;  // ...-0300: ISO 8601's extended form is -03:00
    if (s.size() >= 5 && (s[s.size() - 5] == '+' || s[s.size() - 5] == '-')) s.insert(s.size() - 2, ":");
    return s;
}

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir.back();
    return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

std::string parentDir(const std::string& path) {
    std::string p = path;
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : p.substr(0, slash + 1);
}

std::string fileName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool isFile(const std::string& path) {
    std::error_code ec;
    if (!path.empty() && fs::is_regular_file(toPath(path), ec)) return true;
#ifdef __ANDROID__
    if (!path.empty() && path[0] != '/')  // an asset
        if (SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb")) {
            SDL_CloseIO(io);
            return true;
        }
#endif
    return false;
}

bool isDir(const std::string& path) {
    std::error_code ec;
    return !path.empty() && fs::is_directory(toPath(path), ec);
}

std::vector<std::string> listFiles(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(toPath(dir), ec))
        if (e.is_regular_file(ec)) out.push_back(e.path().u8string());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> listFiles(const std::string& dir, std::string& error) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(toPath(dir), ec)) {
        error = "not there";
        return out;
    }
    fs::directory_iterator it(toPath(dir), ec);
    if (ec) {
        error = "cannot be read (" + ec.message() + ")";
        return out;
    }
    for (; it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec)) out.push_back(it->path().u8string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string appBundle() {
#ifdef __APPLE__
    // SDL_GetBasePath() is <name>.app/Contents/Resources/ in a bundle.
    for (std::string dir = baseDir(); dir.size() > 1; dir = parentDir(dir)) {
        std::string d = dir;
        while (d.size() > 1 && d.back() == '/') d.pop_back();
        if (d.size() > 4 && d.compare(d.size() - 4, 4, ".app") == 0) return d;
        if (parentDir(dir) == dir) break;
    }
#endif
    return "";
}

bool isTranslocated(const std::string& path) { return path.find("/AppTranslocation/") != std::string::npos; }

std::string originalAppBundle(const std::string& app) {
#ifdef __APPLE__
    // CoreFoundation and Security through dlsym: no framework to link, and
    // SecTranslocate* has no public header.
    void* cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LAZY | RTLD_LOCAL);
    void* sec = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY | RTLD_LOCAL);
    std::string out;
    if (cf && sec) {
        using CreateUrl = const void* (*)(const void*, const unsigned char*, long, unsigned char);
        using UrlPath = unsigned char (*)(const void*, unsigned char, unsigned char*, long);
        using Release = void (*)(const void*);
        using IsTranslocatedUrl = unsigned char (*)(const void*, bool*, void**);
        using OriginalUrl = const void* (*)(const void*, void**);
        auto createUrl = reinterpret_cast<CreateUrl>(dlsym(cf, "CFURLCreateFromFileSystemRepresentation"));
        auto urlPath = reinterpret_cast<UrlPath>(dlsym(cf, "CFURLGetFileSystemRepresentation"));
        auto release = reinterpret_cast<Release>(dlsym(cf, "CFRelease"));
        auto isTrans = reinterpret_cast<IsTranslocatedUrl>(dlsym(sec, "SecTranslocateIsTranslocatedURL"));
        auto original = reinterpret_cast<OriginalUrl>(dlsym(sec, "SecTranslocateCreateOriginalPathForURL"));
        if (createUrl && urlPath && release && isTrans && original) {
            if (const void* url = createUrl(nullptr, reinterpret_cast<const unsigned char*>(app.c_str()), long(app.size()), 1)) {
                bool translocated = false;
                if (isTrans(url, &translocated, nullptr) && translocated)
                    if (const void* orig = original(url, nullptr)) {
                        unsigned char buf[4096] = {};
                        if (urlPath(orig, 1, buf, long(sizeof buf))) out = reinterpret_cast<const char*>(buf);
                        release(orig);
                    }
                release(url);
            }
        }
    }
    if (sec) dlclose(sec);
    if (cf) dlclose(cf);
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
#else
    (void)app;
    return "";
#endif
}

bool isSteamDeck() {
#if defined(__ANDROID__)
    return false;
#else
#if defined(GDUL_STEAM_DECK)
    return true;
#else
    if (const char* v = SDL_getenv("SteamDeck"); v && std::string(v) == "1") return true;
#if defined(__linux__)
    std::ifstream f("/sys/devices/virtual/dmi/id/board_name");
    std::string board;
    if (f && std::getline(f, board)) return board == "Jupiter" || board == "Galileo";
#endif
    return false;
#endif
#endif
}

void* openLibrary(const std::string& path, std::string& error) {
#ifdef _WIN32
    // The module's own folder first for its DLLs (FFmpeg), then the launcher's (SDL3).
    HMODULE h = LoadLibraryExW(winPath(path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) error = "LoadLibrary failed (error " + std::to_string(GetLastError()) + ")";
    return reinterpret_cast<void*>(h);
#else
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) error = dlerror();
    return h;
#endif
}

void* librarySymbol(void* lib, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

bool closeLibrary(void* lib, const std::string& path) {
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(lib));
    HMODULE still = nullptr;
    return !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, winPath(path).c_str(), &still) || !still;
#else
    dlclose(lib);
    void* again = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (!again) return true;
    dlclose(again);
    return false;
#endif
}

}  // namespace platform
