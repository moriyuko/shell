import os
import signal
from time import sleep

from utils import random_string, debug


def test_command_not_found(kubsh):
    random_input = random_string()
    stdout, stderr = kubsh.communicate(input=f"{random_input}\n")
    debug(stdout, stderr)

    assert f"{random_input}: command not found" in stdout.strip()


def test_exit(kubsh):
    stdout, stderr = kubsh.communicate(input="\\q\n")
    debug(stdout, stderr)


def test_echo(kubsh):
    random_input = random_string()
    stdout, stderr = kubsh.communicate(input=f"debug '{random_input}'\n")
    debug(stdout, stderr)

    lines = stdout.splitlines()
    assert random_input in lines

def test_env(kubsh):
    shell = os.environ.get("HOME")
    stdout, stderr = kubsh.communicate(input="\\e $HOME\n")
    debug(stdout, stderr)

    lines = stdout.splitlines()
    assert shell in lines

def test_env_list(kubsh):
    path = os.environ.get("PATH")
    paths = path.split(":")
    stdout, stderr = kubsh.communicate(input="\\e $PATH\n")
    debug(stdout, stderr)

    lines = stdout.splitlines()
    assert set(paths) <= set(lines)

def test_signal(kubsh):
    sleep(0.1)
    kubsh.send_signal(signal.SIGHUP)
    sleep(0.1)

    kubsh.stdin.write("\\q\n")
    kubsh.stdin.flush()

    stdout, stderr = kubsh.communicate()
    debug(stdout, stderr)
    assert "Configuration reloaded" in stdout


def test_command(kubsh, users):
    content = users.read()
    stdout, stderr = kubsh.communicate(input="cat /etc/passwd\n")
    debug(stdout, stderr)

    assert content in stdout
