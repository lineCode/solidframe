#!/usr/bin/env bash

VSCMDPATH="`find c\:/Program\ Files\ \(x86\)/ -name vcvarsall.bat 2>/dev/null | grep -v "Permission" | grep -F "14.0"`"
VSSETUP="`cygpath -w "$VSCMDPATH"`"
CMDPATH="`pwd`"
CMDSETUP="`cygpath -w "$CMDPATH"`"
echo "$VSSETUP"
echo "$CMDSETUP"
CMD="\"$VSSETUP\" $1 && cd $CMDSETUP&& $2"
echo "$CMD"
cmd.exe /C "$CMD"
