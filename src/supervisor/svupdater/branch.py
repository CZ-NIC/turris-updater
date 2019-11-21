from euci import EUci, UciExceptionNotFound


def get_os_branch_or_version():
    """Get OS branch or version from uci."""
    with EUci() as uci:
        branch = uci.get("updater", "override", "branch", default="deploy")
        # TOS 3.x can only switch between branches, therefore mode is fixed on branch
        mode = "branch"

        return mode, branch
