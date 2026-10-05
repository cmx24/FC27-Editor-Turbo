// FC 27 LE Turbo GUI - HTTPS GET with WinHTTP. winhttp.dll is loaded the first time a request is made (not at DLL load), so
// Turbo.dll adds nothing to the game's start-up imports.
// An offline setup often blocks FC27.exe's outbound traffic in Windows Firewall (a program rule): WinHTTP then cannot connect
// from inside the game, so the same GET is retried with Windows' own curl.exe (System32), a separate program the rule does
// not cover. Only the one URL asked for, only plain address characters, with the same size and time limits.
#include "core/http.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace turbo {

namespace {

struct Api {
    decltype(&WinHttpOpen) open = nullptr;
    decltype(&WinHttpConnect) connect = nullptr;
    decltype(&WinHttpOpenRequest) open_request = nullptr;
    decltype(&WinHttpSendRequest) send = nullptr;
    decltype(&WinHttpReceiveResponse) receive = nullptr;
    decltype(&WinHttpQueryHeaders) query_headers = nullptr;
    decltype(&WinHttpReadData) read = nullptr;
    decltype(&WinHttpCloseHandle) close = nullptr;
    decltype(&WinHttpCrackUrl) crack = nullptr;
    decltype(&WinHttpSetTimeouts) timeouts = nullptr;
    bool ok = false;
};

template <typename F>
F fn(HMODULE m, const char* name) {
    return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name)));
}

const Api& api() {
    static Api a = [] {
        Api r;
        HMODULE m = LoadLibraryExW(L"winhttp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!m) return r;
        r.open = fn<decltype(r.open)>(m, "WinHttpOpen");
        r.connect = fn<decltype(r.connect)>(m, "WinHttpConnect");
        r.open_request = fn<decltype(r.open_request)>(m, "WinHttpOpenRequest");
        r.send = fn<decltype(r.send)>(m, "WinHttpSendRequest");
        r.receive = fn<decltype(r.receive)>(m, "WinHttpReceiveResponse");
        r.query_headers = fn<decltype(r.query_headers)>(m, "WinHttpQueryHeaders");
        r.read = fn<decltype(r.read)>(m, "WinHttpReadData");
        r.close = fn<decltype(r.close)>(m, "WinHttpCloseHandle");
        r.crack = fn<decltype(r.crack)>(m, "WinHttpCrackUrl");
        r.timeouts = fn<decltype(r.timeouts)>(m, "WinHttpSetTimeouts");
        r.ok = r.open && r.connect && r.open_request && r.send && r.receive && r.query_headers && r.read && r.close && r.crack &&
               r.timeouts;
        return r;
    }();
    return a;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

struct Handle {
    const Api& a;
    HINTERNET h;
    ~Handle() { if (h) a.close(h); }
};

// https:// and nothing a command line could read as anything but the address
bool plain_url(const std::string& url) {
    if (url.compare(0, 8, "https://") != 0 || url.size() > 1024) return false;
    for (unsigned char c : url)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == '/' || c == '.' || c == '_' ||
              c == '-'))
            return false;
    return true;
}

struct OwnedHandle {
    HANDLE h = nullptr;
    ~OwnedHandle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};

