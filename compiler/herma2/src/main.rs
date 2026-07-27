use std::env;
use std::fs;
use std::process::ExitCode;

use herma2::{
    Catalog, compile_hscript_module, compile_schema, decode_graph_image, encode_graph_image,
};

fn main() -> ExitCode {
    let arguments: Vec<String> = env::args().skip(1).collect();
    if arguments.first().map(String::as_str) == Some("schema") {
        return schema_command(&arguments[1..]);
    }
    if arguments.first().map(String::as_str) == Some("workflow") {
        return workflow_command(&arguments[1..]);
    }
    if arguments.first().map(String::as_str) == Some("image") {
        return image_command(&arguments[1..]);
    }
    usage();
    ExitCode::from(2)
}

fn schema_command(arguments: &[String]) -> ExitCode {
    if arguments.len() < 2 || arguments[0] != "check" {
        usage();
        return ExitCode::from(2);
    }
    let mut catalog = Catalog::new();
    for path in &arguments[1..] {
        let source = match fs::read_to_string(path) {
            Ok(source) => source,
            Err(error) => {
                eprintln!("{path}: cannot read schema: {error}");
                return ExitCode::FAILURE;
            }
        };
        match compile_schema(&mut catalog, path, &source) {
            Ok(contract) => {
                println!(
                    "valid: {} schema-fingerprint={}",
                    catalog.app_name(contract.app),
                    contract.fingerprint
                );
                for (action, fingerprint) in &contract.action_fingerprints {
                    println!("action: {action} fingerprint={fingerprint}");
                }
            }
            Err(error) => {
                eprintln!("{error}");
                return ExitCode::FAILURE;
            }
        }
    }
    ExitCode::SUCCESS
}

fn workflow_command(arguments: &[String]) -> ExitCode {
    if arguments.len() < 2 {
        usage();
        return ExitCode::from(2);
    }
    let operation = &arguments[0];
    if !matches!(
        operation.as_str(),
        "check" | "explain" | "graph" | "resources" | "sources" | "image"
    ) {
        usage();
        return ExitCode::from(2);
    }
    let mut cursor = 1usize;
    let workflow_name = if arguments.get(cursor).map(String::as_str) == Some("--workflow") {
        let Some(name) = arguments.get(cursor + 1) else {
            usage();
            return ExitCode::from(2);
        };
        cursor += 2;
        Some(name.as_str())
    } else {
        None
    };
    let Some(script_path) = arguments.get(cursor) else {
        usage();
        return ExitCode::from(2);
    };
    cursor += 1;
    let output_path = if operation == "image" {
        let Some(path) = arguments.get(cursor) else {
            usage();
            return ExitCode::from(2);
        };
        cursor += 1;
        Some(path)
    } else {
        None
    };
    let mut catalog = Catalog::new();
    for schema_path in &arguments[cursor..] {
        let source = match read_source(schema_path, "schema") {
            Ok(source) => source,
            Err(code) => return code,
        };
        if let Err(error) = compile_schema(&mut catalog, schema_path, &source) {
            eprintln!("{error}");
            return ExitCode::FAILURE;
        }
    }
    let script = match read_source(script_path, "workflow") {
        Ok(source) => source,
        Err(code) => return code,
    };
    let module = match compile_hscript_module(&catalog, script_path, &script) {
        Ok(module) => module,
        Err(error) => {
            eprintln!("{error}");
            return ExitCode::FAILURE;
        }
    };
    let selected = workflow_name
        .map(|name| {
            module.workflow(name).ok_or_else(|| {
                eprintln!("{script_path}: module does not declare workflow `{name}`");
                ExitCode::FAILURE
            })
        })
        .transpose();
    let selected = match selected {
        Ok(selected) => selected,
        Err(code) => return code,
    };
    if operation == "check" {
        for graph in module.workflows() {
            println!("valid: {}", graph.name());
        }
        return ExitCode::SUCCESS;
    }
    let graph = match selected {
        Some(graph) => graph,
        None if module.workflows().len() == 1 => &module.workflows()[0],
        None => {
            eprintln!("{script_path}: module declares multiple workflows; use --workflow NAME");
            return ExitCode::FAILURE;
        }
    };
    match operation.as_str() {
        "explain" => print!("{}", graph.explain(&catalog)),
        "graph" => print!("{}", graph.to_dot(&catalog)),
        "resources" => println!("{}", graph.resources(&catalog)),
        "sources" => print!("{}", graph.source_map()),
        "image" => {
            let image = match encode_graph_image(graph, &catalog) {
                Ok(image) => image,
                Err(error) => {
                    eprintln!("cannot encode graph image: {error}");
                    return ExitCode::FAILURE;
                }
            };
            let path = output_path.expect("image operation has an output path");
            if let Err(error) = fs::write(path, image) {
                eprintln!("{path}: cannot write graph image: {error}");
                return ExitCode::FAILURE;
            }
            println!("wrote: {path}");
        }
        _ => unreachable!("operation validated above"),
    }
    ExitCode::SUCCESS
}

fn image_command(arguments: &[String]) -> ExitCode {
    if arguments.len() != 2 || arguments[0] != "check" {
        usage();
        return ExitCode::from(2);
    }
    let bytes = match fs::read(&arguments[1]) {
        Ok(bytes) => bytes,
        Err(error) => {
            eprintln!("{}: cannot read graph image: {error}", arguments[1]);
            return ExitCode::FAILURE;
        }
    };
    match decode_graph_image(&bytes) {
        Ok(image) => {
            println!(
                "valid: {} action-contracts={} nodes={} edges={}",
                image.workflow_name,
                image.action_contract_count,
                image.node_count,
                image.edge_count
            );
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("{}: {error}", arguments[1]);
            ExitCode::FAILURE
        }
    }
}

fn read_source(path: &str, kind: &str) -> Result<String, ExitCode> {
    fs::read_to_string(path).map_err(|error| {
        eprintln!("{path}: cannot read {kind}: {error}");
        ExitCode::FAILURE
    })
}

fn usage() {
    eprintln!(
        "usage:\n  herma2 schema check <file.hschema2>...\n  herma2 workflow check <module.hscript2> <file.hschema2>...\n  herma2 workflow <explain|graph|resources|sources> [--workflow NAME] <module.hscript2> <file.hschema2>...\n  herma2 workflow image [--workflow NAME] <module.hscript2> <output.h2gi> <file.hschema2>...\n  herma2 image check <file.h2gi>"
    );
}
