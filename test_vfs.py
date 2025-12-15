from utils import random_string


CHECKS = {
    'id': 2,
    'home': -2,
    'shell': -1,
}

def check_user(vfs, user):
    for prop, field in CHECKS.items():
        with open(vfs / user[0] / prop) as f:
            assert user[field] == f.read()


def test_vfs_users(kubsh, users, vfs):
    content = users.readlines()
    users = [line.strip().split(":") for line in content if line.endswith('sh\n')]

    vfs_users = [item for item in vfs.iterdir() if item.is_dir()]
    assert set([user.name for user in vfs_users]) == set([user[0] for user in users])

    for user in users:
        check_user(vfs, user)

def test_vfs_add_user(kubsh, vfs):
    username = random_string().lower()
    (vfs / username).mkdir(parents=True, exist_ok=True)
    import time
    time.sleep(0.5)
    user = None
    with open("/etc/passwd", "r") as f:
        for line in f:
            if line.startswith(username):
                user = line.strip().split(':')

    assert user is not None

    check_user(vfs, user)
