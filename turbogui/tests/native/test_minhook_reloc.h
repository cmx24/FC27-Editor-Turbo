// Native tests: the MinHook relocation range check (Turbo 2.15, hooks/minhook-reloc). minhook_reloc_test::run().
// Why: upstream CreateTrampolineFunction copies a target's first instruction into a stub and rewrites a RIP-relative displacement with
// no range check. When the target already begins with another hook's `jmp [rip+x]` (Live Editor hooks ExecuteCommandLists) and our stub
// sits > 2 GB away (next to oo2core, because the attrib patch hooked Oodle at load), the 32-bit displacement truncates and the stub jumps
// through an unmapped pointer slot - the P7 crash (AV read of 0x7FFEAF4D0000). The fix: range-check the relocation; a far copied
// `jmp [rip+x]` becomes a register/flag-preserving absolute stub that reads the slot at run time; any other out-of-range RIP-relative
// operand makes the hook refuse (overlay stays off).
// Proven here (Windows): the pure helpers; CreateTrampolineFunction's three cases (far jmp -> absolute stub; in-range jmp -> normal
// reloc; far non-jump -> refuse); and TWO CHILD PROCESSES that reproduce P7's layout - one runs a truncated `jmp [rip]` to an unmapped
// slot and dies with an access violation (the bug), one runs the fix's stub for a mapped slot and reaches the destination (the fix).
#pragma once
#ifdef _WIN32
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "minhook_diag.h"
extern "C" {
#include "../../third_party/minhook/src/trampoline.h"
}

namespace minhook_reloc_test {

// ---- child side: the destination the fix's stub must reach, and the two child entry points ----
static int __attribute__((noinline)) mh_dest() {
    volatile int v = 0xC0DE;
    return v;
}

static LONG WINAPI child_av_filter(EXCEPTION_POINTERS* ep) {
    // The bug child faults here; exit with a marker so the parent can tell "faulted" from "ran".
    if (ep && ep->ExceptionRecord && ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) ExitProcess(0x00C0FFEEu);
    ExitProcess(0x0000DEADu);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void* exec_page(const uint8_t* bytes, size_t n) {
    void* p = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!p) ExitProcess(0x0000AAAAu);
    std::memcpy(p, bytes, n);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return p;
}

// a free (unmapped) address, so a jmp through a slot there faults on the read
static uint64_t unmapped_addr() {
    uint64_t a = 0x0000500000000000ull;
    MEMORY_BASIC_INFORMATION mbi;
    for (int i = 0; i < 4096; ++i, a += 0x100000ull)
        if (VirtualQuery((LPCVOID)(uintptr_t)a, &mbi, sizeof(mbi)) == sizeof(mbi) && mbi.State == MEM_FREE) return a;
    return a;
}

static int child_main(const char* mode) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(child_av_filter);
    if (std::strcmp(mode, "fixed") == 0) {
        // a mapped pointer slot holding &mh_dest; the fix's stub reads it and jumps there
        void* slotPage = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!slotPage) return 0x0000AAAA;
        *(void**)slotPage = (void*)&mh_dest;
        uint8_t stub[19];
        UINT k = MH_EmitFarJmpStub(stub, slotPage);
        void* code = exec_page(stub, k);
        int r = ((int (*)())code)();  // must reach mh_dest()
        ExitProcess((UINT)r);         // 0xC0DE on success
    } else {  // "bug": a truncated jmp [rip+disp] whose slot is unmapped (what upstream would have produced); the read faults
        uint64_t slot = unmapped_addr();
        uint8_t b[16] = {0};
        b[0] = 0xFF;
        b[1] = 0x25;  // jmp [rip+disp32]; disp so rip(after 6 bytes)+disp = slot. We place the code on its own page:
        // The page base is unknown until allocated, so build disp after allocation.
        void* page = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!page) return 0x0000AAAA;
        uint64_t rip_after = (uint64_t)(uintptr_t)page + 6;
        int32_t disp = (int32_t)((int64_t)slot - (int64_t)rip_after);  // within 2 GB of the page (slot chosen ~far but < 2 GB? not needed)
        // If the chosen unmapped slot is > 2 GB away, fall back to a slot just past the instruction on this same (committed) page's
        // guard: use an address 1 page past the committed region, which is unmapped.
        if ((int64_t)slot - (int64_t)rip_after > 0x7FFFFFFFll || (int64_t)slot - (int64_t)rip_after < -0x80000000ll) {
            uint64_t near_unmapped = (uint64_t)(uintptr_t)page + 0x1000;  // next page, not committed
            disp = (int32_t)((int64_t)near_unmapped - (int64_t)rip_after);
        }
        std::memcpy(b + 2, &disp, 4);
        std::memcpy(page, b, 6);
        FlushInstructionCache(GetCurrentProcess(), page, 6);
        ((void (*)())page)();   // reads the unmapped slot -> access violation
        ExitProcess(0x0u);      // reached only if no fault (unexpected)
    }
    return 0;
}

