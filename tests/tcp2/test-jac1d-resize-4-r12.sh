#!/bin/sh
${LAUNCHER-./launcher} -n 4 -r L[12] ../../examples/jac1d 100 50 -10 > test-jac1d-resize-4-r12.out
cmp test-jac1d-resize-4-r12.out "$(dirname -- "${0}")/test-jac1d-resize-4-r12.expected"
