#!/bin/sh
# git, except that `describe` only considers semantic-version tags (v1.2.3). Version.cmake (cmake/dependencies.cmake) parses `git describe --tags` as a
# semantic version and writes an EMPTY Version.hpp when the nearest tag is anything else (a milestone tag such as `parked-v1` broke every build:
# docs/INTERVENTIONS.md row 37). With no semantic tag `describe --always` gives the bare hash and Version.cmake falls back to 0.0.0-<count>-g<hash>.
# Every other git command passes through unchanged.
for a in "$@"; do
  if [ "$a" = "describe" ]; then exec git "$@" --match 'v[0-9]*.[0-9]*'; fi
done
exec git "$@"
