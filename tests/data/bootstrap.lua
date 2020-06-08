--[[
Root script for updater-ng configuration usable for bootstrapping.

This script expects following variables to be possibly defined in environment:
  BOARD: board name (Mox|Omnia|Turris)
  L10N: commas separated list of languages to be installed in root.
  LISTS: commas separated list of package lists to be included in root.
  TESTKEY: if definied non-empty then test kyes are included in installation
]]

-- Sanity checks
if root_dir == "/" then
	DIE("Bootstrap is not allowed on root.")
end

-- Load requested localizations
l10n = {}
for lang in os.getenv('L10N'):gmatch('[^,]+') do
	table.insert(l10n, lang)
end
Export('l10n')

-- This is helper function for including localization packages.
-- (This is copy of standard entry function that can be found in pkgupdate conf.lua)
function for_l10n(fragment)
	for _, lang in pairs(l10n or {}) do
		Install(fragment .. lang, {optional = true})
	end
end
Export('for_l10n')

-- Aways include base script
Script('base.lua')
-- Include any additional lists
for list in os.getenv('LISTS'):gmatch('[^,]+') do
	Script(list .. '.lua')
end

if os.getenv('TESTKEY') then
	Install('cznic-repo-keys-test')
end
