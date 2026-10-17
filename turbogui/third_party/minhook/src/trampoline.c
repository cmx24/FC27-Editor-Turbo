/*
 *  MinHook - The Minimalistic API Hooking Library for x64/x86
 *  Copyright (C) 2009-2017 Tsuda Kageyu.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 *  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <windows.h>

#if defined(_MSC_VER) && !defined(MINHOOK_DISABLE_INTRINSICS)
    #define ALLOW_INTRINSICS
    #include <intrin.h>
#endif

#ifndef ARRAYSIZE
    #define ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))
#endif

#if defined(_M_X64) || defined(__x86_64__)
    #include "./hde/hde64.h"
    typedef hde64s HDE;
    #define HDE_DISASM(code, hs) hde64_disasm(code, hs)
#else
    #include "./hde/hde32.h"
    typedef hde32s HDE;
    #define HDE_DISASM(code, hs) hde32_disasm(code, hs)
#endif

#include "trampoline.h"
#include "buffer.h"
#include <string.h>
#include "../include/minhook_diag.h"

// Turbo 2.15 (hooks/minhook-reloc): what the last CreateTrampolineFunction did, read by the host for logging (minhook_diag.h).
static MH_RELOC_DIAG g_mhDiag;
const MH_RELOC_DIAG* MH_LastReloc(void) { return &g_mhDiag; }

BOOL MH_DispOutOfRange(LONGLONG d) { return d > 0x7FFFFFFFLL || d < -0x80000000LL; }

UINT MH_EmitFarJmpStub(UINT8* out, void* absTarget)
{
    // 50  push rax | 48 B8 <abs64>  mov rax,absTarget | 48 8B 00  mov rax,[rax] | 48 87 04 24  xchg [rsp],rax | C3  ret
    UINT k = 0;
    out[k++] = 0x50;
    out[k++] = 0x48; out[k++] = 0xB8;
    memcpy(out + k, &absTarget, 8); k += 8;
    out[k++] = 0x48; out[k++] = 0x8B; out[k++] = 0x00;
    out[k++] = 0x48; out[k++] = 0x87; out[k++] = 0x04; out[k++] = 0x24;
    out[k++] = 0xC3;
    return k;
}

// Maximum size of a trampoline function.
#if defined(_M_X64) || defined(__x86_64__)
    #define TRAMPOLINE_MAX_SIZE (MEMORY_SLOT_SIZE - sizeof(JMP_ABS))
#else
    #define TRAMPOLINE_MAX_SIZE MEMORY_SLOT_SIZE
#endif

//-------------------------------------------------------------------------
static BOOL IsCodePadding(LPBYTE pInst, UINT size)
{
    UINT i;

    if (pInst[0] != 0x00 && pInst[0] != 0x90 && pInst[0] != 0xCC)
        return FALSE;

    for (i = 1; i < size; ++i)
    {
        if (pInst[i] != pInst[0])
            return FALSE;
    }
    return TRUE;
}

//-------------------------------------------------------------------------
BOOL CreateTrampolineFunction(PTRAMPOLINE ct)
{
#if defined(_M_X64) || defined(__x86_64__)
    CALL_ABS call = {
        0xFF, 0x15, 0x00000002, // FF15 00000002: CALL [RIP+8]
        0xEB, 0x08,             // EB 08:         JMP +10
        0x0000000000000000ULL   // Absolute destination address
    };
    JMP_ABS jmp = {
        0xFF, 0x25, 0x00000000, // FF25 00000000: JMP [RIP+6]
        0x0000000000000000ULL   // Absolute destination address
    };
    JCC_ABS jcc = {
        0x70, 0x0E,             // 7* 0E:         J** +16
        0xFF, 0x25, 0x00000000, // FF25 00000000: JMP [RIP+6]
        0x0000000000000000ULL   // Absolute destination address
    };
#else
    CALL_REL call = {
        0xE8,                   // E8 xxxxxxxx: CALL +5+xxxxxxxx
        0x00000000              // Relative destination address
    };
    JMP_REL jmp = {
        0xE9,                   // E9 xxxxxxxx: JMP +5+xxxxxxxx
        0x00000000              // Relative destination address
    };
    JCC_REL jcc = {
        0x0F, 0x80,             // 0F8* xxxxxxxx: J** +6+xxxxxxxx
        0x00000000              // Relative destination address
    };
#endif

    UINT8     oldPos   = 0;
    UINT8     newPos   = 0;
    ULONG_PTR jmpDest  = 0;     // Destination address of an internal jump.
    BOOL      finished = FALSE; // Is the function completed?
#if defined(_M_X64) || defined(__x86_64__)
    UINT8     instBuf[32];  // holds a copied instruction, or the 19-byte absolute read-and-jump stub (Turbo 2.15)
#endif

    ct->patchAbove = FALSE;
    ct->nIP        = 0;

    // Turbo 2.15: record what we do with this target (minhook_diag.h), for the host log.
    memset(&g_mhDiag, 0, sizeof(g_mhDiag));
    g_mhDiag.target     = ct->pTarget;
    g_mhDiag.trampoline = ct->pTrampoline;
    g_mhDiag.nFirstBytes = (UINT)sizeof(g_mhDiag.firstBytes);
    memcpy(g_mhDiag.firstBytes, (LPBYTE)ct->pTarget, sizeof(g_mhDiag.firstBytes));

    do
    {
        HDE       hs;
        UINT      copySize;
        LPVOID    pCopySrc;
        ULONG_PTR pOldInst = (ULONG_PTR)ct->pTarget     + oldPos;
        ULONG_PTR pNewInst = (ULONG_PTR)ct->pTrampoline + newPos;

        copySize = HDE_DISASM((LPVOID)pOldInst, &hs);
        if (hs.flags & F_ERROR)
            return FALSE;

        pCopySrc = (LPVOID)pOldInst;
        if (oldPos >= sizeof(JMP_REL))
        {
            // The trampoline function is long enough.
            // Complete the function with the jump to the target function.
#if defined(_M_X64) || defined(__x86_64__)
            jmp.address = pOldInst;
#else
            jmp.operand = (UINT32)(pOldInst - (pNewInst + sizeof(jmp)));
#endif
            pCopySrc = &jmp;
            copySize = sizeof(jmp);

            finished = TRUE;
        }
#if defined(_M_X64) || defined(__x86_64__)
        else if ((hs.modrm & 0xC7) == 0x05)
        {
            // Instructions using RIP relative addressing. (ModR/M = 00???101B)
            PUINT32   pRelAddr;
            // The absolute address the operand points at, and the new displacement from the trampoline copy.
            ULONG_PTR absTarget = pOldInst + hs.len + (INT32)hs.disp.disp32;
            LONGLONG  newDisp   = (LONGLONG)absTarget - (LONGLONG)(pNewInst + hs.len);
            BOOL      isJmp     = (hs.opcode == 0xFF && hs.modrm_reg == 4);

            // Turbo 2.15: record the relocation.
            g_mhDiag.relocCount++;
            g_mhDiag.lastAbsTarget = (LPVOID)absTarget;
            g_mhDiag.lastDistance  = newDisp;

            if (MH_DispOutOfRange(newDisp))
            {
                // Turbo 2.15: the copied operand would need a displacement that does not fit in 32 bits. Upstream MinHook truncates
                // it and the trampoline jumps to a wrong (often unmapped) address - the P7 crash (another hook's `jmp [rip+x]` whose
                // pointer slot is > 2 GB from our trampoline, e.g. Live Editor's slot vs our stub next to oo2core).
                if (isJmp)
                {
                    // A far `jmp [rip+x]`: emit a register- and flag-preserving absolute form that reads the pointer slot at run time
                    // and jumps there, so the previous hook (whoever owns that slot) stays intact.
                    copySize = MH_EmitFarJmpStub(instBuf, (void*)absTarget);   // 19 bytes
                    pCopySrc = instBuf;
                    g_mhDiag.outOfRangeJumpHandled = 1;
                    finished = TRUE;
                }
                else
                {
                    // Any other out-of-range RIP-relative operand: refuse the hook rather than write a wrong address. The caller gets
                    // MH_ERROR_UNSUPPORTED_FUNCTION and the overlay stays off with a log line.
                    g_mhDiag.refusedOutOfRange = 1;
                    return FALSE;
                }
            }
            else
            {
                // In range: the original relocation.
#ifndef ALLOW_INTRINSICS
                memcpy(instBuf, (LPBYTE)pOldInst, copySize);
#else
                __movsb(instBuf, (LPBYTE)pOldInst, copySize);
#endif
                pCopySrc = instBuf;

                // Relative address is stored at (instruction length - immediate value length - 4).
                pRelAddr = (PUINT32)(instBuf + hs.len - ((hs.flags & 0x3C) >> 2) - 4);
                *pRelAddr = (UINT32)newDisp;

                // Complete the function if JMP (FF /4).
                if (isJmp)
                    finished = TRUE;
            }
        }
#endif
        else if (hs.opcode == 0xE8)
        {
            // Direct relative CALL
            ULONG_PTR dest = pOldInst + hs.len + (INT32)hs.imm.imm32;
#if defined(_M_X64) || defined(__x86_64__)
            call.address = dest;
#else
            call.operand = (UINT32)(dest - (pNewInst + sizeof(call)));
#endif
            pCopySrc = &call;
            copySize = sizeof(call);
        }
        else if ((hs.opcode & 0xFD) == 0xE9)
        {
            // Direct relative JMP (EB or E9)
            ULONG_PTR dest = pOldInst + hs.len;

            if (hs.opcode == 0xEB) // isShort jmp
                dest += (INT8)hs.imm.imm8;
            else
                dest += (INT32)hs.imm.imm32;

            // Simply copy an internal jump.
            if ((ULONG_PTR)ct->pTarget <= dest
                && dest < ((ULONG_PTR)ct->pTarget + sizeof(JMP_REL)))
            {
                if (jmpDest < dest)
                    jmpDest = dest;
            }
            else
            {
#if defined(_M_X64) || defined(__x86_64__)
                jmp.address = dest;
#else
                jmp.operand = (UINT32)(dest - (pNewInst + sizeof(jmp)));
#endif
                pCopySrc = &jmp;
                copySize = sizeof(jmp);

                // Exit the function if it is not in the branch.
                finished = (pOldInst >= jmpDest);
            }
        }
        else if ((hs.opcode & 0xF0) == 0x70
            || (hs.opcode & 0xFC) == 0xE0
            || (hs.opcode2 & 0xF0) == 0x80)
        {
            // Direct relative Jcc
            ULONG_PTR dest = pOldInst + hs.len;

            if ((hs.opcode & 0xF0) == 0x70      // Jcc
                || (hs.opcode & 0xFC) == 0xE0)  // LOOPNZ/LOOPZ/LOOP/JECXZ
                dest += (INT8)hs.imm.imm8;
            else
                dest += (INT32)hs.imm.imm32;

            // Simply copy an internal jump.
            if ((ULONG_PTR)ct->pTarget <= dest
                && dest < ((ULONG_PTR)ct->pTarget + sizeof(JMP_REL)))
            {
                if (jmpDest < dest)
                    jmpDest = dest;
            }
            else if ((hs.opcode & 0xFC) == 0xE0)
            {
                // LOOPNZ/LOOPZ/LOOP/JCXZ/JECXZ to the outside are not supported.
                return FALSE;
            }
            else
            {
                UINT8 cond = ((hs.opcode != 0x0F ? hs.opcode : hs.opcode2) & 0x0F);
#if defined(_M_X64) || defined(__x86_64__)
                // Invert the condition in x64 mode to simplify the conditional jump logic.
                jcc.opcode  = 0x71 ^ cond;
                jcc.address = dest;
#else
                jcc.opcode1 = 0x80 | cond;
                jcc.operand = (UINT32)(dest - (pNewInst + sizeof(jcc)));
#endif
                pCopySrc = &jcc;
                copySize = sizeof(jcc);
            }
        }
        else if ((hs.opcode & 0xFE) == 0xC2)
        {
            // RET (C2 or C3)

            // Complete the function if not in a branch.
            finished = (pOldInst >= jmpDest);
        }

        // Can't alter the instruction length in a branch.
        if (pOldInst < jmpDest && copySize != hs.len)
            return FALSE;

        // Trampoline function is too large.
        if ((newPos + copySize) > TRAMPOLINE_MAX_SIZE)
            return FALSE;

        // Trampoline function has too many instructions.
        if (ct->nIP >= ARRAYSIZE(ct->oldIPs))
            return FALSE;

        ct->oldIPs[ct->nIP] = oldPos;
        ct->newIPs[ct->nIP] = newPos;
        ct->nIP++;

        // Avoid using memcpy to reduce the footprint.
#ifndef ALLOW_INTRINSICS
        memcpy((LPBYTE)ct->pTrampoline + newPos, pCopySrc, copySize);
#else
        __movsb((LPBYTE)ct->pTrampoline + newPos, (LPBYTE)pCopySrc, copySize);
#endif
        newPos += copySize;
        oldPos += hs.len;
    } while (!finished);

    // Is there enough place for a long jump?
    if (oldPos < sizeof(JMP_REL)
        && !IsCodePadding((LPBYTE)ct->pTarget + oldPos, sizeof(JMP_REL) - oldPos))
    {
        // Is there enough place for a short jump?
        if (oldPos < sizeof(JMP_REL_SHORT)
            && !IsCodePadding((LPBYTE)ct->pTarget + oldPos, sizeof(JMP_REL_SHORT) - oldPos))
        {
            return FALSE;
        }

        // Can we place the long jump above the function?
        if (!IsExecutableAddress((LPBYTE)ct->pTarget - sizeof(JMP_REL)))
            return FALSE;

        if (!IsCodePadding((LPBYTE)ct->pTarget - sizeof(JMP_REL), sizeof(JMP_REL)))
            return FALSE;

        ct->patchAbove = TRUE;
    }

#if defined(_M_X64) || defined(__x86_64__)
    // Create a relay function.
    jmp.address = (ULONG_PTR)ct->pDetour;

    ct->pRelay = (LPBYTE)ct->pTrampoline + newPos;
    memcpy(ct->pRelay, &jmp, sizeof(jmp));
#endif

    return TRUE;
}
