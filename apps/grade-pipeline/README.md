# Grade Pipeline proof apps

This directory contains the complete HSchema contracts, HScript workflow,
and three independently built C app executables used by the Milestone 5
end-to-end gate:

- `hermas_grade_list` returns the canonical bounded list `[70, 80, 90]`.
- `hermas_mean_calculator` owns the arithmetic and returns `80`.
- `hermas_printer` owns presentation and prints `Mean: 80`.

Each app owns a separate implementation file:

- [grade_list_app.c](grade_list_app.c)
- [mean_calculator_app.c](mean_calculator_app.c)
- [printer_app.c](printer_app.c)

They share only the small
[example bootstrap](../example_app.c), which validates each binary's
compiler-generated contract fingerprint, connects through `libhermas_edge`,
resolves graph-local IDs and port Types from the managed image, and serves one
Action. Business handlers own buffers and domain logic but contain no catalog
numbers. This is the same boundary future apps are expected to use: reuse the
stable edge ABI and semantic contract identity, not another app's handler.

No calculation or printing is performed by HScript or the daemon. The
workflow graph only routes the canonical values and nominal presentation
metadata between these app-owned operations.
