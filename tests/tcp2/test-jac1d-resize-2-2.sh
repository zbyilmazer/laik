#!/bin/sh
${LAUNCHER-./launcher} -n 2 -s 2 ../../examples/jac1d 100 50 -10 > test-jac1d-resize-2-2.out
cmp test-jac1d-resize-2-2.out "$(dirname -- "${0}")/test-jac1d-resize-2-2.expected"
