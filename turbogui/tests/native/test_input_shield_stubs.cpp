// Native tests (Windows only, 2.16 gui/perf): what win/input_shield.cpp needs from the overlay, for timing its hooks in the
// test process (test_gui_perf.h "input shield"). Turbo never wants the input here: every poll passes through untouched, as
// it does in game while Turbo is hidden.
#include <cstdarg>
#include <cstdio>
#include <string>

#include "win/host.h"

namespace host {

std::string g_shield_test_log;  // the last line install_input_shield logged

void log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_shield_test_log = buf;
}
bool input_block_mouse() { return false; }
bool input_block_keyboard() { return false; }
int input_hidden_vk() { return 0; }
bool input_block_vk(int) { return false; }
void input_raw_wheel(unsigned short) {}

}  // namespace host
