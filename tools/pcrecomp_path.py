"""Locate the pcrecomp toolkit (https://github.com/sp00nznet/pcrecomp, MIT).

PCRECOMP_HOME if set, otherwise a sibling checkout named `pcrecomp` next to this
repository. Never a hardcoded absolute path.
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def pcrecomp_home():
    env = os.environ.get("PCRECOMP_HOME")
    cands = ([env] if env else []) + [os.path.join(os.path.dirname(ROOT), "pcrecomp"),
                                      os.path.join(ROOT, "third_party", "pcrecomp")]
    for c in cands:
        if c and os.path.isdir(os.path.join(c, "tools", "disasm")):
            return os.path.abspath(c)
    raise SystemExit("cannot find pcrecomp. Clone https://github.com/sp00nznet/pcrecomp next to this "
                     "repository or set PCRECOMP_HOME. Looked in: " + ", ".join(c for c in cands if c))
