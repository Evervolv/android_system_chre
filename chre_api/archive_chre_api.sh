#!/bin/bash

# Quit if any command produces an error.
set -e

VERSION_FILE=include/chre_api/chre/version.h

# Get the latest API version
CURRENT_VERSION=$(grep -E "^\#define CHRE_API_VERSION CHRE_API_VERSION_[0-9]+" $VERSION_FILE)
MAJOR_VERSION=$(echo $CURRENT_VERSION | cut -d "_" -f 6)
MINOR_VERSION=$(echo $CURRENT_VERSION | cut -d "_" -f 7)
ARCHIVE_DIRECTORY=v${MAJOR_VERSION}_${MINOR_VERSION}
mkdir legacy/$ARCHIVE_DIRECTORY
cp -r include/chre_api/* legacy/$ARCHIVE_DIRECTORY

ARCHIVED_VERSION=$(grep -n "^\#define CHRE_API_VERSION_${MAJOR_VERSION}_${MINOR_VERSION}" $VERSION_FILE)
LINE_NUMBER=$(($(echo $ARCHIVED_VERSION | cut -d ":" -f 1) + 2))
ARCHIVED_VERSION=$(echo $ARCHIVED_VERSION | cut -d ":" -f 2)

HEX_VERSION=$(echo $(echo $(echo $ARCHIVED_VERSION | cut -d "(" -f 2) | cut -d ")" -f 1) | cut -d "x" -f 2)
HEX_VERSION=$((16#$HEX_VERSION))
BITSHIFT=$(($MINOR_VERSION << 16))
HEX_VERSION=$(($HEX_VERSION - $BITSHIFT))
MINOR_VERSION=$(($MINOR_VERSION + 1))
BITSHIFT=$(($MINOR_VERSION<< 16));
HEX_VERSION=$(($HEX_VERSION + $BITSHIFT))
HEX_VERSION=$(printf "%x" $HEX_VERSION)

sed -i "${LINE_NUMBER}i#define CHRE_API_VERSION_${MAJOR_VERSION}_${MINOR_VERSION} UINT32_C(0x0${HEX_VERSION})\n" $VERSION_FILE
sed -i "s/${CURRENT_VERSION}/\#define CHRE_API_VERSION CHRE_API_VERSION_${MAJOR_VERSION}_${MINOR_VERSION}/g" $VERSION_FILE
