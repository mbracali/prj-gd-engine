// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "shortcut.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "platform.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <objbase.h>
#include <shlobj.h>
#else
#include <unistd.h>  // getpid, setenv (self-test)
#endif

// The launcher's app icon, embedded at build time (CMakeLists gdul_embed).
// On Linux/macOS it is written to disk so a shortcut can point its Icon= at
// it; on Windows the launcher's own .exe already carries this icon.
extern const unsigned char kGdulIconPng[];
extern const std::size_t kGdulIconPngSize;

namespace fs = std::filesystem;

namespace shortcut {

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string homeDir() {
#if defined(_WIN32)
    std::string h = env("USERPROFILE");
    if (h.empty()) h = env("HOMEDRIVE") + env("HOMEPATH");
    return h;
#else
    return env("HOME");
#endif
}

// A file name safe on every system: only letters, digits, space and - _ ( ) .
std::string safeFileName(const std::string& name) {
    std::string out;
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' ||
                        c == '-' || c == '_' || c == '(' || c == ')' || c == '.';
        out += ok ? c : '_';
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? std::string("gd-engine-game") : out;
}

bool writeFile(const std::string& path, const std::string& body, bool executable, std::string& error) {
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot write " + path;
        return false;
    }
    f.write(body.data(), std::streamsize(body.size()));
    f.close();
    if (!f) {
        error = "cannot write " + path;
        return false;
    }
    if (executable) {
        std::error_code ec;
        fs::permissions(fs::u8path(path),
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, ec);
    }
    return true;
}

// The gd-engine icon (the embedded PNG) written once to the save folder, so a
// shortcut's Icon= has a real file to point at. "" on failure.
std::string writeGdeIcon() {
    const std::string dir = platform::saveDir();
    if (dir.empty()) return "";
    const std::string path = platform::join(dir, "gdul_icon.png");
    std::error_code ec;
    if (fs::exists(fs::u8path(path), ec) && fs::file_size(fs::u8path(path), ec) == kGdulIconPngSize) return path;
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return "";
    f.write(reinterpret_cast<const char*>(kGdulIconPng), std::streamsize(kGdulIconPngSize));
    f.close();
    return f ? path : std::string();
}

#if !defined(_WIN32)
std::string dataHome() {
    std::string d = env("XDG_DATA_HOME");  // SDL_GetPrefPath uses the same base
    if (d.empty()) {
        const std::string h = homeDir();
        if (h.empty()) return "";
        d = platform::join(h, ".local/share");
    }
    return d;
}

// The user's Desktop folder (XDG user-dirs, else ~/Desktop), or "" if none.
std::string desktopDir() {
    const std::string home = homeDir();
    // ~/.config/user-dirs.dirs: XDG_DESKTOP_DIR="$HOME/Área de Trabalho" etc.
    std::string cfg = env("XDG_CONFIG_HOME");
    if (cfg.empty() && !home.empty()) cfg = platform::join(home, ".config");
    if (!cfg.empty()) {
        std::ifstream f(fs::u8path(platform::join(cfg, "user-dirs.dirs")));
        std::string line;
        while (std::getline(f, line)) {
            const std::string key = "XDG_DESKTOP_DIR=";
            size_t at = line.find(key);
            if (at == std::string::npos) continue;
            std::string v = line.substr(at + key.size());
            if (!v.empty() && v.front() == '"') v = v.substr(1);
            if (!v.empty() && v.back() == '"') v.pop_back();
            const std::string hv = "$HOME/";
            if (v.rfind(hv, 0) == 0) v = platform::join(home, v.substr(hv.size()));
            else if (v == "$HOME") v = home;
            std::error_code ec;
            if (!v.empty() && fs::is_directory(fs::u8path(v), ec)) return v;
        }
    }
    if (!home.empty()) {
        const std::string d = platform::join(home, "Desktop");
        std::error_code ec;
        if (fs::is_directory(fs::u8path(d), ec)) return d;
    }
    return "";
}
#endif  // !_WIN32

// ---- Linux: Desktop Entry (.desktop) ----

// A reserved character in a Desktop Entry Exec value must be quoted.
bool needsQuote(const std::string& s) {
    if (s.empty()) return true;
    for (char c : s) {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                          c == '_' || c == '.' || c == '/' || c == ':' || c == '=' || c == '+' || c == ',' || c == '@';
        if (!safe) return true;
    }
    return false;
}

// Quote one Exec token: wrap in double quotes, escaping " ` $ and \ (spec 1.5).
std::string execQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

#if defined(__APPLE__)
// ---- POSIX shell quoting (macOS .command) ----
std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}
#endif  // __APPLE__

