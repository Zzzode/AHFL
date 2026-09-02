#!/usr/bin/env python3
"""RFC 0026 KR6.5 E4-B2-B1b cross-process crash smoke for the integrity-only
durable-resume payload store.

Drives tests/integration/payload_store_worker.cpp across real process boundaries:
each case parks a `crash-*` worker at a chosen post-syscall StorePhase (via the
landed B1a TransactionObserver), waits for its rendezvous marker, SIGKILLs it, and
then reopens the store in a FRESH process to assert the legal old-or-new / NotFound
state and rebuild-not-adopt behaviour.

HONEST SCOPE: SIGKILL proves post-process-death visibility (the page cache survives),
NOT power-loss durability. The rendezvous marker is a control-plane signal, not store
durability evidence. nlink==0 read race and st_dev submount are NOT covered here.
"""

import json
import os
import selectors
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

SKIP = 77


def require(cond: bool, msg: str) -> None:
    """Fail-fast: a violated invariant raises so the failing case's directory is left
    in place for diagnosis (main() only cleans a case after it fully succeeds)."""
    if not cond:
        raise AssertionError(msg)


def reset_case(base: Path, name: str) -> Path:
    case = base / name
    shutil.rmtree(case, ignore_errors=True)
    # Explicit 0700 store + control dirs so a missing dir / permission error is never
    # misread as a platform/FS skip; only the worker's probe decides skip.
    (case / "store").mkdir(parents=True, exist_ok=True)
    (case / "control").mkdir(parents=True, exist_ok=True)
    os.chmod(case / "store", 0o700)
    os.chmod(case / "control", 0o700)
    return case


def run(worker: str, *args: str, timeout: float = 30.0) -> subprocess.CompletedProcess:
    return subprocess.run(
        [worker, *args], capture_output=True, text=True, timeout=timeout, check=False
    )


def load_state(worker: str, case: Path) -> dict:
    proc = run(worker, "load", str(case))
    if proc.returncode == SKIP:
        return {"state": "unsupported"}
    if proc.returncode not in (0, 4):
        return {"state": "error", "rc": proc.returncode, "stderr": proc.stderr}
    try:
        return json.loads(proc.stdout.strip() or "{}")
    except json.JSONDecodeError:
        return {"state": "error", "raw": proc.stdout}


