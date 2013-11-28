#!/bin/sh
grep -ho stop\([0-9]*\,.* . -R | sed 's,stop(,,g' | sed 's,\, ", - ,g' | sed 's,");,,g' | sort -n
