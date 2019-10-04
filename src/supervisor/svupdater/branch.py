from uci import Uci, UciExceptionNotFound


def get_os_branch_or_version():
    """Get OS branch or version from uci."""
    with Uci() as uci:
        try:
            branch = uci.get("updater", "override", "branch")
        except (UciExceptionNotFound, KeyError):
            branch = "deploy"

        return {"mode": "branch", "value": branch}