// ---- parent side ----
static DWORD run_child(const char* mode) {
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t wmode[16] = {0};
    for (int i = 0; mode[i] && i < 15; ++i) wmode[i] = (wchar_t)mode[i];
    std::wstring cmd = std::wstring(L"\"") + exe + L"\" --minhook-child " + wmode;
    STARTUPINFOW si;
    std::memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    std::memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return 0xFFFFFFFFu;
    DWORD code = 0xFFFFFFFFu;
    if (WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

// Allocate at or above `pref`, scanning up, so two regions can be forced > 2 GB apart
static LPVOID valloc_from(uint64_t pref, SIZE_T n, DWORD prot) {
    for (uint64_t a = pref; a < 0x00007F0000000000ull; a += 0x0000000040000000ull) {  // 1 GB steps
        LPVOID p = VirtualAlloc((LPVOID)(uintptr_t)a, n, MEM_COMMIT | MEM_RESERVE, prot);
        if (p) return p;
    }
    return nullptr;
}

static void run() {
    run_case("minhook reloc: MH_DispOutOfRange and MH_EmitFarJmpStub (the pure helpers)", [] {
        CHECK(!MH_DispOutOfRange(0x7FFFFFFFLL) && !MH_DispOutOfRange(-0x80000000LL), "+-2^31 boundary is in range");
        CHECK(MH_DispOutOfRange(0x80000000LL) && MH_DispOutOfRange(-0x80000001LL), "just past +-2^31 is out of range");
        CHECK(MH_DispOutOfRange(-0xBCE80F86LL), "the P7 distance -0xBCE80F86 (-2.95 GiB) is out of range");
        uint8_t s[19];
        void* abs = (void*)0x00007FFDAF4D0000ull;  // P7's slot
        UINT k = MH_EmitFarJmpStub(s, abs);
        const uint8_t want_head[3] = {0x50, 0x48, 0xB8};
        const uint8_t want_tail[8] = {0x48, 0x8B, 0x00, 0x48, 0x87, 0x04, 0x24, 0xC3};
        void* embedded = nullptr;
        std::memcpy(&embedded, s + 3, 8);
        CHECK(k == 19 && std::memcmp(s, want_head, 3) == 0 && embedded == abs && std::memcmp(s + 11, want_tail, 8) == 0,
              "19-byte stub: push rax; mov rax,abs; mov rax,[rax]; xchg [rsp],rax; ret");
    });
    run_case("minhook reloc: a far `jmp [rip+x]` becomes the absolute stub; an in-range one relocates normally", [] {
        // target far below, trampoline far above -> the slot (within 2 GB of the target) is > 2 GB from the trampoline
        LPVOID tgt = valloc_from(0x0000100000000000ull, 0x1000, PAGE_EXECUTE_READWRITE);
        LPVOID trm = valloc_from(0x0000400000000000ull, 0x1000, PAGE_EXECUTE_READWRITE);
        CHECK(tgt && trm, "two far-apart pages");
        if (!tgt || !trm) return;
        uint64_t slot = (uint64_t)(uintptr_t)tgt + 0x20000000ull;  // +512 MB: within 2 GB of the target
        uint8_t ins[6] = {0xFF, 0x25, 0, 0, 0, 0};
        int32_t disp = (int32_t)((int64_t)slot - (int64_t)((uint64_t)(uintptr_t)tgt + 6));
        std::memcpy(ins + 2, &disp, 4);
        std::memcpy(tgt, ins, 6);
        TRAMPOLINE ct;
        std::memset(&ct, 0, sizeof(ct));
        ct.pTarget = tgt;
        ct.pDetour = (LPVOID)&mh_dest;
        ct.pTrampoline = trm;
        BOOL ok = CreateTrampolineFunction(&ct);
        const MH_RELOC_DIAG* d = MH_LastReloc();
        uint8_t want[19];
        MH_EmitFarJmpStub(want, (void*)slot);
        CHECK(ok && d->outOfRangeJumpHandled && !d->refusedOutOfRange && std::memcmp(trm, want, 19) == 0,
              "far jmp: the trampoline is the absolute stub for the slot");
        CHECK(MH_DispOutOfRange(d->lastDistance), "the recorded distance is out of range");
        // in-range: target and trampoline adjacent (a normal hook), slot near the target
        LPVOID tgt2 = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        LPVOID trm2 = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        CHECK(tgt2 && trm2, "two near pages");
        if (!tgt2 || !trm2) return;
        uint64_t slot2 = (uint64_t)(uintptr_t)tgt2 + 0x100;
        int32_t disp2 = (int32_t)((int64_t)slot2 - (int64_t)((uint64_t)(uintptr_t)tgt2 + 6));
        uint8_t ins2[6] = {0xFF, 0x25, 0, 0, 0, 0};
        std::memcpy(ins2 + 2, &disp2, 4);
        std::memcpy(tgt2, ins2, 6);
        TRAMPOLINE ct2;
        std::memset(&ct2, 0, sizeof(ct2));
        ct2.pTarget = tgt2;
        ct2.pDetour = (LPVOID)&mh_dest;
        ct2.pTrampoline = trm2;
        BOOL ok2 = CreateTrampolineFunction(&ct2);
        const MH_RELOC_DIAG* d2 = MH_LastReloc();
        CHECK(ok2 && !d2->outOfRangeJumpHandled && ((uint8_t*)trm2)[0] == 0xFF && ((uint8_t*)trm2)[1] == 0x25 && !MH_DispOutOfRange(d2->lastDistance),
              "in-range jmp: the trampoline keeps FF 25 with a fitting displacement (unchanged behaviour)");
        VirtualFree(tgt, 0, MEM_RELEASE);
        VirtualFree(trm, 0, MEM_RELEASE);
        VirtualFree(tgt2, 0, MEM_RELEASE);
        VirtualFree(trm2, 0, MEM_RELEASE);
    });
    run_case("minhook reloc: a far non-jump RIP-relative operand makes the hook refuse", [] {
        LPVOID tgt = valloc_from(0x0000110000000000ull, 0x1000, PAGE_EXECUTE_READWRITE);
        LPVOID trm = valloc_from(0x0000410000000000ull, 0x1000, PAGE_EXECUTE_READWRITE);
        CHECK(tgt && trm, "two far-apart pages");
        if (!tgt || !trm) return;
        // 48 8B 05 <disp>  mov rax,[rip+disp]  then 90 90 (pad so the trampoline needs 5+ bytes)
        uint64_t slot = (uint64_t)(uintptr_t)tgt + 0x20000000ull;
        int32_t disp = (int32_t)((int64_t)slot - (int64_t)((uint64_t)(uintptr_t)tgt + 7));
        uint8_t ins[9] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x90, 0x90};
        std::memcpy(ins + 3, &disp, 4);
        std::memcpy(tgt, ins, 9);
        TRAMPOLINE ct;
        std::memset(&ct, 0, sizeof(ct));
        ct.pTarget = tgt;
        ct.pDetour = (LPVOID)&mh_dest;
        ct.pTrampoline = trm;
        BOOL ok = CreateTrampolineFunction(&ct);
        const MH_RELOC_DIAG* d = MH_LastReloc();
        CHECK(!ok && d->refusedOutOfRange && !d->outOfRangeJumpHandled, "far mov rax,[rip+x]: CreateTrampolineFunction refuses");
        VirtualFree(tgt, 0, MEM_RELEASE);
        VirtualFree(trm, 0, MEM_RELEASE);
    });
    run_case("minhook reloc: child processes reproduce P7's layout (bug dies, fix reaches the destination)", [] {
        DWORD fixed = run_child("fixed");
        CHECK(fixed == 0xC0DEu, "fix: the absolute stub for a mapped slot reaches the destination (exit 0xC0DE, got 0x" + [&] {
            char b[16];
            std::snprintf(b, sizeof(b), "%X", (unsigned)fixed);
            return std::string(b);
        }() + ")");
        DWORD bug = run_child("bug");
        CHECK(bug == 0x00C0FFEEu, "bug: a truncated jmp through an unmapped slot faults (the AV filter exits 0x00C0FFEE, got 0x" + [&] {
            char b[16];
            std::snprintf(b, sizeof(b), "%X", (unsigned)bug);
            return std::string(b);
        }() + ")");
    });
}

}  // namespace minhook_reloc_test
#else
namespace minhook_reloc_test {
static void run() {}
}  // namespace minhook_reloc_test
#endif
