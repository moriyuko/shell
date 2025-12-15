import random
import string


def debug(stdout, stderr):
    print(stdout.strip())
    print("---")
    print(stderr.strip())


def random_string(length: int = 12):
    characters = string.ascii_letters
    random_string = ''.join(random.choice(characters) for _ in range(length))
    return random_string
