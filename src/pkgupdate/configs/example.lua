--[[
This is example configuration file. You can use it as quick reference for your own
configurations.
You can create any additional file in same directory as this file
(/etc/updater/conf.d) and use it for your configuration.

Note that this is Lua 5.1 so you can write any Lua code to these configuration
files and it will be executed as part of updater execution.

Full reference to language it self can be found here:
https://turris.pages.nic.cz/updater/docs/language.html
]]

--[[
To add repository you have to call Repository command.
First argument must be name of repository (duplicate names are not allowed).
Second argument must be URL to repository. Use `file://` for local path.
Third argument is optional table with options for this repository.

Most important options are following:
priority: This allows you to specify priority of this repository. Packages are
  preferably taken from repository with higher priority no matter even if there
  is potentially newer version of package. In default all repositories has
  priority 50 and you can specify any value from 0 to 100.
verification: Specifies verification level. You can specify "none" to have do no
  repository verification or "sig" to check only repository signature or "cert" to
  not check signature but check ssl certificate if https is used. Or use "both" to
  verify both signature  and ssl vertificate.
pubkey: URL to public key used for signature verification.
]]
--Repository("repo-name", "https://example.com/", { pubkey = "file:///etc/repo.pubkey" })

--[[
To specify that you want to have some package installed you have to use function
Install.
It expect packages names as arguments and optional table with additional options.
You can pass multiple arguments at once to this function and additional options
will be applied on all of those.

Most important options are following:
priority: This allows you to specify how much you want this package. It has to be
  number between 0 and 100 and in default if not specified is set to 50. Updater
  can ignore given package if it's not possible to install it because it collides
  with some other package that is required with higher priority or with Uninstall
  command with higher priority.
version: Explicitly states that we want to install given packages with some
  specific version. Value has to be a string starting with at least one of these
  characters "=<>". So for example if you want anything newer than version 2.0
  then you can specify ">=2.0" to this option.
repository: Explicitly states that we want to install given packages from one of
  specified repositories.
]]
--Install("pkg1mame", "pkg2name", { repository = {"repo-name"} })


--[[
To specify that you don't want to have some package installed you have to use
function Uninstall.
It expect packages names as arguments and optional table with additional options.

Most important options are following:
priority: See Install command for more information.
]]
--Uninstall("pkg1name", "pkg2name")
