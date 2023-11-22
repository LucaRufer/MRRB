#!/bin/bash
# Fix mistakes in the auto-generated code using HAL Version 1.10.0
# Autor: Luca Rufer, luca.rufer@swissloop.ch

# -------------------- Script Setup --------------------

dump_log() {
  echo "Fixing the auto-generated code failed. The following errors occured:"
  cat ${SETUP_LOG}
}

# Exit upon error
trap 'dump_log' EXIT
set -e

# Make sure to start executing from the script's directory
cd $(dirname $0)

# ----------------- Script Parameters ------------------
SETUP_LOG=$(pwd)/fix.log
PATCH_FOLDER=Patches
SCRIPT_LOCATION=CodeGen
CODE_LOCATION=..

# -------------------- Fix the Code --------------------

# Initialize Logfile
echo "Auto-generated code fix: $(date)" > $SETUP_LOG

# Find all patches
patches=$(ls -1 $PATCH_FOLDER/*.patch)
echo "" >> $SETUP_LOG
echo "Located patch(es):" >> $SETUP_LOG
for patch in $patches
do
  echo $patch >> $SETUP_LOG
done

# Change to the code folder
cd $CODE_LOCATION

# Apply patches
echo "" >> $SETUP_LOG
echo "Applying patch(es):" >> $SETUP_LOG
num_skipped=0
num_applied=0
num_failed=0
for patch in $patches
do
  # Check if the patch is already and if it can be applied
  if git apply --reverse --check $SCRIPT_LOCATION/$patch 2>> /dev/null ; then
    # Patch was already applied
    echo "Patch $patch is already applied. Skipping..." >> $SETUP_LOG
    ((num_skipped++))
  elif git apply $SCRIPT_LOCATION/$patch 2>> /dev/null ; then
    # Patch could be applied
    echo "Applied Patch $patch" >> $SETUP_LOG
    ((num_applied++))
  else
    # Patch cannot be applied due to conflict
    echo "Cannot Apply Patch $patch" >> $SETUP_LOG
    ((num_failed++))
  fi
done

echo "" >> $SETUP_LOG
echo "Summary:" >> $SETUP_LOG
echo "$num_skipped patch(es) were already applied" >> $SETUP_LOG
echo "$num_applied patch(es) were successfully applied." >> $SETUP_LOG
echo "$num_failed patch(es) could not be applied." >> $SETUP_LOG

if [ "$num_failed" -gt 0 ] ; then
  exit 1
fi

# Exit normally
trap - EXIT
