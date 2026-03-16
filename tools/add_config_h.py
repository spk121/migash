import os
import stat

INSERT_BLOCK = [
    '#ifdef HAVE_CONFIG_H',
    '#include "config.h"',
    '#endif',
    ''
]

def first_noncomment_nonwhitespace(lines):
    """
    Returns index of the first line that is not:
      - empty/whitespace
      - a // comment
      - inside a /* ... */ block comment
    """
    in_block_comment = False

    for idx, line in enumerate(lines):
        stripped = line.strip()

        # Handle block comments
        if in_block_comment:
            if "*/" in stripped:
                in_block_comment = False
            continue

        if stripped.startswith("/*"):
            if "*/" not in stripped:
                in_block_comment = True
            continue

        # Skip single-line comments
        if stripped.startswith("//"):
            continue

        # Skip empty lines
        if stripped == "":
            continue

        return idx

    return None


def process_file(path):
    with open(path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    idx = first_noncomment_nonwhitespace(lines)
    if idx is None:
        return False, None  # nothing to do

    # Check if the block already appears anywhere
    has_config_block = any(
        line.strip() == "#ifdef HAVE_CONFIG_H" for line in lines
    )

    # If present but not at the correct location → nastygram
    if has_config_block:
        if lines[idx].strip() != "#ifdef HAVE_CONFIG_H":
            return False, "misplaced"
        else:
            return False, None  # already correct

    # Otherwise insert the block
    new_lines = (
        lines[:idx]
        + [l + "\n" if not l.endswith("\n") else l for l in INSERT_BLOCK]
        + lines[idx:]
    )

    with open(path, "w", encoding="utf-8") as f:
        f.writelines(new_lines)

    return True, None


def main():
    changed = []
    nasty = []

    for filename in os.listdir("."):
        # Skip dotfiles
        if filename.startswith("."):
            continue

        # Skip non-.c files
        if not filename.endswith(".c"):
            continue

        # Skip symbolic links
        try:
            st = os.lstat(filename)
            if stat.S_ISLNK(st.st_mode):
                continue
        except OSError:
            continue

        modified, status = process_file(filename)
        if status == "misplaced":
            nasty.append(filename)
        elif modified:
            changed.append(filename)

    if nasty:
        print("Files with misplaced HAVE_CONFIG_H block:")
        for f in nasty:
            print("  ", f)

    if changed:
        print("Modified files (block inserted):")
        for f in changed:
            print("  ", f)

    if not nasty and not changed:
        print("No changes needed.")


if __name__ == "__main__":
    main()
