#!/usr/bin/env bash
# Tests the "never deploy an older web build over a newer one" guard of packaging/web/hovernet-web-deploy (no root, no server needed).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
python3 -I - "$here/../packaging/web/hovernet-web-deploy" <<'PY'
import importlib.machinery, importlib.util, sys
loader = importlib.machinery.SourceFileLoader("deploy", sys.argv[1])
spec = importlib.util.spec_from_loader("deploy", loader)
d = importlib.util.module_from_spec(spec)
loader.exec_module(d)


def refused(new, live):
    try:
        d.refuse_older(new, live)
    except SystemExit:
        return True
    return False

plain = '<head><meta name="hn-build" content="abc"><meta name="hn-order" content="1700000500"></head>'
mini = '<!doctypehtml><html><head><meta content=abc name=hn-build><meta content=1700000500 name=hn-order></head>'
checks = [
    ("reads the plain page", d.build_order(plain) == 1700000500),
    ("reads the minified page", d.build_order(mini) == 1700000500),
    ("an old page without a stamp reads as none", d.build_order('<meta name="hn-build" content="abc">') is None),
    ("an unfilled placeholder reads as none", d.build_order('<meta name="hn-order" content="__HN_ORDER__">') is None),
    ("older than live is refused", refused(1700000000, 1700000500)),
    ("equal to live is allowed (a tag of the commit already live)", not refused(1700000500, 1700000500)),
    ("newer than live is allowed", not refused(1700000600, 1700000500)),
    ("unstamped over stamped is refused", refused(None, 1700000500)),
    ("anything over an unstamped live build is allowed (first stamped deploy)", not refused(1700000500, None) and not refused(None, None)),
]
bad = [name for name, ok in checks if not ok]
for name, ok in checks:
    print(("ok   " if ok else "FAIL ") + name)
sys.exit(1 if bad else 0)
PY
