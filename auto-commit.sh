#!/bin/bash

# Auto-commit script for calculator project
# Runs every 5 hours to commit changes with descriptive messages

# Navigate to the project directory
cd /Users/jaiminpansal/Documents/programs/calc

# Check if there are any changes
if [[ -z $(git status -s) ]]; then
    echo "$(date): No changes to commit"
    exit 0
fi

# Generate a human-readable commit message based on changed files
changed_files=$(git status --porcelain | awk '{print $2}')
num_files=$(echo "$changed_files" | wc -l | tr -d ' ')

# Analyze changes to create a descriptive message
message=""

if echo "$changed_files" | grep -q "main.c"; then
    message="Update core calculator functionality"
elif echo "$changed_files" | grep -q "\.c$"; then
    message="Improve implementation"
elif echo "$changed_files" | grep -q "\.h$"; then
    message="Update header files"
fi

if echo "$changed_files" | grep -q "Makefile"; then
    if [[ -z "$message" ]]; then
        message="Update build configuration"
    else
        message="$message and build configuration"
    fi
fi

if echo "$changed_files" | grep -q "train.c\|model"; then
    if [[ -z "$message" ]]; then
        message="Update ML model and training"
    else
        message="$message and ML components"
    fi
fi

if echo "$changed_files" | grep -q "README"; then
    if [[ -z "$message" ]]; then
        message="Update documentation"
    else
        message="$message and documentation"
    fi
fi

# If no specific pattern matched, create a generic message
if [[ -z "$message" ]]; then
    message="General improvements and updates"
fi

# Add timestamp context
hour=$(date +%H)
if [[ $hour -ge 5 && $hour -lt 12 ]]; then
    time_context="morning"
elif [[ $hour -ge 12 && $hour -lt 17 ]]; then
    time_context="afternoon"
elif [[ $hour -ge 17 && $hour -lt 21 ]]; then
    time_context="evening"
else
    time_context="late night"
fi

# Create final commit message
final_message="$message - $time_context session"

# Add all changes
git add -A

# Commit with the descriptive message
git commit -m "$final_message"

# Log the commit
echo "$(date): Committed with message: $final_message" >> /Users/jaiminpansal/Documents/programs/calc/auto-commit.log

# Optional: Push to remote (uncomment if you want auto-push)
# git push origin main

exit 0
