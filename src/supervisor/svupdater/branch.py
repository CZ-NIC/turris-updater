from uci import Uci, UciExceptionNotFound


def get_os_branch_or_version():
    """Get OS branch or version from uci."""
    with Uci() as uci:
        try:
            branch = uci.get("updater", "override", "branch")
        except (UciExceptionNotFound, KeyError):
            branch = "deploy"
        # TOS 3.x can only switch between branches, therefore mode is fixed on branch
        mode = "branch"

        return mode, branch
