"""Code quality of a tier 0 result, judged against the seed it started from and the reference it should reach (never seen by the worker).

A test pass says the behaviour is right; it does not say the edit was minimal, that the code around the defect was left alone, that nothing half done
was left behind, or that the result is not a copy-paste sprawl. Each measure is per file the item owns:

  changed     lines the worker changed (seed -> final), against `ideal`, the lines the reference differs from the seed by
  collateral  lines changed outside the regions the reference changes (the code around the defect, rewritten for no reason)
  leftovers   stub markers, merge-conflict markers, TODO/FIXME the reference does not have: a HARD failure (the work is not finished)
  repeated    lines that occur three or more times once string and number literals are blanked, beyond the reference's own (copy-paste, not DRY)
  format      tabs, trailing spaces, lines over 160 columns, beyond the reference's own
  comments    the reference's comment lines that were in the seed and are gone from the result
  overlap     for a test file: share of its checks that repeat a check of the given tests (a tester that copies, not covers)
"""
import difflib, re

LEFTOVER = re.compile(r"not implemented|^(<<<<<<<|=======$|>>>>>>>)|\bTODO\b|\bFIXME\b", re.M)
LITERALS = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\b\d+(?:\.\d+)?\b')
CHECK = re.compile(r"CHECK(?:_NEAR|_EQ|_THROWS)?\((.*?)\);")


def _touched(a, b, context=0):
    """Indices of lines of `a` that the a -> b diff replaces or deletes, widened by `context` lines; and the count of lines changed on either side."""
    touched, changed = set(), 0
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if tag == "equal": continue
        changed += max(i2 - i1, j2 - j1)
        touched.update(range(max(0, i1 - context), min(len(a), max(i2, i1 + 1) + context)))
    return touched, changed


def _repeated(lines):
    seen = {}
    for l in lines:
        k = LITERALS.sub("L", l.strip())
        if len(k) >= 20: seen[k] = seen.get(k, 0) + 1
    return sum(n for n in seen.values() if n >= 3)


def _format(lines):
    return sum(("\t" in l) + (l != l.rstrip()) + (len(l) > 160) for l in lines)


def judge(seed, final, reference=None, given_tests=()):
    s, f = seed.splitlines(), final.splitlines()
    r = reference.splitlines() if reference is not None else None
    q = {}
    worker_touched, q["changed"] = _touched(s, f)
    q["leftovers"] = len(LEFTOVER.findall(final)) - (len(LEFTOVER.findall(reference)) if reference else 0)
    q["leftovers"] = max(0, q["leftovers"])
    q["format"] = max(0, _format(f) - (_format(r) if r else 0))
    if not given_tests:   # a test file repeats its check shape by nature: copy-paste is judged on code, and a tester by `overlap`
        q["repeated"] = max(0, _repeated(f) - (_repeated(r) if r else 0))
    if r is not None:
        defect, q["ideal"] = _touched(s, r, context=2)
        q["collateral"] = len(worker_touched - defect)
        ref_comments = {l.strip() for l in r if l.strip().startswith("//")}
        q["comments"] = sum(1 for l in {x.strip() for x in s} & ref_comments if l not in {x.strip() for x in f})
    if given_tests:
        mine = {re.sub(r"\s+", "", c) for c in CHECK.findall(final)}
        theirs = {re.sub(r"\s+", "", c) for t in given_tests for c in CHECK.findall(t)}
        q["checks"] = len(mine)
        q["overlap"] = round(len(mine & theirs) / len(mine), 2) if mine else 0.0
    return q


def summary(q):
    """One short cell: the hard failure first, then what deserves a look."""
    notes = []
    if q.get("leftovers"): notes.append(f"LEFTOVERS {q['leftovers']}")
    if "ideal" in q: notes.append(f"{q['changed']}/{q['ideal']} lines")
    else: notes.append(f"{q['changed']} lines")
    for k in ("collateral", "repeated", "format", "comments"):
        if q.get(k): notes.append(f"{k} {q[k]}")
    if "overlap" in q: notes.append(f"{q['checks']} checks, overlap {q['overlap']}")
    return ", ".join(notes)
