/*
 *  MinHook diagnostics (Turbo 2.15, hooks/minhook-reloc): what CreateTrampolineFunction did with the most recent target, so the host
 *  (turbogui/src/win/overlay_dx12.cpp) can log the first bytes, the trampoline address and any RIP-relative relocation - and, in
 *  particular, whether an out-of-range copied `jmp [rip+x]` was turned into the register-preserving absolute form or a hook was refused.
 *  This is a Turbo addition, not part of upstream MinHook. It is a single most-recent record, read right after each MH_CreateHook, which
 *  is safe because MinHook creates hooks serially under its own lock.
 */
#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _MH_RELOC_DIAG {
    LPVOID    target;                // the hooked function
    LPVOID    trampoline;            // the stub MinHook placed
    UINT8     firstBytes[16];        // the target's first bytes, for the log
    UINT      nFirstBytes;
    UINT      relocCount;            // RIP-relative operands relocated for this target
    LONGLONG  lastDistance;          // the last relocation's new displacement (signed; |x| >= 2^31 = out of range)
    LPVOID    lastAbsTarget;         // the address that operand points at (the pointer slot, for a jmp [rip+x])
    int       outOfRangeJumpHandled; // a far `jmp [rip+x]` was emitted as the absolute read-and-jump stub
    int       refusedOutOfRange;     // a different out-of-range RIP-relative operand made CreateTrampolineFunction refuse the hook
} MH_RELOC_DIAG;

// The most recent CreateTrampolineFunction result (overwritten each call; valid after MH_CreateHook returns MH_OK or an error)
const MH_RELOC_DIAG* MH_LastReloc(void);

// Turbo 2.15 pure helpers (also used by the native tests). MH_EmitFarJmpStub writes the 19-byte register- and flag-preserving
// "jmp *[absTarget]" stub (push rax; mov rax,absTarget; mov rax,[rax]; xchg [rsp],rax; ret) and returns its length.
// MH_DispOutOfRange is true when a 32-bit relative displacement cannot hold d.
UINT MH_EmitFarJmpStub(UINT8* out, void* absTarget);
BOOL MH_DispOutOfRange(LONGLONG d);

#ifdef __cplusplus
}
#endif
