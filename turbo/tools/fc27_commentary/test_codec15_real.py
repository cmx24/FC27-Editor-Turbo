#!/usr/bin/env python3
"""E15 (5.0c): decode ONE real eng_us (SNS codec 0x0F, EAOPUSMU) segment with fc27_commentary.write_wavs and check the
wav is not silent (RMS > 1e-6, eafc-voice-audio rule 3) and lasts more than 0.2 s at the bank's native 48 kHz mono.
Reads the installed game's eng_us bank (read-only); prints SKIP and exits 3 when the game or ffmpeg is absent.

usage: python -I test_codec15_real.py [--module <path to an fc27_commentary.py>] [--game <game folder>]
Fail-old: point --module at the pre-port copy, whose write_wavs accepts codec 0x0C / 0x0E only.
"""
import argparse
import importlib.util
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", default=os.path.join(HERE, "fc27_commentary.py"))
    ap.add_argument("--game", default=None)
    ap.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    a = ap.parse_args(argv)
    sys.path.insert(0, HERE)
    spec = importlib.util.spec_from_file_location("fc27_commentary", a.module)
    fc = importlib.util.module_from_spec(spec)
    sys.modules["fc27_commentary"] = fc
    spec.loader.exec_module(fc)
    sys.path.insert(0, HERE)
    from club_clips import wav_stats  # noqa: E402  (uses the module loaded above)
    game = a.game or fc.DEFAULT_GAME
    if not a.ffmpeg or not os.path.isdir(game):
        print("SKIP no game folder or no ffmpeg")
        return 3
    bank = fc.Bank(game, "eng_us")
    fails = []
    try:
        picked = None
        for f in bank.families():
            fam = bank.load_family(f)
            try:
                streams = fc.segment_streams(bank, fam)
            except fc.FormatError:
                continue
            for s, d, o in streams:
                head, _p = fc.parse_sns(d, o)
                if head["codec"] == 0x0F and head["samples"] > 0.3 * 48000:
                    picked = (f, fam, s, head)
                    break
            if picked:
                break
        if not picked:
            print("FAIL no codec 0x0F segment longer than 0.3 s in the eng_us primary set")
            return 1
        f, fam, s, head = picked
        print("block: eng_us %s segment %d codec %#x channels %d samples %d" % (f, s, head["codec"], head["channels"], head["samples"]))
        tmp = tempfile.mkdtemp(prefix="c15t_")
        try:
            problems = fc.write_wavs(bank, fam, "t", tmp, a.ffmpeg, {s}, lambda *_: None)
            path = os.path.join(tmp, "t_%d_%d.wav" % (s, s))
            ok = not problems and os.path.exists(path)
            print(("PASS " if ok else "FAIL ") + "write_wavs decodes the codec 0x0F segment" + ("" if ok else ": %s" % problems))
            if not ok:
                fails.append("decode")
            else:
                rms, peak, sec, rate, ch = wav_stats(path)
                print("measured: rms=%.6f peak=%.4f dur=%.3fs rate=%d ch=%d" % (rms, peak, sec, rate, ch))
                for cond, what in ((rms > 1e-6, "not silent (RMS > 1e-6)"), (sec > 0.2, "longer than 0.2 s"),
                                   (rate == 48000 and ch == 1, "native 48 kHz mono")):
                    print(("PASS " if cond else "FAIL ") + what)
                    if not cond:
                        fails.append(what)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    finally:
        bank.close()
    print("RESULT %d FAIL" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
