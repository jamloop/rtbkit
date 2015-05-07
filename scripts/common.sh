#! /bin/bash

#
# Copyright (c) 2014 Datacratic Inc.  All rights reserved.
#

RTB_ROOT=${RTB_ROOT:-~/prod}

function rtbActiveColor {
   color=$(set -o pipefail; cd $RTB_ROOT && readlink -n rtb | sed 's/rtb-//; s!/$!!;')
   if [ $? -eq 0 ]; then
       echo $color
   fi
}

function rtbInactiveColor {
color=$(rtbActiveColor)

  case "$color" in
      "white")
          echo "black"
          ;;
      "black")
          echo "white"
          ;;
  esac
}

function numParallelJobs {
  # Returns a nice value for building in parallel without overloading the box
  # half the cores or 1

  cores=$(/usr/bin/nproc)
  j=$((cores / 2))
  [[ $j > 0 ]] && jobs=$j || jobs=1

  echo $jobs
}
