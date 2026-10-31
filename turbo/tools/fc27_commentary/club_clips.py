"""Preview clips of club commentary names (E15, 5.0c): a clip index per language and an on-demand decode.

FC 27 has no club-name bank (club_ids.py). The commentator says a club's name inside team-keyed SoundWave families whose
Selection rows are keyed by club parameters (CLUB_PARAMS, the set the Turbo voice-swap hook rewrites). For every club id
this tool picks the SHORTEST segment of any such row (the clip most likely to be the name alone, brief E15 check g) and
records it; the decode is done on demand, one id at a time, into a cache. A row keyed by two clubs (home + away) may say
the other club too, so a row that names ONLY this club wins over a shorter two-club row; "clubs" in the index says which
kind was picked (1 = only this club, 2 = the row also names another club). The cache keeps each wav with a sidecar
<id>.json naming its (set, family, segment); a wav whose sidecar does not match the index row (or has none) is decoded
again, so a rebuilt index never leaves a stale clip behind.

    python -I club_clips.py --lang eng_us --index --out ...\\turbo\\callnames\\clubs\\eng_us_clips.json
    python -I club_clips.py --lang eng_us --clips ...\\eng_us_clips.json --decode 95 --cache ...\\turbo_output\\cache\\teamaudio
    python -I club_clips.py --selftest

Index json: {"lang", "game_build", "clips": {"<id>": {"set", "family", "segment", "variation", "param", "samples", "clubs"}}}.
A decoded clip is <cache>\\<lang>\\<id>.wav (mono, 48 kHz, the bank's native rate); a silent decode (RMS < 1e-6) is
refused and removed (eafc-voice-audio rule 3). Codec 0x0F (eng_us) decodes stream 1 only (fc27_commentary.speech_packets).
One language per call; nothing in the game folder is written.
"""
import argparse
import array
import json
import math
import os
import shutil
import sys
import tempfile
import time
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fc27_commentary as fc  # noqa: E402
from club_ids import CLUB_PARAMS  # noqa: E402  (also registers the parameter names in fc.NAMES)


