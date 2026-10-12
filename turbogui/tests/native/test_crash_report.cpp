// Native tests: when the test binary dies (an access violation, abort(), an exception nobody catches), say which case was
// running before the process goes. Together with the unbuffered stdout of main() a crash can no longer end a run with no
// output and only an exit code to go on.
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
const std::string* g_case_name = nullptr;

const char* case_name() {
    return g_case_name && !g_case_name->empty() ? g_case_name->c_str() : "(before the first case)";
}

#ifdef _WIN32
LPTOP_LEVEL_EXCEPTION_FILTER g_prev_filter = nullptr;

// 0x20474343 ("GCC" + 0x20) is the SEH code of a g++ C++ exception. When one is thrown where nothing catches it and the
// runtime does not get to std::terminate, the process ends with that code: MSYS shows its low byte, exit code 67, which
// is how a native run once "exited 67 with no output".
constexpr unsigned long kGccCxxException = 0x20474343ul;

LONG WINAPI on_unhandled_exception(EXCEPTION_POINTERS* ep) {
    const unsigned long code = ep && ep->ExceptionRecord ? static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode) : 0ul;
    if (code == kGccCxxException)
        std::fprintf(stdout, "CRASH: a C++ exception nobody caught (code 0x20474343, MSYS exit code 67) in case [%s]\n", case_name());
    else
        std::fprintf(stdout, "CRASH: Windows exception 0x%08lX in case [%s]\n", code, case_name());
    std::fflush(stdout);
    return g_prev_filter ? g_prev_filter(ep) : EXCEPTION_CONTINUE_SEARCH;  // then whatever happened before
}

void on_abort_signal(int sig) {
    std::fprintf(stdout, "CRASH: abort() (signal %d) in case [%s]\n", sig, case_name());
    std::fflush(stdout);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#endif

void on_terminate() {
    std::fprintf(stdout, "CRASH: std::terminate (an exception escaped, or a noexcept function threw) in case [%s]\n", case_name());
    std::fflush(stdout);
#ifdef _WIN32
    std::signal(SIGABRT, SIG_DFL);  // one CRASH line, not two
#endif
    std::abort();
}
}  // namespace

// current_case: the runner's "case being run" string, read only when the process is dying.
// On Linux the sanitizers report crashes themselves; only std::terminate is added there.
void install_crash_report(const std::string* current_case) {
    g_case_name = current_case;
    std::set_terminate(on_terminate);
#ifdef _WIN32
    g_prev_filter = SetUnhandledExceptionFilter(on_unhandled_exception);
    std::signal(SIGABRT, on_abort_signal);
#endif
}
