#!/bin/bash
#
#   Tony
#   An intonation analysis and annotation tool
#   Centre for Digital Music, Queen Mary, University of London.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2 of the
#   License, or (at your option) any later version.  See the file
#   COPYING included with this distribution for more information.
#
# What goes in the cloud environment's "Setup script" field, whole. It
# runs deploy/linux/cloud-environment.sh as it is on default, or the
# cloned checkout's copy if GitHub cannot be reached. The field cannot
# hold that script itself: it keeps only about the first 6000
# characters of what is pasted there.
#
# The environment's cache is built again when this text changes: change
# the date to have a change to cloud-environment.sh reach sessions now
# rather than when the cache expires. 2026-09-26

script=/tmp/tony-cloud-environment.sh
curl -sSfL --retry 3 --max-time 60 -o "$script" \
     https://raw.githubusercontent.com/jhhr/tony/default/deploy/linux/cloud-environment.sh ||
    script=$(ls /home/user/*/deploy/linux/cloud-environment.sh 2>/dev/null | head -1)

if [ -n "$script" ]; then
    bash "$script"
else
    echo "No deploy/linux/cloud-environment.sh to run"
fi

# A failing setup script stops the session from starting
exit 0