def wav_stats(path):
    """(rms in [0,1], peak in [0,1], seconds, rate, channels) of a 16-bit PCM wav."""
    with wave.open(path, "rb") as w:
        n, rate, ch, sw = w.getnframes(), w.getframerate(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(n)
    if sw != 2:
        raise fc.FormatError("%s: %d-byte samples, only 16-bit is measured" % (path, sw))
    a = array.array("h")
    a.frombytes(raw)
    if sys.byteorder != "little":
        a.byteswap()
    if not a:
        return 0.0, 0.0, 0.0, rate, ch
    ss = sum(x * x for x in a)
    rms = math.sqrt(ss / len(a)) / 32768.0
    peak = max(abs(x) for x in a) / 32768.0
    return rms, peak, n / float(rate), rate, ch


def _family(bank, set_name, family):
    name = bank.family_name(family, set_name)
    a = bank.assets.get(name)
    if not a or "res" not in a or "ebx" not in a:
        raise fc.FormatError("%s/%s not found in the %s bank" % (set_name, family, bank.lang))
    toc_path, loc, r = a["res"]
    res = bank.cas.read(toc_path, loc)
    if len(res) != r["size"]:
        raise fc.FormatError("%s: res is %d bytes, the manifest says %d" % (name, len(res), r["size"]))
    return {"name": name, "res": res, "ebx": bank.cas.read(*a["ebx"]), "sets": fc.parse_sble(res)}


def club_rows(sel, wanted=None):
    """[(id, segment, variation, param, clubs)] of one Selection data set, over its club-parameter indexes; clubs = how many
    distinct club ids the row is keyed by (1 = only this club, 2 = home + away)."""
    seg = sel.fields.get(fc.H_SEGMENT)
    var = sel.fields.get(fc.djb2x("VariationId"))
    if seg is None:
        return []
    out = []
    for ix in sel.indexes:
        names = [p["name"] for p in ix["params"] if p["name"] in CLUB_PARAMS]
        if not names:
            continue
        try:
            keys = sel.row_keys(ix)
        except fc.FormatError:
            continue
        for r, k in keys.items():
            nclubs = len({k.get(p) for p in names if k.get(p) and k.get(p) > 0})
            for p in names:
                i = k.get(p)
                if i and i > 0 and (wanted is None or i in wanted):
                    out.append((i, seg[r], var[r] if var is not None else 0, p, nclubs))
    return out


def segment_samples(bank, fam):
    """{segment: samples} from each segment's SNS header (no decode)."""
    out = {}
    for s, d, o in fc.segment_streams(bank, fam):
        try:
            head, _packets = fc.parse_sns(d, o)
        except (fc.FormatError, IndexError):
            continue
        out[s] = head["samples"]
    return out


def pick_shortest(cands):
    """cands [(id, set, family, segment, variation, param, samples, clubs)] -> {id: the best row}: a row keyed by this club
    alone (clubs == 1) before a row that also names another club, then the fewest samples. A candidate without the clubs
    field counts as a two-club row. Ties break on (set, family, segment) so the pick is stable across runs."""
    def rank(c):
        clubs = c[7] if len(c) > 7 else 2
        return (clubs > 1, c[6], c[1], c[2], c[3])
    best = {}
    for c in cands:
        i = c[0]
        if c[6] <= 0:
            continue
        if i not in best or rank(c) < rank(best[i]):
            best[i] = c
    return best


def build_index(bank, wanted=None, log=print):
    cands, errors, t0 = [], [], time.time()
    for s in sorted(bank.sets):
        for f in bank.families(s):
            try:
                a = bank.assets[bank.family_name(f, s)]
                sel = fc.parse_sble(bank.cas.read(*a["res"][:2])).get("Selection")
                if sel is None:
                    continue
                rows = club_rows(sel, wanted)
                if not rows:
                    continue
                fam = _family(bank, s, f)
                lens = segment_samples(bank, fam)
            except (fc.FormatError, KeyError, IndexError) as ex:
                errors.append("%s/%s: %s" % (s, f, ex))
                continue
            for i, seg, var, p, n in rows:
                cands.append((i, s, f, seg, var, p, lens.get(seg, 0), n))
    best = pick_shortest(cands)
    log("index: %d candidate rows (%d keyed by two clubs), %d clubs with a clip (%d by a row naming only that club, "
        "%d only by a two-club row), %d families skipped, %.1f s"
        % (len(cands), sum(1 for c in cands if c[7] > 1), len(best), sum(1 for c in best.values() if c[7] == 1),
           sum(1 for c in best.values() if c[7] != 1), len(errors), time.time() - t0))
    for e in errors[:20]:
        log("  skipped " + e)
    return {str(i): {"set": c[1], "family": c[2], "segment": c[3], "variation": c[4], "param": c[5], "samples": c[6],
                     "clubs": c[7]} for i, c in sorted(best.items())}


def clip_path(cache, lang, club_id):
    return os.path.join(cache, lang, "%d.wav" % int(club_id))


def _source(row):
    return {"set": row["set"], "family": row["family"], "segment": row["segment"]}


def cache_matches(wav, row):
    """True when <id>.wav exists and its sidecar <id>.json names the same (set, family, segment) as the index row."""
    side = os.path.splitext(wav)[0] + ".json"
    if not (os.path.exists(wav) and os.path.exists(side)):
        return False
    try:
        with open(side, encoding="utf-8") as f:
            return json.load(f) == _source(row)
    except (OSError, ValueError):
        return False


def decode_clip(bank, clips, club_id, cache, ffmpeg, log=print):
    """Decode one club's clip into <cache>/<lang>/<id>.wav (+ sidecar <id>.json). Returns (path, stats) or raises FormatError
    with the reason. A wav whose sidecar matches the index row is returned as it is; any other one is decoded again."""
    out = clip_path(cache, bank.lang, club_id)
    row = clips.get(str(int(club_id)))
    if row is None:
        raise fc.FormatError("no clip: club %s has no club-parameter row in the %s bank" % (club_id, bank.lang))
    if cache_matches(out, row):
        return out, wav_stats(out)
    fam = _family(bank, row["set"], row["family"])
    tmp = tempfile.mkdtemp(prefix="e15clip_")
    try:
        problems = fc.write_wavs(bank, fam, "c", tmp, ffmpeg, {row["segment"]}, log)
        got = os.path.join(tmp, "c_%d_%d.wav" % (row["segment"], row["segment"]))
        if problems or not os.path.exists(got):
            raise fc.FormatError("decode failed: %s" % ("; ".join(problems) or "no wav written"))
        st = wav_stats(got)
        if not (st[0] >= 1e-6 and math.isfinite(st[0])):
            raise fc.FormatError("silent decode refused (RMS %.3g)" % st[0])
        os.makedirs(os.path.dirname(out), exist_ok=True)
        if cache_matches(out, row):      # raced with another caller: keep the first file
            return out, wav_stats(out)
        if os.path.exists(out):          # a stale clip (sidecar missing or another segment)
            os.remove(out)
        shutil.move(got, out)
        with open(os.path.splitext(out)[0] + ".json", "w", encoding="utf-8") as f:
            json.dump(_source(row), f)
        return out, st
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ---------------------------------------------------------------------------------------------------- self-test
def self_test():
    fails = []

    def check(cond, what):
        print(("PASS " if cond else "FAIL ") + what)
        if not cond:
            fails.append(what)

    b = pick_shortest([(7, "a", "f1", 3, 0, "home_ID", 90000), (7, "a", "f2", 1, 0, "away_ID", 30000),
                       (7, "b", "f0", 0, 0, "home_ID", 0), (9, "a", "f1", 4, 0, "home_ID", 50000)])
    check(b[7][2] == "f2" and b[7][6] == 30000, "shortest segment wins; a zero-length segment is never picked")
    check(set(b) == {7, 9}, "one clip per club id")
    d = tempfile.mkdtemp(prefix="e15st_")
    try:
        p = os.path.join(d, "t.wav")
        with wave.open(p, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(48000)
            w.writeframes(array.array("h", [0] * 4800).tobytes())
        rms, peak, sec, rate, ch = wav_stats(p)
        check(rms == 0.0 and abs(sec - 0.1) < 1e-9 and rate == 48000 and ch == 1, "silence measures RMS 0 (would be refused)")
        check(clip_path(d, "eng_us", 95).endswith(os.path.join("eng_us", "95.wav")), "cache path <cache>/<lang>/<id>.wav")
    finally:
        shutil.rmtree(d, ignore_errors=True)
    print("selftest: %d FAIL" % len(fails))
    return 1 if fails else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lang")
    ap.add_argument("--game", default=fc.DEFAULT_GAME)
    ap.add_argument("--index", action="store_true", help="build the clip index (--out)")
    ap.add_argument("--ids", help="clubs/<lang>.json: index only these ids")
    ap.add_argument("--out")
    ap.add_argument("--clips", help="the clip index json (for --decode)")
    ap.add_argument("--decode", action="append", type=int, help="club id to decode (repeatable)")
    ap.add_argument("--cache", help="cache root, e.g. turbo_output\\cache\\teamaudio")
    ap.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return self_test()
    if not a.lang:
        ap.error("--lang is required")
    bank = fc.Bank(a.game, a.lang)
    try:
        if a.index:
            if not a.out:
                ap.error("--index needs --out")
            if os.path.exists(a.out):
                print("refusing to overwrite %s" % a.out, file=sys.stderr)
                return 2
            wanted = None
            if a.ids:
                with open(a.ids, encoding="utf-8") as f:
                    wanted = set(json.load(f)["ids"])
            clips = build_index(bank, wanted)
            os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
            with open(a.out, "w", encoding="utf-8") as f:
                json.dump({"lang": a.lang, "game_build": fc.game_build(a.game), "clips": clips}, f, indent=1)
                f.write("\n")
            print("%s: %d clips -> %s" % (a.lang, len(clips), a.out))
        if a.decode:
            if not (a.clips and a.cache and a.ffmpeg):
                ap.error("--decode needs --clips, --cache and ffmpeg")
            with open(a.clips, encoding="utf-8") as f:
                clips = json.load(f)["clips"]
            rc = 0
            for i in a.decode:
                try:
                    p, st = decode_clip(bank, clips, i, a.cache, a.ffmpeg, log=lambda *_: None)
                    print("CLIP %s id=%d %s rms=%.5f peak=%.4f dur=%.3fs rate=%d ch=%d"
                          % (a.lang, i, p, st[0], st[1], st[2], st[3], st[4]))
                except fc.FormatError as ex:
                    print("NOCLIP %s id=%d %s" % (a.lang, i, ex))
                    rc = 1
            return rc
    finally:
        bank.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
