# Grade Pipeline proof apps

This directory contains the complete HSchema2 contracts, HScript2 workflow,
and three independently built C app executables used by the Milestone 5
end-to-end gate:

- `hermas2_grade_list` returns the canonical bounded list `[70, 80, 90]`.
- `hermas2_mean_calculator` owns the arithmetic and returns `80`.
- `hermas2_printer` owns presentation and prints `Mean: 80`.

The executables share [grade_app.c](grade_app.c) only to keep their small
protocol bootstrap identical. Each target is compiled with one closed app
identity and handler, connects outward through `libhermas2edge`, registers its
exact graph-image contract fingerprint, serves exactly one Action, and exits.

No calculation or printing is performed by HScript2 or the daemon. The
workflow graph only routes the canonical values and nominal presentation
metadata between these app-owned operations.
