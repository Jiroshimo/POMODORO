#!/usr/bin/env python3
"""Supply missing PulseAudio client libraries locally on Ubuntu/Debian.

Packages are checked against the system's authenticated apt SHA256 metadata.
No system packages or settings are changed; files stay in the build directory.
"""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.DEVNULL).strip()


def installed(package):
    try:
        return run("dpkg-query", "-W", "-f=${db:Status-Status}", package) == "installed"
    except subprocess.CalledProcessError:
        return False


def metadata(package):
    text = run("apt-cache", "show", "--no-all-versions", package)
    return dict(line.split(": ", 1) for line in text.split("\n\n", 1)[0].splitlines()
                if ": " in line and not line.startswith(" "))


def main():
    root = Path(sys.argv[1]).resolve()
    arch = run("dpkg-architecture", "-qDEB_HOST_MULTIARCH")
    library_dir = root / "usr/lib" / arch
    paths = [str(library_dir), str(library_dir / "pulseaudio")]
    environment = dict(os.environ)
    existing_path = environment.get("LD_LIBRARY_PATH")
    environment["LD_LIBRARY_PATH"] = ":".join(paths + ([existing_path] if existing_path else []))
    check = subprocess.run([sys.executable, "-c", "import ctypes; ctypes.CDLL('libpulse-simple.so.0')"],
                           env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if check.returncode == 0:
        return
    distro = Path("/etc/os-release").read_text()
    if re.search(r'^ID=ubuntu$', distro, re.M):
        machine = run("dpkg", "--print-architecture")
        mirror = "https://archive.ubuntu.com/ubuntu/" if machine in ("amd64", "i386") else "https://ports.ubuntu.com/ubuntu-ports/"
    elif re.search(r'^ID=debian$', distro, re.M):
        mirror = "https://deb.debian.org/debian/"
    else:
        raise RuntimeError("Install your distribution's PulseAudio client runtime.")
    root.mkdir(parents=True, exist_ok=True)
    cache = root / "packages"
    cache.mkdir(exist_ok=True)
    pending = ["libpulse0"]
    seen = set()
    packages = []
    while pending:
        package = pending.pop(0)
        if package in seen:
            continue
        seen.add(package)
        if installed(package):
            continue
        info = metadata(package)
        if not info.get("Filename") or not info.get("SHA256"):
            raise RuntimeError(f"No apt download metadata for {package}; run sudo apt update.")
        for dependency in (info.get("Pre-Depends", "") + "," + info.get("Depends", "")).split(","):
            alternatives = re.findall(r'(?:^|\|)\s*([a-z0-9][a-z0-9+.-]*)', dependency)
            if alternatives and not any(installed(name) for name in alternatives):
                pending.append(alternatives[0])
        packages.append((package, info))
    for package, info in packages:
        target = cache / Path(info["Filename"]).name
        if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != info["SHA256"]:
            print(f"Setting up local audio: {package}", flush=True)
            subprocess.run(["curl", "-fsSL", "--connect-timeout", "10", "--max-time", "30",
                            mirror + info["Filename"], "-o", str(target)], check=True)
        if hashlib.sha256(target.read_bytes()).hexdigest() != info["SHA256"]:
            target.unlink()
            raise RuntimeError(f"Package checksum mismatch: {package}")
        subprocess.run(["dpkg-deb", "-x", str(target), str(root)], check=True)
    subprocess.run([sys.executable, "-c", "import ctypes; ctypes.CDLL('libpulse-simple.so.0')"],
                   env=environment, check=True)
    print("Local audio runtime ready.", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Audio runtime setup: {error}\nAlternatively, install libpulse0 with your package manager.", file=sys.stderr)
        sys.exit(1)
