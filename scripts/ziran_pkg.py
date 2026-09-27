#!/usr/bin/env python3
"""Source package and project driver for Ziran.

The compiler consumes a resolved module map. Git and TOML remain outside the
language frontend so bare compiler commands continue to work without a project.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import tomllib
from urllib.parse import urlparse


class PackageError(Exception):
    pass


BIN = Path(__file__).resolve().parent
SOURCE = BIN.parent if (BIN.parent / "Makefile").is_file() else BIN.parent.parent
CACHE = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "ziran"


def run(*args: str, cwd: Path | None = None, capture: bool = False) -> str:
    try:
        result = subprocess.run(
            args, cwd=cwd, check=True, text=True,
            stdout=subprocess.PIPE if capture else None,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise PackageError(f"command failed: {' '.join(args)}: {exc}") from exc
    return result.stdout.strip() if capture else ""


def manifest(path: Path) -> dict:
    try:
        with path.open("rb") as file:
            value = tomllib.load(file)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise PackageError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value.get("package"), dict):
        raise PackageError(f"{path}: missing [package]")
    name = value["package"].get("name")
    if not isinstance(name, str) or not name or not name.replace("_", "").replace("-", "").isalnum():
        raise PackageError(f"{path}: invalid package name")
    return value


def project_root() -> Path:
    for directory in (Path.cwd(), *Path.cwd().parents):
        if (directory / "ziran.toml").is_file():
            return directory
    raise PackageError("no ziran.toml found in this directory or its parents")


def public_git(url: str) -> None:
    parsed = urlparse(url)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password:
        if os.environ.get("ZIRAN_TEST_ALLOW_LOCAL_GIT") == "1" and parsed.scheme == "file":
            return
        raise PackageError(f"package Git URL must use public HTTPS: {url}")


def source_id(url: str, commit: str) -> str:
    return "p" + hashlib.sha256(f"{url}\n{commit}".encode()).hexdigest()[:16]


def is_commit(value: str) -> bool:
    return len(value) == 40 and all(ch in "0123456789abcdef" for ch in value)


def git_commit(url: str, ref: str) -> str:
    public_git(url)
    lines = run("git", "ls-remote", url, ref, capture=True).splitlines()
    commits = [line.split()[0] for line in lines if len(line.split()) == 2]
    if len(commits) != 1 or not is_commit(commits[0]):
        raise PackageError(f"cannot resolve a unique Git commit for {url} {ref}")
    return commits[0]


def cached_checkout(url: str, commit: str, offline: bool = False) -> Path:
    if not is_commit(commit):
        raise PackageError(f"invalid locked Git commit: {commit}")
    public_git(url)
    destination = CACHE / "sources" / source_id(url, commit)
    if destination.is_dir():
        actual = run("git", "-C", str(destination), "rev-parse", "HEAD", capture=True)
        if actual == commit:
            return destination
        raise PackageError(f"cache entry has the wrong commit: {destination}")
    if offline:
        raise PackageError(f"package {url}@{commit} is missing from the offline cache")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".fetch-", dir=destination.parent))
    try:
        run("git", "clone", "--no-checkout", url, str(temporary))
        run("git", "-C", str(temporary), "checkout", "--detach", commit)
        actual = run("git", "-C", str(temporary), "rev-parse", "HEAD", capture=True)
        if actual != commit:
            raise PackageError(f"fetched Git commit differs from lock: {url}")
        try:
            temporary.rename(destination)
        except FileExistsError:
            pass
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return destination


def lock_file(root: Path) -> dict:
    path = root / "ziran.lock"
    try:
        result = json.loads(path.read_text())
    except (OSError, ValueError) as exc:
        raise PackageError(f"cannot read {path}; run ziran lock: {exc}") from exc
    if result.get("format") != 1 or not isinstance(result.get("packages"), list):
        raise PackageError(f"{path}: unsupported lock format")
    return result


def dependency_specs(data: dict) -> dict[str, dict]:
    dependencies = data.get("dependencies", {})
    if not isinstance(dependencies, dict):
        raise PackageError("[dependencies] must be a table")
    for name, spec in dependencies.items():
        if not isinstance(name, str) or not name.isidentifier():
            raise PackageError(f"invalid dependency alias: {name}")
        if not isinstance(spec, dict) or not isinstance(spec.get("git"), str):
            raise PackageError(f"dependency {name} needs a Git source")
        public_git(spec["git"])
        if not isinstance(spec.get("ref", "master"), str):
            raise PackageError(f"dependency {name} has an invalid ref")
        aliases = spec.get("module_aliases", {})
        if not isinstance(aliases, dict) or not all(
            isinstance(old, str) and old.isidentifier() and
            isinstance(new, str) and new.isidentifier()
            for old, new in aliases.items()
        ):
            raise PackageError(f"dependency {name} has invalid module aliases")
    return dependencies


def resolve_lock(root: Path, refresh: str | None = None) -> dict:
    data = manifest(root / "ziran.toml")
    overrides = local_overrides(root)
    old = None
    try:
        old = lock_file(root)
    except PackageError:
        pass
    old_entries = {(item["url"], item["ref"]): item for item in old["packages"]} if old else {}
    if refresh not in (None, "*") and refresh not in dependency_specs(data):
        raise PackageError(f"unknown dependency: {refresh}")
    pending = [("root", alias, spec) for alias, spec in dependency_specs(data).items()]
    entries: dict[tuple[str, str], dict] = {}
    while pending:
        owner, alias, spec = pending.pop(0)
        url, ref = spec["git"], spec.get("ref", "master")
        key = (url, ref)
        if key not in entries:
            previous = old_entries.get(key)
            override = overrides.get(alias)
            if override is not None:
                commit = run("git", "-C", str(override), "rev-parse", "HEAD", capture=True)
                checkout = override
            else:
                commit = previous["commit"] if previous and refresh not in ("*", alias) else git_commit(url, ref)
                checkout = cached_checkout(url, commit)
            package_data = manifest(checkout / "ziran.toml")
            entries[key] = {
                "id": source_id(url, commit), "name": package_data["package"]["name"],
                "url": url, "ref": ref, "commit": commit, "dependencies": {},
            }
            for child_alias, child_spec in dependency_specs(package_data).items():
                pending.append((entries[key]["id"], child_alias, child_spec))
        if owner == "root":
            data.setdefault("_resolved", {})[alias] = entries[key]["id"]
        else:
            parent = next(item for item in entries.values() if item["id"] == owner)
            parent["dependencies"][alias] = entries[key]["id"]
    toolchain = data.get("toolchain", {})
    if not isinstance(toolchain, dict) or not isinstance(toolchain.get("git"), str):
        raise PackageError("[toolchain] needs a Git source")
    url, ref = toolchain["git"], toolchain.get("ref", "master")
    public_git(url)
    old_tool = old.get("toolchain") if old else None
    if "ziran" in overrides:
        commit = run("git", "-C", str(overrides["ziran"]), "rev-parse", "HEAD", capture=True)
    else:
        commit = old_tool["commit"] if old_tool and old_tool.get("url") == url and old_tool.get("ref") == ref and refresh != "*" else git_commit(url, ref)
    locked = {
        "format": 1,
        "toolchain": {"url": url, "ref": ref, "commit": commit},
        "root": {"name": data["package"]["name"], "dependencies": data.get("_resolved", {})},
        "packages": sorted(entries.values(), key=lambda item: item["id"]),
    }
    path = root / "ziran.lock"
    temporary = path.with_suffix(".lock.tmp")
    temporary.write_text(json.dumps(locked, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)
    return locked


def local_overrides(root: Path) -> dict[str, Path]:
    path = root / "ziran.local.toml"
    if not path.exists():
        return {}
    try:
        with path.open("rb") as file:
            data = tomllib.load(file)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise PackageError(f"cannot read {path}: {exc}") from exc
    overrides = data.get("overrides", {})
    if not isinstance(overrides, dict):
        raise PackageError("[overrides] must be a table")
    result = {}
    for name, value in overrides.items():
        if not isinstance(value, str):
            raise PackageError(f"override {name} must be a path")
        result[name] = (root / value).resolve()
        if not result[name].is_dir():
            raise PackageError(f"override {name} does not exist: {result[name]}")
    return result


def checkout_graph(root: Path, locked: dict, offline: bool, allow_override: bool) -> tuple[dict[str, Path], Path]:
    configured = local_overrides(root)
    if configured and not allow_override:
        raise PackageError("local overrides are not allowed with --locked")
    overrides = configured if allow_override else {}
    current = manifest(root / "ziran.toml")
    if current["package"]["name"] != locked["root"]["name"]:
        raise PackageError("package name differs from ziran.lock; run ziran lock")
    specs = dependency_specs(current)
    if set(specs) != set(locked["root"]["dependencies"]):
        raise PackageError("dependencies differ from ziran.lock; run ziran lock")
    by_id = {item["id"]: item for item in locked["packages"]}
    for alias, spec in specs.items():
        item = by_id[locked["root"]["dependencies"][alias]]
        if (item["url"], item["ref"]) != (spec["git"], spec.get("ref", "master")):
            raise PackageError(f"dependency {alias} differs from ziran.lock")
    tool_spec = current.get("toolchain", {})
    if (tool_spec.get("git"), tool_spec.get("ref", "master")) != (
        locked["toolchain"]["url"], locked["toolchain"]["ref"]
    ):
        raise PackageError("toolchain differs from ziran.lock")
    paths = {"root": root}
    for item in locked["packages"]:
        path = overrides.get(item["name"])
        paths[item["id"]] = path if path else cached_checkout(item["url"], item["commit"], offline)
        package_data = manifest(paths[item["id"]] / "ziran.toml")
        if package_data["package"]["name"] != item["name"]:
            raise PackageError(f"package name changed at {item['url']}@{item['commit']}")
        child_specs = dependency_specs(package_data)
        if set(child_specs) != set(item["dependencies"]):
            raise PackageError(f"package {item['name']} dependencies differ from ziran.lock")
        for alias, spec in child_specs.items():
            child = by_id[item["dependencies"][alias]]
            if (child["url"], child["ref"]) != (spec["git"], spec.get("ref", "master")):
                raise PackageError(f"package {item['name']} dependency {alias} differs from ziran.lock")
    tool = locked["toolchain"]
    toolchain = overrides.get("ziran")
    if toolchain is None:
        toolchain = cached_checkout(tool["url"], tool["commit"], offline)
    return paths, toolchain


def module_files(directory: Path, roots: list[str]) -> dict[str, Path]:
    found = {}
    for value in roots:
        path = (directory / value).resolve()
        if not path.is_relative_to(directory.resolve()) or not path.is_dir():
            raise PackageError(f"module root is unavailable: {directory / value}")
        for source in sorted(path.glob("*.zi")):
            # A package cannot acquire source from a neighboring checkout via
            # a symlink. Legacy links to std are resolved from the toolchain.
            if source.is_symlink() and not source.resolve().is_relative_to(directory.resolve()):
                continue
            name = source.stem
            if not name.isidentifier():
                raise PackageError(f"invalid module name: {source}")
            if name in found:
                raise PackageError(f"duplicate module {name} in {directory}")
            found[name] = source
        for source in sorted(path.glob("*/module.zi")):
            if source.is_symlink() and not source.resolve().is_relative_to(directory.resolve()):
                continue
            name = source.parent.name
            if not name.isidentifier():
                raise PackageError(f"invalid module name: {source}")
            if name in found:
                raise PackageError(f"duplicate module {name} in {directory}")
            found[name] = source
    return found


def module_map(root: Path, locked: dict, paths: dict[str, Path], toolchain: Path) -> Path:
    packages = {"root": (root, manifest(root / "ziran.toml"))}
    for item in locked["packages"]:
        packages[item["id"]] = (paths[item["id"]], manifest(paths[item["id"]] / "ziran.toml"))
    lines = []
    modules = {}
    for ident, (directory, data) in packages.items():
        lines.append(f"P\t{ident}\t{directory.resolve()}")
        roots = data["package"].get("module_roots", ["src"])
        if not isinstance(roots, list) or not all(isinstance(part, str) for part in roots):
            raise PackageError(f"{directory}: module_roots must be an array of paths")
        modules[ident] = module_files(directory, roots)
        for name, source in modules[ident].items():
            lines.append(f"M\t{ident}\t{name}\t{source.resolve()}")
    std = toolchain / "std"
    lines.append(f"P\tstd\t{toolchain.resolve()}")
    modules["std"] = module_files(toolchain, ["std"])
    for name, source in modules["std"].items():
        lines.append(f"M\tstd\t{name}\t{source.resolve()}")
    package_by_id = {item["id"]: item for item in locked["packages"]}
    for ident, (directory, data) in packages.items():
        edges = locked["root"]["dependencies"] if ident == "root" else package_by_id[ident]["dependencies"]
        visible = {}
        for alias, target_id in sorted(edges.items()):
            target_data = packages[target_id][1]
            exports = target_data.get("exports", {})
            if not isinstance(exports, dict):
                raise PackageError(f"{directory}: [exports] must be a table")
            aliases = dependency_specs(data)[alias].get("module_aliases", {})
            if not isinstance(aliases, dict):
                raise PackageError(f"{directory}: module_aliases must be a table")
            if set(aliases) - set(exports):
                raise PackageError(f"{directory}: module_aliases names a module {alias} does not export")
            for exported, path in exports.items():
                if exported not in modules[target_id] or not isinstance(path, str) or modules[target_id][exported] != (packages[target_id][0] / path).resolve():
                    raise PackageError(f"{packages[target_id][0]}: invalid export {exported}")
                visible_name = aliases.get(exported, exported)
                if visible_name in modules[ident]:
                    raise PackageError(f"module {visible_name} in {directory} conflicts with dependency {alias}; set module_aliases")
                if visible_name in visible:
                    raise PackageError(f"module {visible_name} is exported by both {visible[visible_name]} and {alias}; set module_aliases")
                visible[visible_name] = alias
                lines.append(f"D\t{ident}\t{visible_name}\t{target_id}\t{exported}")
    bridges = packages["root"][1]["package"].get("bridge_modules", [])
    if not isinstance(bridges, list) or not all(
        isinstance(name, str) and name.isidentifier() and name in modules["root"]
        for name in bridges
    ):
        raise PackageError("bridge_modules must name root package modules")
    for target_id in locked["root"]["dependencies"].values():
        for name in bridges:
            if name in modules[target_id]:
                raise PackageError(f"bridge module {name} conflicts with {target_id}")
            lines.append(f"D\t{target_id}\t{name}\troot\t{name}")
    generated = root / "build" / ".ziran"
    generated.mkdir(parents=True, exist_ok=True)
    output = generated / "module-map.tsv"
    output.write_text("\n".join(lines) + "\n")
    return output


def select_package(locked: dict, selector: str) -> dict:
    packages = {item["id"]: item for item in locked["packages"]}
    direct = locked["root"]["dependencies"]
    if selector in direct:
        return packages[direct[selector]]
    matches = [item for item in locked["packages"] if item["name"] == selector]
    direct_matches = [item for item in matches if item["id"] in direct.values()]
    if len(direct_matches) == 1:
        return direct_matches[0]
    if len(matches) == 1:
        return matches[0]
    if matches:
        raise PackageError(f"package {selector} has multiple locked revisions; use a direct dependency alias")
    raise PackageError(f"package {selector} is not locked")


def dispatch_native(command: str, arguments: list[str], toolchain: Path, map_path: Path | None) -> int:
    binaries = {"check": "zi2zir", "ir": "zi2zir", "inspect": "zi-inspect",
                "fmt": "zi-fmt", "bundle": "zi2zib", "run": "zi2zib"}
    extra = []
    if command == "build":
        target = next((arg for arg in arguments if arg.startswith("--target=")), None)
        if target not in ("--target=c", "--target=cpp", "--target=go"):
            raise PackageError("build needs --target=c|cpp|go")
        executable = {"--target=c": "zi2c", "--target=cpp": "zi2cpp", "--target=go": "zi2go"}[target]
        arguments = [arg for arg in arguments if arg != target]
        extra = []
    else:
        executable = binaries.get(command)
    if executable is None:
        raise PackageError(f"unknown Ziran command: {command}")
    if command == "check":
        extra.append("--check-only")
    if command in ("bundle", "run"):
        extra.insert(0, command)
    binary = toolchain / "build" / "bin" / executable
    if not binary.is_file():
        run("make", "-C", str(toolchain), "all")
    environment = os.environ.copy()
    if map_path:
        environment["ZIRAN_PACKAGE_MAP"] = str(map_path)
    else:
        environment.pop("ZIRAN_PACKAGE_MAP", None)
    return subprocess.run([str(binary), *extra, *arguments], env=environment).returncode


def main(argv: list[str]) -> int:
    if not argv or argv[0] in ("help", "--help", "-h"):
        print("usage: ziran lock|update|fetch|add|pkg|tool|check|ir|build|bundle|run|fmt|inspect ...")
        print("project commands: --project [--locked] [--offline]")
        print("ziran tool PACKAGE run|build|check [--profile NAME]")
        return 0
    command, *arguments = argv
    if command in ("lock", "update", "fetch", "add", "tool", "pkg"):
        root = project_root()
        if command == "lock":
            resolve_lock(root)
            return 0
        if command == "update":
            if len(arguments) > 1:
                raise PackageError("usage: ziran update [PACKAGE]")
            resolve_lock(root, arguments[0] if arguments else "*")
            return 0
        if command == "add":
            parser = argparse.ArgumentParser(prog="ziran add")
            parser.add_argument("name")
            parser.add_argument("--git", required=True)
            parser.add_argument("--ref", default="master")
            options = parser.parse_args(arguments)
            public_git(options.git)
            if not options.name.isidentifier() or not options.ref or any(
                ch in options.ref for ch in "\r\n\t\0"
            ):
                raise PackageError("invalid dependency name or Git ref")
            path = root / "ziran.toml"
            data = manifest(path)
            if options.name in dependency_specs(data):
                raise PackageError(f"dependency {options.name} already exists")
            before = path.read_text()
            path.write_text(before +
                f'\n[dependencies.{options.name}]\n'
                f'git = {json.dumps(options.git)}\n'
                f'ref = {json.dumps(options.ref)}\n')
            try:
                resolve_lock(root)
            except Exception:
                path.write_text(before)
                raise
            return 0
        locked_mode = "--locked" in arguments
        offline = "--offline" in arguments
        arguments = [value for value in arguments if value not in ("--locked", "--offline")]
        locked = lock_file(root)
        paths, toolchain = checkout_graph(root, locked, offline=offline, allow_override=not locked_mode)
        if command == "fetch":
            return 0
        if command == "pkg":
            if len(arguments) != 2 or arguments[0] != "path":
                raise PackageError("usage: ziran pkg path PACKAGE")
            if arguments[1] == "ziran":
                print(toolchain)
                return 0
            match = select_package(locked, arguments[1])
            print(paths[match["id"]])
            return 0
        if command == "tool":
            if len(arguments) < 2:
                raise PackageError("usage: ziran tool PACKAGE COMMAND [OPTIONS]")
            name, *tool_args = arguments
            match = select_package(locked, name)
            package_root = paths[match["id"]]
            package_data = manifest(package_root / "ziran.toml")
            relative_tool = package_data.get("tool", {}).get("project")
            if not isinstance(relative_tool, str) or Path(relative_tool).is_absolute() or ".." in Path(relative_tool).parts:
                raise PackageError(f"package {name} has no declared project tool")
            binary = package_root / relative_tool
            if not binary.is_file():
                run("make", "-C", str(package_root), relative_tool,
                    f"ZIRAN_DIR={toolchain}")
            environment = os.environ.copy()
            environment["ZIRAN_PROJECT_ROOT"] = str(root)
            environment["ZIRAN_PACKAGE_ROOT"] = str(package_root)
            environment["ZIRAN_PACKAGE_ID"] = match["id"]
            environment["ZIRAN_TOOLCHAIN_ROOT"] = str(toolchain)
            return subprocess.run([str(binary), *tool_args], env=environment, cwd=root).returncode
    project = "--project" in arguments
    locked_mode = "--locked" in arguments
    offline = "--offline" in arguments
    arguments = [arg for arg in arguments if arg not in ("--project", "--locked", "--offline")]
    if project:
        if "--module-path" in arguments:
            raise PackageError("--module-path is for standalone commands; project imports use ziran.toml")
        root = project_root()
        locked = lock_file(root)
        paths, toolchain = checkout_graph(root, locked, offline, not locked_mode)
        if not (toolchain / "build" / "bin" / "zi2zir").is_file():
            run("make", "-C", str(toolchain), "all")
        map_path = module_map(root, locked, paths, toolchain)
        if "--root" not in arguments:
            arguments = ["--root", str(root / "src"), *arguments]
        if not any(not part.startswith("-") and part.endswith((".zi", ".zir")) for part in arguments) and command in ("check", "ir", "build", "bundle"):
            entry = manifest(root / "ziran.toml")["package"].get("entry", "src/app.zi")
            arguments.append(str(root / entry))
        return dispatch_native(command, arguments, toolchain, map_path)
    return dispatch_native(command, arguments, SOURCE, None)


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except PackageError as exc:
        print(f"ziran: {exc}", file=sys.stderr)
        sys.exit(2)