// The GET through %SystemRoot%\System32\curl.exe: the body goes to a temp file, the HTTP status to stdout (a pipe)
HttpResult curl_get(const std::string& url, size_t max_bytes) {
    HttpResult res;
    if (!plain_url(url)) { res.error = "address not fetched with curl.exe"; return res; }
    wchar_t sys[MAX_PATH] = {}, tmp_dir[MAX_PATH] = {}, tmp[MAX_PATH] = {};
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (!n || n >= MAX_PATH) { res.error = "no system folder"; return res; }
    const std::wstring curl = std::wstring(sys) + L"\\curl.exe";
    if (GetFileAttributesW(curl.c_str()) == INVALID_FILE_ATTRIBUTES) { res.error = "curl.exe is not in the system folder"; return res; }
    DWORD tn = GetTempPathW(MAX_PATH, tmp_dir);
    if (!tn || tn >= MAX_PATH || !GetTempFileNameW(tmp_dir, L"tbo", 0, tmp)) { res.error = "no temp file"; return res; }
    struct Remove { const wchar_t* p; ~Remove() { DeleteFileW(p); } } remove_tmp{tmp};

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    OwnedHandle rd, wr;
    if (!CreatePipe(&rd.h, &wr.h, &sa, 0)) { res.error = "no pipe"; return res; }
    SetHandleInformation(rd.h, HANDLE_FLAG_INHERIT, 0);
    OwnedHandle nul;
    nul.h = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    // -s quiet, -L follow redirects, --proto =https only, -o the body, -w the status code on stdout
    std::wstring cmd = L"\"" + curl + L"\" -s -L --proto =https --proto-redir =https --connect-timeout 8 --max-time 15 --max-filesize " +
                       std::to_wstring(max_bytes) + L" -o \"" + tmp + L"\" -w \"%{http_code}\" \"" + widen(url) + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nullptr;
    si.hStdOutput = wr.h;
    si.hStdError = nul.h != INVALID_HANDLE_VALUE ? nul.h : nullptr;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(curl.c_str(), &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        res.error = "curl.exe did not start (error " + std::to_string(GetLastError()) + ")";
        return res;
    }
    CloseHandle(wr.h);  // our copy: ReadFile ends when curl exits
    wr.h = nullptr;
    OwnedHandle proc, thread;
    proc.h = pi.hProcess;
    thread.h = pi.hThread;
    if (WaitForSingleObject(pi.hProcess, 20000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        res.error = "curl.exe timed out";
        return res;
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    char out[32] = {};
    DWORD got = 0;
    ReadFile(rd.h, out, sizeof(out) - 1, &got, nullptr);
    const int status = std::atoi(out);
    if (code != 0) {
        res.error = "curl.exe exit code " + std::to_string(code) + (code == 63 ? " (answer too large)" : "");
        return res;
    }
    res.status = status;
    if (status != 200) return res;
    std::ifstream in(tmp, std::ios::binary);
    res.body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (res.body.size() > max_bytes) { res.status = 0; res.error = "answer too large"; res.body.clear(); }
    return res;
}

HttpResult winhttp_get(const std::string& url, size_t max_bytes);

}  // namespace

// WinHTTP first; when it gets no answer at all (no connection: e.g. a firewall rule on FC27.exe), curl.exe
HttpResult http_get(const std::string& url, size_t max_bytes) {
    HttpResult r = winhttp_get(url, max_bytes);
    if (r.status != 0 || r.error == "only https:// addresses are fetched" || r.error == "answer too large") return r;
    HttpResult c = curl_get(url, max_bytes);
    if (c.status != 0) return c;
    r.error += "; curl.exe: " + c.error;
    return r;
}

namespace {

HttpResult winhttp_get(const std::string& url, size_t max_bytes) {
    HttpResult res;
    if (url.compare(0, 8, "https://") != 0) { res.error = "only https:// addresses are fetched"; return res; }
    const Api& a = api();
    if (!a.ok) { res.error = "winhttp.dll is not available"; return res; }
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    if (!a.crack(wurl.c_str(), 0, 0, &uc)) { res.error = "bad address"; return res; }
    Handle session{a, a.open(L"FC27-LE-Turbo (player import)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0)};
    if (!session.h) { res.error = "could not open an internet session"; return res; }
    a.timeouts(session.h, 8000, 8000, 15000, 15000);
    Handle conn{a, a.connect(session.h, host, uc.nPort, 0)};
    if (!conn.h) { res.error = "could not connect"; return res; }
    Handle req{a, a.open_request(conn.h, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!req.h) { res.error = "could not create the request"; return res; }
    if (!a.send(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !a.receive(req.h, nullptr)) {
        res.error = "no answer (error " + std::to_string(GetLastError()) + ")";
        return res;
    }
    DWORD status = 0, size = sizeof(status);
    if (!a.query_headers(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                         WINHTTP_NO_HEADER_INDEX)) {
        res.error = "no status";
        return res;
    }
    res.status = static_cast<int>(status);
    if (status != 200) return res;
    for (;;) {
        uint8_t buf[16384];
        DWORD got = 0;
        if (!a.read(req.h, buf, sizeof(buf), &got)) { res.status = 0; res.error = "read failed"; res.body.clear(); return res; }
        if (got == 0) break;
        if (res.body.size() + got > max_bytes) { res.status = 0; res.error = "answer too large"; res.body.clear(); return res; }
        res.body.insert(res.body.end(), buf, buf + got);
    }
    return res;
}

}  // namespace

}  // namespace turbo