def wait_for(path: Path, proc: subprocess.Popen, timeout: float = 30.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        if proc.poll() is not None:
            out, err = proc.communicate()
            raise AssertionError(
                f"worker exited before marker: rc={proc.returncode}\n{out}\n{err}"
            )
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {path}")


def crash_at(
    worker: str, case: Path, op: str, phase: str, expected: int, tag: str = ""
) -> None:
    """Park a crash worker at `phase`, verify the rendezvous marker, then SIGKILL and
    assert the child was actually killed by the signal. On ANY exception the parked
    child is still killed and reaped so no worker leaks."""
    marker = case / "control" / "fault-ready.json"
    if op == "publish":
        argv = [worker, "crash-publish", str(case), phase, str(expected), tag]
    else:
        argv = [worker, "crash-consume", str(case), phase, str(expected)]
    proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    killed = False
    try:
        wait_for(marker, proc)
        # The marker JSON must describe this exact operation/phase/expected generation.
        meta = json.loads(marker.read_text())
        require(meta.get("operation") == op, f"{phase}: marker operation")
        require(meta.get("phase") == phase, f"{phase}: marker phase")
        require(meta.get("expected_generation") == expected, f"{phase}: marker expected")
        proc.send_signal(signal.SIGKILL)
        killed = True
        out, err = proc.communicate(timeout=10)
        require(proc.returncode == -signal.SIGKILL,
                f"{phase}: not SIGKILLed (rc={proc.returncode})")
        require("FAULT_READY" in out, f"{phase}: missing FAULT_READY ({err})")
    finally:
        if proc.poll() is None:
            try:
                proc.send_signal(signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                proc.communicate(timeout=10)
            except Exception:
                pass
        elif not killed:
            # child already gone but we never intentionally killed it -> drain pipes.
            try:
                proc.communicate(timeout=10)
            except Exception:
                pass


# Payload hex for the fixed tag enum (mirrors the worker).
TAG_HEX = {
    "baseline": "00010203",
    "orphan": "0a0b0c0d",
    "real": "deadbeef",
    "race-a": "aaaaaaaa",
    "race-b": "bbbbbbbb",
}

# The exact post-syscall publish phase order (Available first publish / update).
PUBLISH_PHASES = [
    "WorkflowDirParentFsynced",
    "CheckpointDirParentFsynced",
    "StageDirCreated",
    "RecordFileFsynced",
    "SlotFilesFsynced",
    "ManifestFileFsynced",
    "StageDirFsynced",
    "GenerationRenamed",
    "CheckpointDirFsyncedAfterGeneration",
    "PointerFileFsynced",
    "PointerRenamed",
    "CheckpointDirFsyncedAfterPointer",
]
# Consumed publish emits the applicable subsequence (no record/slot phases).
CONSUME_PHASES = [
    "StageDirCreated",
    "ManifestFileFsynced",
    "StageDirFsynced",
    "GenerationRenamed",
    "CheckpointDirFsyncedAfterGeneration",
    "PointerFileFsynced",
    "PointerRenamed",
    "CheckpointDirFsyncedAfterPointer",
]
# The two parent-dir-fsync phases fire ONLY on first-create (the wf/ckpt directories
# are made). An update publish / a consume over an existing checkpoint does not
# recreate them, so those phases are unreachable there.
UPDATE_PHASES = [p for p in PUBLISH_PHASES
                 if p not in ("WorkflowDirParentFsynced", "CheckpointDirParentFsynced")]
# Phases at or after which the pointer swap has happened (the commit point).
POST_COMMIT = {"PointerRenamed", "CheckpointDirFsyncedAfterPointer"}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: payload_store_crash_smoke.py <worker> <base-dir>", file=sys.stderr)
        return 2
    worker = sys.argv[1]
    base = Path(sys.argv[2])
    shutil.rmtree(base, ignore_errors=True)
    base.mkdir(parents=True, exist_ok=True)

    # Platform/FS preflight: probe the SAME store root shape. ONLY the worker's own
    # UnsupportedPlatform/UnsupportedFilesystem (exit 77) is a skip.
    pre = reset_case(base, "probe")
    probe = run(worker, "probe", str(pre))
    if probe.returncode == SKIP:
        print("SKIP: payload store requires a Linux durable filesystem", file=sys.stderr)
        shutil.rmtree(base, ignore_errors=True)
        return SKIP
    require(probe.returncode == 0, f"probe failed rc={probe.returncode} {probe.stderr}")

    # ---- probe shape negatives: missing / non-dir / symlink store root -------
    # A malformed store root is a SETUP error (exit 3), NEVER a platform/FS SKIP.
    neg = base / "neg-missing"
    shutil.rmtree(neg, ignore_errors=True)
    (neg / "control").mkdir(parents=True, exist_ok=True)  # store deliberately absent
    r = run(worker, "probe", str(neg))
    require(r.returncode == 3, f"neg-missing: expected rc3, got {r.returncode}")
    shutil.rmtree(neg, ignore_errors=True)

    neg = base / "neg-file"
    shutil.rmtree(neg, ignore_errors=True)
    (neg / "control").mkdir(parents=True, exist_ok=True)
    (neg / "store").write_text("not a directory")  # store is a regular file
    r = run(worker, "probe", str(neg))
    require(r.returncode == 3, f"neg-file: expected rc3, got {r.returncode}")
    shutil.rmtree(neg, ignore_errors=True)

    neg = base / "neg-symlink"
    shutil.rmtree(neg, ignore_errors=True)
    (neg / "control").mkdir(parents=True, exist_ok=True)
    (neg / "elsewhere").mkdir(parents=True, exist_ok=True)
    os.symlink("elsewhere", neg / "store")  # store is a symlink to a real dir
    r = run(worker, "probe", str(neg))
    require(r.returncode == 3, f"neg-symlink: expected rc3, got {r.returncode}")
    shutil.rmtree(neg, ignore_errors=True)

    # ---- FIRST-PUBLISH crash matrix (expected=0, target gen 1) --------------
    for phase in PUBLISH_PHASES:
        case = reset_case(base, f"first-{phase}")
        crash_at(worker, case, "publish", phase, 0, "orphan")
        st = load_state(worker, case)
        if phase in POST_COMMIT:
            require(st.get("state") == "available" and st.get("generation") == 1,
                    f"first-{phase}: post-commit expected available gen1, got {st}")
            require(st.get("slot_payload_hex") == TAG_HEX["orphan"],
                    f"first-{phase}: post-commit payload should be the crash writer's")
        else:
            require(st.get("state") == "notfound",
                    f"first-{phase}: pre-commit expected notfound, got {st}")
            # A fresh writer at the same expected generation rebuilds with REAL payload.
            rc = run(worker, "publish", str(case), "0", "real")
            require(rc.returncode == 0, f"first-{phase}: rebuild publish rc={rc.returncode}")
            st2 = load_state(worker, case)
            require(st2.get("state") == "available" and st2.get("generation") == 1 and
                    st2.get("slot_payload_hex") == TAG_HEX["real"],
                    f"first-{phase}: rebuild expected real gen1, got {st2}")
        shutil.rmtree(case, ignore_errors=True)

    # ---- UPDATE-PUBLISH crash matrix (seed gen1 baseline; crash expected=1) ---
    for phase in UPDATE_PHASES:
        case = reset_case(base, f"update-{phase}")
        require(run(worker, "publish", str(case), "0", "baseline").returncode == 0,
                f"update-{phase}: seed publish")
        crash_at(worker, case, "publish", phase, 1, "orphan")
        st = load_state(worker, case)
        if phase in POST_COMMIT:
            require(st.get("state") == "available" and st.get("generation") == 2 and
                    st.get("slot_payload_hex") == TAG_HEX["orphan"],
                    f"update-{phase}: post-commit expected orphan gen2, got {st}")
        else:
            require(st.get("state") == "available" and st.get("generation") == 1 and
                    st.get("slot_payload_hex") == TAG_HEX["baseline"],
                    f"update-{phase}: pre-commit expected baseline gen1, got {st}")
            rc = run(worker, "publish", str(case), "1", "real")
            require(rc.returncode == 0, f"update-{phase}: rebuild publish rc={rc.returncode}")
            st2 = load_state(worker, case)
            require(st2.get("state") == "available" and st2.get("generation") == 2 and
                    st2.get("slot_payload_hex") == TAG_HEX["real"],
                    f"update-{phase}: rebuild expected real gen2, got {st2}")
        shutil.rmtree(case, ignore_errors=True)

    # ---- CONSUMED crash matrix (seed gen1; crash-consume expected=1) ---------
    for phase in CONSUME_PHASES:
        case = reset_case(base, f"consume-{phase}")
        require(run(worker, "publish", str(case), "0", "baseline").returncode == 0,
                f"consume-{phase}: seed publish")
        crash_at(worker, case, "consume", phase, 1)
        st = load_state(worker, case)
        if phase in POST_COMMIT:
            require(st.get("state") == "consumed" and st.get("generation") == 2 and
                    st.get("consumed_generation") == 1,
                    f"consume-{phase}: post-commit expected consumed{{2,1}}, got {st}")
        else:
            require(st.get("state") == "available" and st.get("generation") == 1,
                    f"consume-{phase}: pre-commit expected available gen1, got {st}")
        shutil.rmtree(case, ignore_errors=True)

    # ---- CONSUMED pre-commit residue rebuild (PointerFileFsynced) -----------
    case = reset_case(base, "consume-retry")
    require(run(worker, "publish", str(case), "0", "baseline").returncode == 0,
            "consume-retry: seed")
    crash_at(worker, case, "consume", "PointerFileFsynced", 1)
    st = load_state(worker, case)
    require(st.get("state") == "available" and st.get("generation") == 1,
            f"consume-retry: pre-commit still available, got {st}")
    rc = run(worker, "consume", str(case), "1")
    require(rc.returncode == 0, f"consume-retry: fresh consume rc={rc.returncode}")
    st2 = load_state(worker, case)
    require(st2.get("state") == "consumed" and st2.get("generation") == 2 and
            st2.get("consumed_generation") == 1,
            f"consume-retry: rebuilt consumed{{2,1}}, got {st2}")
    shutil.rmtree(case, ignore_errors=True)

    # ---- ORPHAN rebuild-not-adopt (real kills; residue proven cleaned) -------
    # (a) residual gen-*.stage  <- crash at StageDirFsynced
    # (b) complete-unreferenced final gen  <- crash at GenerationRenamed
    # (c) residual pointer.tmp  <- crash at PointerFileFsynced
    orphan_cases = [
        ("orphan-stage", "StageDirFsynced"),
        ("orphan-final", "GenerationRenamed"),
        ("orphan-tmp", "PointerFileFsynced"),
    ]
    for name, phase in orphan_cases:
        case = reset_case(base, name)
        require(run(worker, "publish", str(case), "0", "baseline").returncode == 0,
                f"{name}: seed")
        crash_at(worker, case, "publish", phase, 1, "orphan")
        # Pre-commit: still the seeded gen1 baseline.
        st = load_state(worker, case)
        require(st.get("state") == "available" and st.get("generation") == 1 and
                st.get("slot_payload_hex") == TAG_HEX["baseline"],
                f"{name}: pre-rebuild expected baseline gen1, got {st}")
        # A fresh writer rebuilds gen2 with REAL payload (orphan not adopted).
        rc = run(worker, "publish", str(case), "1", "real")
        require(rc.returncode == 0, f"{name}: rebuild rc={rc.returncode} {rc.stderr}")
        st2 = load_state(worker, case)
        require(st2.get("state") == "available" and st2.get("generation") == 2 and
                st2.get("slot_payload_hex") == TAG_HEX["real"],
                f"{name}: rebuilt real gen2, got {st2}")
        # Residue cleaned: no gen-*.stage, no pointer.tmp under the checkpoint.
        ckpt = case / "store" / "wf-00000007" / "ckpt-0000000000000003"
        leftover_stage = list(ckpt.glob("gen-*.stage")) if ckpt.exists() else []
        require(not leftover_stage, f"{name}: stage residue left {leftover_stage}")
        require(not (ckpt / "pointer.tmp").exists(), f"{name}: pointer.tmp residue left")
        shutil.rmtree(case, ignore_errors=True)

    # ---- cross-process CAS via stdin handshake ------------------------------
    case = reset_case(base, "race")
    a = subprocess.Popen([worker, "race-publish", str(case), "0", "race-a"],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         text=True)
    b = subprocess.Popen([worker, "race-publish", str(case), "0", "race-b"],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         text=True)

    def wait_ready_both(procs: dict, timeout: float = 30.0) -> None:
        """Wait until BOTH children print CAS_READY, using selectors so a silent child
        cannot block past the deadline, and premature exit is detected."""
        sel = selectors.DefaultSelector()
        pending = {}
        for tag, proc in procs.items():
            sel.register(proc.stdout, selectors.EVENT_READ, tag)
            pending[tag] = proc
        deadline = time.monotonic() + timeout
        ready = set()
        while len(ready) < len(procs):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError(f"race timed out waiting for CAS_READY; ready={ready}")
            for key, _ in sel.select(timeout=remaining):
                tag = key.data
                line = key.fileobj.readline()
                if "CAS_READY" in line:
                    ready.add(tag)
                elif line == "":  # EOF -> child exited before ready
                    rc = pending[tag].poll()
                    raise AssertionError(f"race {tag} exited before ready rc={rc}")
            # also catch a child that died without closing stdout promptly
            for tag, proc in pending.items():
                if tag not in ready and proc.poll() is not None:
                    raise AssertionError(f"race {tag} exited before ready rc={proc.returncode}")
        sel.close()

    try:
        wait_ready_both({"a": a, "b": b})
        # Release both simultaneously.
        a.stdin.write("go\n"); a.stdin.flush()
        b.stdin.write("go\n"); b.stdin.flush()
        a_rc = a.wait(timeout=30)
        b_rc = b.wait(timeout=30)
        rcs = sorted([a_rc, b_rc])
        require(rcs == [0, 5],
                f"race: expected one exit0 + one exit5(GenerationMismatch), got {rcs}")
        winner_tag = "race-a" if a_rc == 0 else "race-b"
        st = load_state(worker, case)
        require(st.get("state") == "available" and st.get("generation") == 1 and
                st.get("slot_payload_hex") == TAG_HEX[winner_tag],
                f"race: single valid NEW gen1 with winner payload, got {st}")
    finally:
        for proc in (a, b):
            if proc.poll() is None:
                try:
                    proc.send_signal(signal.SIGKILL)
                except ProcessLookupError:
                    pass
            try:
                proc.communicate(timeout=10)
            except Exception:
                pass
            for stream in (proc.stdin, proc.stdout, proc.stderr):
                try:
                    if stream is not None:
                        stream.close()
                except Exception:
                    pass
    shutil.rmtree(case, ignore_errors=True)

    print("payload_store_crash_smoke: all checks passed")
    shutil.rmtree(base, ignore_errors=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        # Fail-fast: the failing case's directory is left in place for diagnosis.
        print(f"payload_store_crash_smoke: FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
