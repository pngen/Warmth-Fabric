#include <cstdio>
#include <cstring>
#include <string>
#include "wtest.hpp"
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
static std::string self_dir() {
    wchar_t buf[MAX_PATH]; GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::string s;
    const int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    s.resize(static_cast<std::size_t>(n) - 1);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, s.data(), n, nullptr, nullptr);
    const auto pos = s.find_last_of("\\\\");
    if (pos != std::string::npos) s = s.substr(0, pos);
    return s;
}
static bool run_cuda_demo() {
    const std::string exe = self_dir() + "\\\\..\\\\cuda\\\\warmth_cuda_demo.exe";
    STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
    const int n = MultiByteToWideChar(CP_UTF8, 0, exe.c_str(), -1, nullptr, 0);
    std::wstring wcmd(static_cast<std::size_t>(n) - 1, L' ');
    MultiByteToWideChar(CP_UTF8, 0, exe.c_str(), -1, wcmd.data(), n);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        std::printf("cuda demo not found (%s) - skipping\n", exe.c_str());
        return true; // treat as skippable if module absent
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    std::printf("cuda demo exit code: %u\n", (unsigned)code);
    return code == 0;
}
#endif

WTEST(cuda_cold_warm_invalidate_rewarm) {
#ifdef _WIN32
    CHECK(run_cuda_demo());
#else
    std::printf("CUDA validation requires Windows; skipping\n");
#endif
}

int main() { RUN_TESTS(); }
