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
and serves one Action. App IDs, Action checks, nominal Type IDs, buffers, and
business logic remain in their owning app source. This is the same boundary
future apps are expected to use: reuse the stable edge ABI, not another app's
handler.

No calculation or printing is performed by HScript or the daemon. The
workflow graph only routes the canonical values and nominal presentation
metadata between these app-owned operations.
