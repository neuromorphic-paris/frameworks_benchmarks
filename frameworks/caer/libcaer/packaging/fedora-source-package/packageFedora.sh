#!/bin/sh

# Requirements: Fedora, fedora-packager

FEDORA_RELEASE=f27

# Download source tarball.
spectool -g libcaer.spec

# Generate source RPM.
fedpkg --release "$FEDORA_RELEASE" srpm

# Run checks on generated RPM.
fedpkg --release "$FEDORA_RELEASE" lint

# Now you can upload the source RPM to COPR for building.
