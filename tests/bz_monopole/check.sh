#!/bin/bash

# Run checks against analytic result for specified tests

. ~/libs/anaconda3/etc/profile.d/conda.sh
conda activate pyharm

python3 ./check.py
