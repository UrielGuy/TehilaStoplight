#!/usr/bin/env python3
"""tools/stoplight_render.py against tools/stopcli (the firmware's C++), byte
for byte: sizes, sprites, `check` on many texts and `rle` / `preview` on many
preview queries. Run under WSL / Linux (it builds tools/stopcli):

    python3 tools/render_parity_test.py [--quick] [--seed N]

Prints the number of cases per kind and the first mismatches; exits 1 on any.
"""
import argparse, os, random, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import stoplight_render as sr   # noqa: E402

CLI = os.path.join(HERE, "stopcli")
SRCS = ["tools/stopcli.cpp", "src/TextLayout.cpp", "src/Face.cpp", "src/Preview.cpp"]
DEPS = SRCS + ["src/fonts/StopFonts.h", "src/TextLayout.h", "src/Face.h", "src/Canvas.h", "src/Preview.h",
               "src/sprites/Sprites.h", "src/Font.h", "src/Sprite.h"]


def build():
    newest = max(os.path.getmtime(os.path.join(ROOT, p)) for p in DEPS)
    if not os.path.exists(CLI) or os.path.getmtime(CLI) < newest:
        print("building tools/stopcli ...")
        subprocess.check_call(["g++", "-std=c++11", "-O2", "-Isrc"] + SRCS + ["-o", CLI], cwd=ROOT)


EN = ["STOP", "GO", "Wait", "walk", "don't", "WALK!", "Hello,", "world.", "a", "I", "the", "quick", "brown", "fox",
      "jumps", "over", "lazy", "dog", "Tehila", "birthday", "PARTY", "now", "please", "wait...", "OK?", "yes/no",
      "100%", "#1", "$5", "a+b=c", "x_y", "@home", "~", "^_^", ":-)", "e-mail", "WWWWWWWW", "iiiiiiii", "MMMMMMMMMMMMMM",
      "Supercalifragilistic", "ÀÉÎ", "çà", "°C", "–", "…", "₪"]
HE = ["עצור", "המתן", "סע", "שלום", "תהילה", "יום", "הולדת", "שמח", "מסיבה", "עכשיו", "בבקשה", "חכה", "רגע", "לך",
      "אהבה", "גשם", "ש\"ח", "צה\"ל", "ג'ירפה", "מזל-טוב", "שַׁבָּת", "ךםןףץ", "אאאאאאאאאאאאאאאאאא", "בית־ספר", "׳", "״"]
NUM = ["1", "2", "3", "12", "2026", "3.14", "-5", "+972", "10:30", "1,000", "50%", "(1)", "1-2", "007"]
PUNCT = ["!", "?", ".", ",", "-", "(", ")", "[", "]", "{", "}", "<", ">", "\"", "'", ":", ";", "/", "\\", "&", "*", "=",
         "(עצור)", "[GO]", "{שלום}", "<סע>", "(a)", "!!", "?!", "...", "«x»"]


def fixed_texts():
    t = ["STOP", "GO", "המתן", "עצור", "סע", "", " ", "\n", "  \n  ", "a", "A B", "STOP NOW", "Don't walk",
         "שלום עולם", "שלום world", "hello עולם", "עצור (stop) עכשיו", "(שלום)", "[א] ב", "a (ב) c", "1 2 3",
         "שעה 10:30", "מחיר 50 ₪", "ש\"ח 100", "יום הולדת 12 שמח!", "Hello, world!", "WAIT\nHERE", "line1\nline2\nline3",
         "a\n\n\nb", "\nleading", "trailing\n", "x\ny\nz\nw\nv\nu\nt\ns\nr", "one two three four five six seven eight nine",
         "the quick brown fox jumps over the lazy dog and keeps on running far away",
         "אחת שתיים שלוש ארבע חמש שש שבע שמונה תשע עשר אחת עשרה שתים עשרה שלוש עשרה ארבע עשרה חמש עשרה",
         "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM", "Supercalifragilisticexpialidocious", "tab\there", "nbsp here",
         "cr\r\nlf", "‎ltr mark‏", "﻿bom", "nikud שָׁלוֹם", "emoji 😀", "日本", "€", "control\x01char",
         "ñ", "°", "–…₪", "a" * 240, "word " * 60, "מילה " * 50, "(" * 10, "<<>>", "{[()]}", "עברית (English) עברית",
         "English (עברית) English", "123 עברית 456", "עברית 1.5 עברית", "a-b c-d", "- - -", "!", "?!", "...",
         "Tehila 13", "תהילה בת 13", "STOP!\nעצור!", "GO\nסע\nGO", "abc\ndef ghi jkl mno pqr stu vwx yz"]
    return t


