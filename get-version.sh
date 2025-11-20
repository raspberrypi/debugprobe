#!/bin/sh
if command -v git &>/dev/null
then
    VERSION=$(git describe --exact-match --tags 2> /dev/null || git rev-parse --short HEAD)   
else
    VERSION="unknown"
fi

if [ ! -e generated/probe ]
then
    mkdir -p generated/probe
fi

cat > generated/probe/version.h << EOF
#ifndef _PROBE_VERSION_H
#define _PROBE_VERSION_H

#define PROBE_VERSION "${VERSION}"

#endif
EOF