#if defined(_WIN32)
std::wstring toW(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

// One argument quoted for a Windows command line (CommandLineToArgvW rules).
std::wstring winQuote(const std::string& arg8) {
    std::wstring a = toW(arg8);
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    for (size_t i = 0;; ++i) {
        unsigned backslashes = 0;
        while (i < a.size() && a[i] == L'\\') ++i, ++backslashes;
        if (i == a.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (a[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
        } else {
            out.append(backslashes, L'\\');
            out += a[i];
        }
    }
    out += L'"';
    return out;
}

std::string knownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path) {
        int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        out.assign(size_t(n > 0 ? n - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, path, -1, out.data(), n, nullptr, nullptr);
    }
    if (path) CoTaskMemFree(path);
    return out;
}

bool createLnk(const std::wstring& lnkPath, const std::wstring& target, const std::wstring& args,
               const std::wstring& icon, const std::wstring& workdir, std::string& error) {
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool didInit = SUCCEEDED(hrInit);
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                  reinterpret_cast<void**>(&link));
    if (FAILED(hr) || !link) {
        error = "Windows could not create the shortcut object";
        if (didInit) CoUninitialize();
        return false;
    }
    link->SetPath(target.c_str());
    link->SetArguments(args.c_str());
    if (!workdir.empty()) link->SetWorkingDirectory(workdir.c_str());
    if (!icon.empty()) link->SetIconLocation(icon.c_str(), 0);  // the .exe's own icon
    link->SetDescription(L"Guitar Dream Engine instant icon");
    IPersistFile* file = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    bool ok = false;
    if (SUCCEEDED(hr) && file) {
        ok = SUCCEEDED(file->Save(lnkPath.c_str(), TRUE));
        if (!ok) error = "Windows could not save the shortcut file";
        file->Release();
    } else {
        error = "Windows could not save the shortcut file";
    }
    link->Release();
    if (didInit) CoUninitialize();
    return ok;
}
#endif  // _WIN32

}  // namespace

std::string shortcutName(const std::string& gameName) { return "(GD-ENGINE) " + gameName; }

std::vector<std::string> launchArgs(const std::string& gameId, const std::string& isoPath) {
    return {"--play", gameId, "--iso", isoPath, "--exit-after"};
}

std::string desktopEntry(const std::string& name, const std::string& exec, const std::vector<std::string>& args,
                         const std::string& icon) {
    std::string cmd = execQuote(exec);  // the launcher path (usually has spaces)
    for (const std::string& a : args) cmd += " " + (needsQuote(a) ? execQuote(a) : a);
    std::string one = name;  // a Name= value is one line
    for (char& c : one)
        if (c == '\n' || c == '\r') c = ' ';
    std::string out;
    out += "[Desktop Entry]\n";
    out += "Type=Application\n";
    out += "Version=1.0\n";
    out += "Name=" + one + "\n";
    out += "Comment=Start this game directly with the Guitar Dream Universal Launcher\n";
    out += "Exec=" + cmd + "\n";
    if (!icon.empty()) out += "Icon=" + icon + "\n";
    out += "Terminal=false\n";
    out += "Categories=Game;\n";
    out += "StartupNotify=true\n";
    return out;
}

bool supported() {
#if defined(__ANDROID__)
    return false;
#else
    return !platform::executablePath().empty();
#endif
}

