# HoverNet 2.0 Phase 2 verification

Phase 2 implementation is covered by the following repeatable checks:

- `ctest --test-dir build/linux --output-on-failure` exercises negotiation,
  framing, simultaneous isolated races, lobby/race chat, duplicate names,
  host promotion, disconnect cleanup, capacity recovery, graceful shutdown,
  restart, malformed packets, and deterministic protocol fuzzing.
- `build/phase2-sanitize` runs the protocol, lifecycle, and fuzz scenarios with
  AddressSanitizer and UndefinedBehaviorSanitizer. The local qualification run
  on 28 September 2026 completed all four selected tests without findings.
- `ldd build/linux/RaceServer` is the dependency inventory for the production
  server binary. It has no bundled networking/XML runtime: configuration parsing
  is internal, and runtime dependencies are the platform C/C++ libraries.
- GitHub CI repeats the security-sensitive tests under ASan/UBSan on every push.

The release exit gate is a wall-clock test, not an accelerated-test substitute:

```bash
scripts/run-raceserver-soak.sh build/linux 86400 phase2-soak.log
```

The driver repeatedly runs the full protocol integration scenario, capacity and
restart lifecycle checks, and 500-frame fragmented fuzz batches. A release may
claim the Phase 2 exit gate only when the final line reports `result=pass`, the
measured duration is at least 86,400 seconds, and the log contains no sanitizer,
crash, deadlock, forced-kill, or orphaned-race failure.
