from pathlib import Path
import subprocess
from time import sleep

import pytest

from utils import debug

# PROGRAM = ["../shell/build/kubsh"]
PROGRAM = ["./kubsh"]


@pytest.fixture(scope="session", autouse=True)
def vfs():
    vfs_dir = Path(__file__).parent / "users"
    vfs_dir.mkdir(parents=True, exist_ok=True)
    return vfs_dir


@pytest.fixture()
def kubsh():
    process = subprocess.Popen(
        PROGRAM,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    sleep(0.1)

    yield process

    if process.returncode is None:
        stdout, stderr = process.communicate('')
        debug(stdout, stderr)
    sleep(0.1)

    assert process.returncode == 0

@pytest.fixture()
def users():
    with open("/etc/passwd", "r") as f:
        yield f
