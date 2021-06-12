# This is implementation of filter function using findstring instead of patter.
# It can be used in places where pattern such as %FOO% is required.
# Usage: $(call FILTER,what_to_find,$(VAR))
FILTER = $(foreach v,$(2),$(if $(findstring $(1),$(v)),$(v)))
