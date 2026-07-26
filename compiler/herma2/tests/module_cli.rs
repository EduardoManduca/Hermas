use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use herma2::decode_graph_image;

fn repository_path(relative: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .join(relative)
}

fn module_arguments() -> [PathBuf; 3] {
    [
        repository_path("apps/multi-workflow/operations.hscript2"),
        repository_path("apps/grade-pipeline/mean-calculator.hschema2"),
        repository_path("apps/grade-pipeline/printer.hschema2"),
    ]
}

#[test]
fn cli_checks_the_module_and_requires_selection_for_one_graph() {
    let paths = module_arguments();
    let checked = Command::new(env!("CARGO_BIN_EXE_herma2"))
        .args(["workflow", "check"])
        .args(&paths)
        .output()
        .expect("module check runs");
    assert!(checked.status.success());
    let output = String::from_utf8(checked.stdout).unwrap();
    assert!(output.contains("valid: operations::calculate"));
    assert!(output.contains("valid: operations::print"));

    let ambiguous = Command::new(env!("CARGO_BIN_EXE_herma2"))
        .args(["workflow", "explain"])
        .args(&paths)
        .output()
        .expect("ambiguous inspection runs");
    assert!(!ambiguous.status.success());
    assert!(
        String::from_utf8(ambiguous.stderr)
            .unwrap()
            .contains("use --workflow NAME")
    );

    let selected = Command::new(env!("CARGO_BIN_EXE_herma2"))
        .args(["workflow", "explain", "--workflow", "print"])
        .args(&paths)
        .output()
        .expect("selected inspection runs");
    assert!(selected.status.success());
    assert!(
        String::from_utf8(selected.stdout)
            .unwrap()
            .starts_with("workflow operations::print\n")
    );
}

#[test]
fn cli_emits_one_selected_graph_image() {
    let paths = module_arguments();
    let output_path = std::env::temp_dir().join(format!(
        "hermas2-module-{}-calculate.h2gi",
        std::process::id()
    ));
    let emitted = Command::new(env!("CARGO_BIN_EXE_herma2"))
        .args(["workflow", "image", "--workflow", "calculate"])
        .arg(&paths[0])
        .arg(&output_path)
        .args(&paths[1..])
        .output()
        .expect("selected image emission runs");
    assert!(
        emitted.status.success(),
        "{}",
        String::from_utf8_lossy(&emitted.stderr)
    );
    let image = fs::read(&output_path).expect("selected image exists");
    let decoded = decode_graph_image(&image).expect("selected image validates");
    assert_eq!(decoded.workflow_name, "operations::calculate");
    fs::remove_file(output_path).expect("temporary selected image is removed");
}

#[test]
fn cli_has_no_built_in_workflows() {
    let result = Command::new(env!("CARGO_BIN_EXE_herma2"))
        .args(["check", "grade-pipeline"])
        .output()
        .expect("unsupported built-in workflow command runs");
    assert!(!result.status.success());
    let error = String::from_utf8(result.stderr).unwrap();
    assert!(error.contains("herma2 schema check"));
    assert!(!error.contains("grade-pipeline"));
}