bool createForGame(const std::string& gameName, const std::string& gameId, const std::string& isoPath,
                   std::string& outPath, std::string& error) {
    const std::string exec = platform::executablePath();
    if (exec.empty()) {
        error = "the launcher's own path could not be found";
        return false;
    }
    const std::string name = shortcutName(gameName);
    const std::vector<std::string> args = launchArgs(gameId, isoPath);

#if defined(__ANDROID__)
    (void)outPath;
    error = "instant icons are a desktop feature";
    return false;

#elif defined(_WIN32)
    const std::string file = safeFileName(name) + ".lnk";
    std::string dir = knownFolder(FOLDERID_Desktop);
    if (dir.empty()) dir = knownFolder(FOLDERID_Programs);
    if (dir.empty()) {
        error = "the Desktop folder could not be found";
        return false;
    }
    const std::string path = platform::join(dir, file);
    std::wstring winArgs;
    for (size_t i = 0; i < args.size(); ++i) winArgs += (i ? L" " : L"") + winQuote(args[i]);
    const std::string workdir = platform::parentDir(exec);
    if (!createLnk(toW(path), toW(exec), winArgs, toW(exec), toW(workdir), error)) return false;
    outPath = path;
    return true;

#elif defined(__APPLE__)
    // A double-clickable shell script (Finder runs a .command in Terminal).
    // macOS keeps icons in a resource fork, so the script shows the default
    // icon (the gd-engine icon cannot be attached to a bare script).
    std::string body = "#!/bin/sh\n";
    body += "exec " + shellQuote(exec);
    for (const std::string& a : args) body += " " + shellQuote(a);
    body += "\n";
    const std::string file = safeFileName(name) + ".command";
    std::string dir = desktopDir();
    if (dir.empty()) dir = homeDir();
    if (dir.empty()) {
        error = "the Desktop folder could not be found";
        return false;
    }
    const std::string path = platform::join(dir, file);
    if (!writeFile(path, body, true, error)) return false;
    outPath = path;
    return true;

#else  // Linux and other desktops
    const std::string icon = writeGdeIcon();  // "" falls back to no custom icon
    const std::string body = desktopEntry(name, exec, args, icon);
    const std::string file = safeFileName(name) + ".desktop";

    // The applications menu (reliable, searchable) always; the Desktop too
    // when there is one (the visible "instant icon" #141 asks for).
    std::string appsPath, deskPath;
    const std::string apps = dataHome().empty() ? "" : platform::join(dataHome(), "applications");
    if (!apps.empty()) {
        std::error_code ec;
        fs::create_directories(fs::u8path(apps), ec);
        std::string err;
        if (writeFile(platform::join(apps, file), body, false, err)) appsPath = platform::join(apps, file);
    }
    const std::string desk = desktopDir();
    if (!desk.empty()) {
        std::string err;
        if (writeFile(platform::join(desk, file), body, true, err)) deskPath = platform::join(desk, file);
    }
    if (appsPath.empty() && deskPath.empty()) {
        error = "no writable Desktop or applications folder was found";
        return false;
    }
    outPath = deskPath.empty() ? appsPath : deskPath;  // the Desktop icon when there is one
    return true;
#endif
}

int selfTest() {
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
        if (!ok) ++fails;
    };
    const auto has = [](const std::string& hay, const std::string& needle) {
        return hay.find(needle) != std::string::npos;
    };

    check(shortcutName("Guitar Hero II") == "(GD-ENGINE) Guitar Hero II", "name is '(GD-ENGINE) <game>'");

    const std::vector<std::string> args = launchArgs("gd-game-02", "/discs/My Disc.iso");
    const std::vector<std::string> want = {"--play", "gd-game-02", "--iso", "/discs/My Disc.iso", "--exit-after"};
    check(args == want, "launch args: --play <id> --iso <disc> --exit-after");

    const std::string body = desktopEntry(shortcutName("Guitar Hero II"), "/opt/gd engine/gdul", args, "/opt/icon.png");
    check(has(body, "Name=(GD-ENGINE) Guitar Hero II\n"), "desktop Name line");
    check(has(body, "Exec=\"/opt/gd engine/gdul\""), "desktop Exec quotes the launcher path");
    check(has(body, "--play gd-game-02 "), "desktop Exec has --play <id>");
    check(has(body, "--iso \"/discs/My Disc.iso\""), "desktop Exec quotes the disc path");
    check(has(body, "--exit-after"), "desktop Exec has --exit-after");
    check(has(body, "Icon=/opt/icon.png\n"), "desktop Icon line");
    check(has(body, "Type=Application\n"), "desktop Type=Application");

#if !defined(_WIN32) && !defined(__ANDROID__)
    // A real round trip into a throwaway HOME: write the shortcut, read it back.
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / ("gdul-shortcut-selftest-" + std::to_string(::getpid()));
    fs::create_directories(tmp, ec);
    ::setenv("HOME", tmp.u8string().c_str(), 1);
    ::setenv("XDG_DATA_HOME", (tmp / "data").u8string().c_str(), 1);
    ::setenv("XDG_CONFIG_HOME", (tmp / "config").u8string().c_str(), 1);
    fs::create_directories(tmp / "Desktop", ec);  // so the Desktop copy is written
    std::string out, err;
    const bool made = createForGame("Guitar Hero II", "gd-game-02", "/discs/My Disc.iso", out, err);
    check(made, made ? "createForGame wrote a shortcut" : ("createForGame failed: " + err).c_str());
    if (made) {
        std::printf("  wrote: %s\n", out.c_str());
        std::ifstream f(fs::u8path(out), std::ios::binary);
        std::string got((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        check(has(got, "(GD-ENGINE) Guitar Hero II"), "written file carries the name");
        check(has(got, "--play"), "written file launches with --play");
        check(has(got, "gd-game-02"), "written file names the game id");
        check(has(got, "My Disc.iso"), "written file carries the disc path");
        check(fs::exists(fs::u8path(out), ec), "written file exists on disk");
    }
    if (!std::getenv("GDUL_KEEP_SELFTEST")) fs::remove_all(tmp, ec);  // kept for inspection when set
#endif

    std::printf("%s\n", fails == 0 ? "gdul_shortcut: ok" : "gdul_shortcut: FAILED");
    return fails == 0 ? 0 : 1;
}

}  // namespace shortcut
