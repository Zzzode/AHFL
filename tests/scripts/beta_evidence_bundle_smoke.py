#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def write_evidence(path: Path, schema: str, criterion: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(
            {
                "schema": schema,
                "status": "passed",
                "criterion": criterion,
                "source_revision": "bundle-smoke-revision",
            }
        )
        + "\n",
        encoding="utf-8",
    )


def write_fake_ctest(path: Path) -> None:
    path.write_text(
        """#!/usr/bin/env python3
import json
import os
import sys
from pathlib import Path

root = Path(os.environ["AHFL_BUNDLE_SMOKE_ROOT"])
sys.path.insert(0, str(root / "scripts"))

from ahfl_source_revision import compute_source_revision

out = root / "build/release-evidence/beta"
revision = compute_source_revision(root)
values = (
    ("run-profiles.json", "ahfl.beta-evidence.run-profiles.v1", "BETA-01"),
    (
        "reference-workflow-recovery.json",
        "ahfl.beta-evidence.reference-workflow-recovery.v1",
        "BETA-07",
    ),
)
for name, schema, criterion in values:
    path = out / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({
        "schema": schema,
        "status": "passed",
        "criterion": criterion,
        "source_revision": revision,
    }) + "\\n")
print("fake ctest passed")
""",
        encoding="utf-8",
    )
    path.chmod(0o755)


def write_fake_generator(path: Path, outputs: list[tuple[str, str, str]]) -> None:
    path.write_text(
        f"""#!/usr/bin/env python3
import json
import sys
from pathlib import Path

args = sys.argv[1:]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from ahfl_source_revision import compute_source_revision

if "--out-dir" in args:
    out = Path(args[args.index("--out-dir") + 1])
else:
    out = Path(args[args.index("--out") + 1]).parent
root_flag = "--repo-root" if "--repo-root" in args else "--root"
repo = Path(args[args.index(root_flag) + 1])
revision = compute_source_revision(repo)
outputs = {outputs!r}
for name, schema, criterion in outputs:
    path = out / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({{
        "schema": schema,
        "status": "passed",
        "criterion": criterion,
        "source_revision": revision,
    }}) + "\\n")
""",
        encoding="utf-8",
    )


def prepare_root(source: Path, root: Path) -> None:
    (root / "config").mkdir(parents=True)
    (root / "scripts").mkdir()
    shutil.copy2(source / "config/beta-gate.json", root / "config/beta-gate.json")
    shutil.copy2(source / "scripts/check-beta-gate.py", root / "scripts/check-beta-gate.py")
    shutil.copy2(
        source / "scripts/ahfl_source_revision.py",
        root / "scripts/ahfl_source_revision.py",
    )
    write_fake_generator(
        root / "scripts/generate-beta-runtime-evidence.py",
        [
            (
                "runtime-identity.json",
                "ahfl.beta-evidence.runtime-identity.v1",
                "BETA-02",
            ),
            (
                "event-projections.json",
                "ahfl.beta-evidence.event-projections.v1",
                "BETA-03",
            ),
            (
                "product-scope-freeze.json",
                "ahfl.beta-evidence.product-scope-freeze.v1",
                "BETA-10",
            ),
        ],
    )
    generators = (
        (
            "generate-beta-lifecycle-evidence.py",
            "lifecycle-matrix.json",
            "ahfl.beta-evidence.lifecycle-matrix.v1",
            "BETA-04",
        ),
        (
            "generate-beta-formatter-evidence.py",
            "formatter-idempotence.json",
            "ahfl.beta-evidence.formatter-idempotence.v1",
            "BETA-05",
        ),
        (
            "generate-beta-stdlib-container-evidence.py",
            "stdlib-container-migration.json",
            "ahfl.beta-evidence.stdlib-container-migration.v1",
            "BETA-06",
        ),
        (
            "generate-beta-install-evidence.py",
            "install-smoke.json",
            "ahfl.beta-evidence.install-smoke.v1",
            "BETA-08",
        ),
        (
            "check-readme-capabilities.py",
            "readme-capabilities.json",
            "ahfl.beta-evidence.readme-capabilities.v1",
            "BETA-09",
        ),
    )
    for script, name, schema, criterion in generators:
        write_fake_generator(root / "scripts" / script, [(name, schema, criterion)])


def initialize_repository(root: Path) -> None:
    (root / ".gitignore").write_text("build/\n", encoding="utf-8")
    empty_template = root / "empty-git-template"
    empty_template.mkdir()
    commands = (
        ["git", "init", "-q", f"--template={empty_template}"],
        ["git", "config", "user.name", "AHFL Test"],
        ["git", "config", "user.email", "ahfl-test@example.invalid"],
        ["git", "add", "."],
        [
            "git",
            "commit",
            "--no-verify",
            "-qm",
            "test: initialize beta evidence bundle fixture",
        ],
    )
    for command in commands:
        subprocess.run(command, cwd=root, check=True, capture_output=True, text=True)


def run_bundle(
    generator: Path, root: Path, build: Path, fake_bin: Path
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["PATH"] = str(fake_bin) + os.pathsep + env["PATH"]
    env["AHFL_BUNDLE_SMOKE_ROOT"] = str(root)
    return subprocess.run(
        [
            sys.executable,
            str(generator),
            "--repo-root",
            str(root),
            "--build-dir",
            str(build),
        ],
        check=False,
        capture_output=True,
        text=True,
        env=env,
    )


def main() -> int:
    require(
        len(sys.argv) == 3,
        "usage: beta_evidence_bundle_smoke.py <bundle-generator> <repo-root>",
    )
    generator = Path(sys.argv[1]).resolve()
    source = Path(sys.argv[2]).resolve()
    require(generator.is_file(), f"beta evidence bundle generator missing: {generator}")

    with tempfile.TemporaryDirectory(prefix="ahfl-beta-bundle-") as temp_dir:
        temp = Path(temp_dir)
        root = temp / "repo"
        build = root / "build/dev"
        fake_bin = temp / "bin"
        build.mkdir(parents=True)
        fake_bin.mkdir()
        prepare_root(source, root)
        write_fake_ctest(fake_bin / "ctest")
        initialize_repository(root)

        out = root / "build/release-evidence/beta"
        out.mkdir(parents=True)
        stale = out / "stale-evidence.json"
        stale.write_text("{}\n", encoding="utf-8")

        result = run_bundle(generator, root, build, fake_bin)
        require(
            result.returncode == 0,
            f"complete evidence bundle failed:\n{result.stdout}\n{result.stderr}",
        )
        require(not stale.exists(), "bundle generator retained stale evidence")
        report = json.loads(result.stdout)
        require(report["schema"] == "ahfl.beta-gate-report.v1", "wrong final report")
        require(report["status"] == "ready", "complete bundle was not ready")
        require(len(list(out.glob("*.json"))) == 10, "bundle did not produce ten evidence files")

        (root / "scripts/check-readme-capabilities.py").write_text(
            "#!/usr/bin/env python3\n", encoding="utf-8"
        )
        result = run_bundle(generator, root, build, fake_bin)
        require(result.returncode != 0, "bundle accepted a missing BETA-09 artifact")
        require(
            not (out / "readme-capabilities.json").exists(),
            "failed regeneration reused stale BETA-09 evidence",
        )

    print("beta evidence bundle smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
