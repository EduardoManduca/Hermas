use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use hermas::{MAX_GRAPH_IMAGE_SIZE, decode_graph_image};

fn repository_path(relative: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .join(relative)
}

fn module_arguments() -> [PathBuf; 3] {
    [
        repository_path("apps/multi-workflow/operations.hscript"),
        repository_path("apps/grade-pipeline/mean-calculator.hschema"),
        repository_path("apps/grade-pipeline/printer.hschema"),
    ]
}

#[test]
fn cli_checks_the_module_and_requires_selection_for_one_graph() {
    let paths = module_arguments();
    let checked = Command::new(env!("CARGO_BIN_EXE_hermas"))
        .args(["workflow", "check"])
        .args(&paths)
        .output()
        .expect("module check runs");
    assert!(checked.status.success());
    let output = String::from_utf8(checked.stdout).unwrap();
    assert!(output.contains("valid: operations::calculate"));
    assert!(output.contains("valid: operations::print"));

    let ambiguous = Command::new(env!("CARGO_BIN_EXE_hermas"))
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

    let selected = Command::new(env!("CARGO_BIN_EXE_hermas"))
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
        "hermas-module-{}-calculate.hgi",
        std::process::id()
    ));
    let emitted = Command::new(env!("CARGO_BIN_EXE_hermas"))
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
fn cli_emits_a_c_contract_identity_header() {
    let schema = repository_path("apps/grade-pipeline/grade-list.hschema");
    let output_path = std::env::temp_dir().join(format!(
        "hermas-contract-{}-grade-list.h",
        std::process::id()
    ));
    let emitted = Command::new(env!("CARGO_BIN_EXE_hermas"))
        .args(["schema", "c-header"])
        .arg(schema)
        .arg(&output_path)
        .arg("hermas_grade_list")
        .output()
        .expect("C header emission runs");
    assert!(
        emitted.status.success(),
        "{}",
        String::from_utf8_lossy(&emitted.stderr)
    );
    let header = fs::read_to_string(&output_path).expect("C header exists");
    assert!(header.contains("#define HERMAS_GRADE_LIST_ACTION_GET_FINGERPRINT"));
    fs::remove_file(output_path).expect("temporary C header is removed");
}

#[test]
fn cli_has_no_built_in_workflows() {
    let result = Command::new(env!("CARGO_BIN_EXE_hermas"))
        .args(["check", "grade-pipeline"])
        .output()
        .expect("unsupported built-in workflow command runs");
    assert!(!result.status.success());
    let error = String::from_utf8(result.stderr).unwrap();
    assert!(error.contains("hermas schema check"));
    assert!(!error.contains("grade-pipeline"));
}

#[test]
fn cli_rejects_oversized_source_and_image_files() {
    let temporary = std::env::temp_dir().join(format!("hermas-oversized-{}", std::process::id()));
    fs::write(&temporary, vec![b'a'; MAX_GRAPH_IMAGE_SIZE + 1])
        .expect("oversized fixture is written");

    for arguments in [
        vec!["schema", "check", temporary.to_str().unwrap()],
        vec!["image", "check", temporary.to_str().unwrap()],
    ] {
        let result = Command::new(env!("CARGO_BIN_EXE_hermas"))
            .args(arguments)
            .output()
            .expect("bounded CLI command runs");
        assert!(!result.status.success());
        assert!(
            String::from_utf8(result.stderr)
                .unwrap()
                .contains("exceeds the 1 MiB input limit")
        );
    }

    fs::remove_file(temporary).expect("oversized fixture is removed");
}
