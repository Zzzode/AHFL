#!/usr/bin/env python3

from __future__ import annotations

import os
import signal
import subprocess
import sys
import tempfile
import time
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
from unittest.mock import patch


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    require(
        len(sys.argv) == 3,
        "usage: production_confidence_ci_only_smoke.py <long-soak-script> <repo-root>",
    )
    harness = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    require(harness.is_file(), f"long-soak harness missing: {harness}")
    spec = spec_from_file_location("reference_workflow_long_soak", harness)
    require(spec is not None and spec.loader is not None, "cannot import long-soak harness")
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    revision = module.compute_source_revision(repo)
    ci_environment = {
        "CI": "true",
        "GITHUB_ACTIONS": "true",
        "GITHUB_REPOSITORY": "Zzzode/AHFL",
        "GITHUB_WORKFLOW_REF": (
            "Zzzode/AHFL/.github/workflows/production-confidence.yml@refs/heads/develop"
        ),
        "GITHUB_EVENT_NAME": "workflow_dispatch",
        "GITHUB_SHA": revision,
        "GITHUB_RUN_ID": "123456789",
        "GITHUB_RUN_ATTEMPT": "1",
        "GITHUB_JOB": "hour-scale-soak",
        "RUNNER_OS": "Linux",
        "RUNNER_ARCH": "X64",
    }
    with patch.dict(os.environ, ci_environment, clear=True):
        provenance = module.github_actions_provenance(repo, revision)
    require(provenance["kind"] == "ci", "GitHub Actions provenance kind drifted")
    require(
        provenance["provider"] == "github-actions",
        "GitHub Actions provenance provider drifted",
    )
    require(provenance["commit_sha"] == revision, "GitHub Actions revision drifted")

    with tempfile.TemporaryDirectory(prefix="ahfl-local-hour-scale-") as temp_dir:
        temp = Path(temp_dir)
        marker = temp / "worker-started"
        worker = temp / "forbidden-worker.py"
        worker.write_text(
            "#!/usr/bin/env python3\n"
            "from pathlib import Path\n"
            "import time\n"
            f"Path({str(marker)!r}).write_text('started\\n', encoding='utf-8')\n"
            "time.sleep(30)\n",
            encoding="utf-8",
        )
        worker.chmod(0o755)
        evidence = temp / "hour-scale.json"
        work = temp / "work"
        environment = {
            key: value
            for key, value in os.environ.items()
            if key != "CI"
            and key != "GITHUB_ACTIONS"
            and not key.startswith("GITHUB_")
            and not key.startswith("RUNNER_")
        }
        started = time.monotonic()
        process = subprocess.Popen(
            [
                sys.executable,
                str(harness),
                "--worker",
                str(worker),
                "--repo-root",
                str(repo),
                "--work-dir",
                str(work),
                "--minimum-seconds",
                "3600",
                "--minimum-iterations",
                "100",
                "--contract-kind",
                "hour-scale",
                "--evidence-path",
                str(evidence),
            ],
            cwd=repo,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
        try:
            stdout, stderr = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.communicate()
            raise AssertionError("local hour-scale soak started instead of failing immediately")

        elapsed = time.monotonic() - started
        require(process.returncode != 0, "local hour-scale soak unexpectedly succeeded")
        require(elapsed < 5, "local hour-scale rejection was not immediate")
        require(
            "hour-scale soak is CI-only" in stdout + stderr,
            f"local rejection did not explain the CI-only policy:\n{stdout}\n{stderr}",
        )
        require(not marker.exists(), "local hour-scale soak started the worker")
        require(not work.exists(), "local hour-scale soak created a work directory")
        require(not evidence.exists(), "local hour-scale soak wrote release evidence")

    print("production confidence CI-only smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