def random_text(rng):
    k = rng.choice([1, 1, 2, 2, 3, 3, 4, 5, 6, 8, 10, 12, 13, 14, 15, 16, 20, 25])
    pools = rng.choice([[EN], [HE], [EN, HE], [EN, HE, NUM, PUNCT], [HE, NUM], [EN, PUNCT], [HE, PUNCT]])
    words = []
    for _ in range(k):
        words.append(rng.choice(rng.choice(pools)))
    out = ""
    for i, w in enumerate(words):
        if i:
            out += rng.choice([" "] * 8 + ["\n", "  ", " \n "])
        out += w
    return out


def stop(args):
    return subprocess.run([CLI] + args, capture_output=True, timeout=30).stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    build()
    rng = random.Random(a.seed)
    E = sr.Engine()
    fails, counts, outcome = [], {}, {}

    def compare(kind, args):
        counts[kind] = counts.get(kind, 0) + 1
        want = stop(args)
        got = E.run(args)
        if got != want:
            fails.append((kind, args, want[:200], got[:200]))
        if kind == "check":
            m = re.search(rb'"ok":(true|false)(?:,"size":\d+,"lines":(\d+)|,"error":"([^"]*)")', want)
            if m and m.group(1) == b"true":
                outcome[f"fits on {int(m.group(2))} line(s)"] = outcome.get(f"fits on {int(m.group(2))} line(s)", 0) + 1
            elif m:
                e = m.group(3).decode("utf-8", "replace").split(":")[0]
                outcome[e] = outcome.get(e, 0) + 1
            if len(args[2].split()) > sr.MAX_ENUM_WORDS:
                outcome["over 14 words (greedy)"] = outcome.get("over 14 words (greedy)", 0) + 1

    t0 = time.time()
    compare("sizes", ["sizes"])
    compare("sprites", ["sprites"])

    texts = fixed_texts() + [random_text(rng) for _ in range(150 if a.quick else 900)]
    caps = [0, 0, 56, 36, 22, 16, 11, 5, 100]
    for i, t in enumerate(texts):
        for cap in ([0, rng.choice(caps)] if i % 3 else caps):
            compare("check", ["check", str(rng.randrange(3)), t, str(cap)])
    compare("check", ["check", "1", "STOP"])   # no size argument

    sprites = [s.id.decode() for s in E.sprites]
    queries = []
    for sid in sprites:
        for k in (0, 1, 64, 65, 149, 200, 999, 1000, 1559, 12345, 4294967295):
            light = rng.randrange(3)
            queries.append([f"g{light}={sid}", f"k={k}", f"c{light}={rng.randrange(64)}", f"f{light}={rng.randrange(64)}"])
    for m in range(8):
        for o in (0, 1):
            queries.append([f"m={m}", f"o={o}", "t0=STOP", "t1=המתן", "t2=GO", "g1=walking"])
            queries.append([f"m={m}", f"o={o}", "c0=0", "c1=63", "c2=21"])
    for c in (0, 1, 2, 3, 12, 48, 52, 63):
        for f in (0, c, 63, 3):
            queries.append([f"c0={c}", f"f0={f}", "t0=STOP", f"c1={c}", f"f1={f}", "g1=dancing", "k=440",
                            f"c2={c}", f"f2={f}", "t2=שלום"])
    queries.append(["c0=", "f0=", "t0=x", "m=", "k=", "o="])   # empty values: defaults
    queries.append(["k0=off", "k1=off", "t0=STOP", "t1=GO", "t2=BYE", "k2=on"])
    queries.append(["g0=nosuch", "t0=STOP", "g1=fx:nosuch"])
    queries.append(["t0=STOP", "t0=GO"])   # first one wins
    for ms in range(0, 6001, 41):   # beat animations through several beats: wave swing, quarters turning, clock ticks
        queries.append(["g0=fx:wave", "g1=fx:quarters", "q1=60,12,3", "g2=fx:wave", "c2=0", "f2=0", "hc0=30",
                        "bpm=120", f"k={ms}", f"b={ms + 1000}"])
        queries.append(["g0=fx:clock", "c0=0", "f0=63", "g1=fx:clock", "c1=48", "f1=48", "hf1=60", "g2=fx:clock",
                        "c2=12", "hc2=90", "bpm=97", f"k={ms * 13}", f"b={ms * 13 + 333}"])
    bpms = [0, 1, 60, 90, 120, 128, 140, 180, 300, 65536 + 120, -5]
    for _ in range(60 if a.quick else 400):
        q = []
        for i in range(3):
            kind = rng.choice(["fx:quarters", "fx:pulse", "fx:wave", "fx:clock", "text", "sprite", "none"])   # not fx:ecg: the Python port is frozen without it (CLAUDE.md)
            if kind.startswith("fx"):
                q.append(f"g{i}={kind}")
                if rng.random() < 0.7:
                    q.append(f"q{i}=" + ",".join(str(rng.randrange(64)) for _ in range(rng.choice([1, 2, 3, 3, 4]))))
            elif kind == "text":
                q.append(f"t{i}={random_text(rng)}")
                if rng.random() < 0.4:
                    q.append(f"s{i}={rng.choice(caps)}")
            elif kind == "sprite":
                q.append(f"g{i}={rng.choice(sprites)}")
            if rng.random() < 0.6:
                q.append(f"hc{i}={rng.choice([0, 30, 60, 90, 120, 45, -60, 359, 360, 720, 15, -1])}")
            if rng.random() < 0.5:
                q.append(f"hf{i}={rng.choice([0, 30, 60, 90, 120, -90, 180, 7])}")
            if rng.random() < 0.8:
                q.append(f"c{i}={rng.randrange(64)}")
            if rng.random() < 0.6:
                q.append(f"f{i}={rng.randrange(64)}")
        q.append(f"bpm={rng.choice(bpms)}")
        k = rng.choice([0, 1, 250, 499, 500, 501, 999, 1000, 2500, 33333, 600000, 4000000000])
        q.append(f"k={k}")
        if rng.random() < 0.5:
            q.append(f"b={k + rng.choice([0, 0, 1, 250, 500, 1234, 77777])}")
        if rng.random() < 0.3:
            q.append(f"b={max(0, k - 300)}")
        q.append(f"m={rng.randrange(8)}")
        if rng.random() < 0.3:
            q.append("o=1")
        rng.shuffle(q)
        queries.append(q)
    for t in texts[:(40 if a.quick else 200)]:
        for cap in (0, rng.choice(caps)):
            light = rng.randrange(3)
            queries.append([f"t{light}={t}", f"s{light}={cap}", f"c{light}={rng.randrange(64)}", f"f{light}={rng.randrange(64)}"])
    for q in queries:
        compare("rle", ["rle"] + q)
    for q in queries[::(10 if a.quick else 4)]:
        compare("preview", ["preview"] + q)
    for q in queries[::(40 if a.quick else 20)]:
        compare("ppm", ["ppm"] + q)

    print(f"{sum(counts.values())} cases in {time.time() - t0:.1f} s: " + ", ".join(f"{k} {v}" for k, v in counts.items()))
    print("check outcomes (stopcli): " + ", ".join(f"{k} {v}" for k, v in sorted(outcome.items())))
    for kind, args, want, got in fails[:10]:
        print(f"MISMATCH {kind} {args!r}\n  stopcli: {want!r}\n  python:  {got!r}")
    if fails:
        print(f"{len(fails)} mismatches")
        return 1
    print("all identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
