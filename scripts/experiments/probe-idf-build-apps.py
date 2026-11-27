"""Exercise idf-build-apps 3.1.1 against generated EspHub ESP32-S3 apps."""

from argparse import Namespace
import json
from pathlib import Path
import runpy
import subprocess
import sys
from tempfile import TemporaryDirectory
from time import monotonic
from unittest.mock import patch

from idf_build_apps.app import BuildStatus, CMakeApp
from idf_build_apps.manifest.manifest import Manifest

ROOT = Path.cwd()
SAMPLES = (
    ("m5stack/atoms3", "display-shapes"),
    ("m5stack/atoms3", "buttons"),
    ("waveshare/esp32-s3-zero-n8r8", "rgb-blink"),
)
FIRMWARE = runpy.run_path("scripts/build-example.py")


class ProjectPrepared(Exception):
    pass


def prepare(board_id, example_id, workspace):
    workspace.mkdir()
    board_directory = ROOT / "firmware/board" / board_id
    board_file = board_directory / "board.json"
    board = json.loads(board_file.read_text())
    example = next(entry for entry in board["examples"] if entry["id"] == example_id)
    source_file = board_directory / example["entrypoint"]
    settings = {}
    run = subprocess.run

    def capture(command, *args, **kwargs):
        if command[0] != "idf.py":
            return run(command, *args, **kwargs)
        settings.update(arg[2:].split("=", 1) for arg in command if str(arg).startswith("-D"))
        raise ProjectPrepared

    with patch("subprocess.run", side_effect=capture):
        try:
            FIRMWARE["build"](
                Namespace(board=board_id, app_size_bytes=None, version="2.0.1", out=workspace / "out"),
                board, example, workspace,
            )
        except ProjectPrepared:
            pass
        else:
            raise RuntimeError("Production project generator did not call idf.py")
    return workspace / "project", settings, board_file, source_file


def make_app(project, case, settings):
    return CMakeApp(
        str(project), "esp32s3", build_dir=case, build_log_filename=f"{case}.log",
        cmake_vars=settings, preserve=True,
    )


def report(case, label, app, seconds, commands=()):
    print("PROBE_RESULT " + json.dumps({
        "case": case, "app": label, "status": app.build_status.value,
        "comment": app.build_comment, "commands": list(commands), "seconds": round(seconds, 2),
    }), flush=True)


def plan(case, label, app, modified_files, modified_components):
    start = monotonic()
    app.check_should_build(
        check_app_dependencies=True, manifest_rootpath=str(ROOT),
        modified_files=[str(file) for file in modified_files],
        modified_components=modified_components,
    )
    report(case, label, app, monotonic() - start)
    return app.build_status


def execute(case, label, app, modified_components):
    start = monotonic()
    from idf_build_apps.app import subprocess_run
    commands = []

    def trace(command, *args, **kwargs):
        commands.append({"action": command[-1], "eh_options": [part for part in command if str(part).startswith("-DEH_")]})
        return subprocess_run(command, *args, **kwargs)

    with patch("idf_build_apps.app.subprocess_run", side_effect=trace):
        app.build(check_app_dependencies=True, manifest_rootpath=str(ROOT), modified_components=modified_components)
    report(case, label, app, monotonic() - start, commands)
    if app.build_status == BuildStatus.FAILED:
        print(Path(app.build_log_path).read_text(errors="replace")[-8000:], flush=True)
    return app.build_status, [command["action"] for command in commands]


with TemporaryDirectory(prefix="esphub-idf-apps-") as temp:
    root = Path(temp)
    projects = [prepare(board, example, root / str(i)) for i, (board, example) in enumerate(SAMPLES)]
    changed = projects[0][3]
    sdk_source = ROOT / "firmware/components/eh_agent/CMakeLists.txt"
    for label, (project, settings, _, _) in zip(SAMPLES, projects):
        status = plan("raw file only", label, make_app(project, "raw", settings), [changed], [])
        assert status == BuildStatus.SKIPPED
    if "--plan-only" in sys.argv:
        sys.exit(0)

    project, settings, _, _ = projects[0]
    status, commands = execute("SDK auto, stock arguments", SAMPLES[0], make_app(project, "stock", settings), ["eh_agent"])
    assert (status, commands) == (BuildStatus.FAILED, ["reconfigure"])

    manifest = root / "manifest.json"
    rules = {}
    for project, _, board, source in projects:
        rules[str(project)] = {"depends_filepatterns": [
            str(source.parent.relative_to(ROOT) / "**"),
            str(board.relative_to(ROOT)),
            "firmware/app/**",
            "firmware/components/eh_agent/**",
        ]}
    manifest.write_text(json.dumps(rules))
    CMakeApp.MANIFEST = Manifest.from_files([str(manifest)], root_path=str(ROOT))
    for i, (label, (project, settings, _, _)) in enumerate(zip(SAMPLES, projects)):
        expected = BuildStatus.SHOULD_BE_BUILT if i == 0 else BuildStatus.SKIPPED
        assert plan("manifest, one example", label, make_app(project, "manifest-example", settings), [changed], []) == expected
        assert plan("manifest, shared SDK", label, make_app(project, "manifest-sdk", settings), [sdk_source], []) == BuildStatus.SHOULD_BE_BUILT
    app = make_app(projects[0][0], "manifest-real-build", projects[0][1])
    assert plan("manifest, actual build selection", SAMPLES[0], app, [changed], []) == BuildStatus.SHOULD_BE_BUILT
    assert execute("manifest, actual build", SAMPLES[0], app, []) == (BuildStatus.SUCCESS, ["build"])
    description = json.loads((Path(app.build_path) / "project_description.json").read_text())
    assert (Path(app.build_path) / description["app_bin"]).read_bytes()[0] == 0xE9

    CMakeApp.MANIFEST = None
    for label, (project, settings, _, _) in zip(SAMPLES, projects):
        # The dependency preflight omits cmake_vars. Seed the generated project's
        # CMake variables so the preflight can discover the actual component graph.
        cmake = project / "CMakeLists.txt"
        initial = "".join(f'set({key} "{value}" CACHE STRING "")\n' for key, value in settings.items())
        cmake.write_text(initial + cmake.read_text())
        app = make_app(project, "component-check", settings)
        assert execute("irrelevant component, automatic", label, app, ["unrelated_component"]) == (
            BuildStatus.SKIPPED, ["reconfigure"],
        )
        app = make_app(project, "sdk-check", settings)
        assert execute("shared SDK, automatic", label, app, ["eh_agent"]) == (
            BuildStatus.SUCCESS, ["reconfigure", "build"],
        )
