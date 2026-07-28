use std::env;
use std::fs::{self, File};
use std::io::Read;
use std::process::ExitCode;

use hermas::{
    Catalog, MAX_GRAPH_IMAGE_SIZE, compile_hscript_module, compile_schema, decode_graph_image,
    encode_graph_image,
};

const MAX_SOURCE_SIZE: usize = 1024 * 1024;

fn main() -> ExitCode {
    let arguments: Vec<String> = env::args().skip(1).collect();
    if arguments.as_slice() == ["--version"] {
        println!(
            "Hermas {} (hermas; graph-image 1)",
            env!("CARGO_PKG_VERSION")
        );
        return ExitCode::SUCCESS;
    }
    if arguments.as_slice() == ["--help"] {
        print_usage();
        return ExitCode::SUCCESS;
    }
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
        let source = match read_source(path, "schema") {
            Ok(source) => source,
            Err(code) => return code,
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
    let bytes = match read_bounded(&arguments[1], "graph image", MAX_GRAPH_IMAGE_SIZE) {
        Ok(bytes) => bytes,
        Err(code) => return code,
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
    let bytes = read_bounded(path, kind, MAX_SOURCE_SIZE)?;
    String::from_utf8(bytes).map_err(|error| {
        eprintln!("{path}: {kind} is not valid UTF-8: {error}");
        ExitCode::FAILURE
    })
}

fn read_bounded(path: &str, kind: &str, limit: usize) -> Result<Vec<u8>, ExitCode> {
    let file = File::open(path).map_err(|error| {
        eprintln!("{path}: cannot read {kind}: {error}");
        ExitCode::FAILURE
    })?;
    let mut bytes = Vec::new();
    file.take(limit as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(|error| {
            eprintln!("{path}: cannot read {kind}: {error}");
            ExitCode::FAILURE
        })?;
    if bytes.len() > limit {
        eprintln!("{path}: {kind} exceeds the 1 MiB input limit");
        return Err(ExitCode::FAILURE);
    }
    Ok(bytes)
}

fn usage() {
    eprintln!(
        "usage:\n  hermas schema check <file.hschema>...\n  hermas workflow check <module.hscript> <file.hschema>...\n  hermas workflow <explain|graph|resources|sources> [--workflow NAME] <module.hscript> <file.hschema>...\n  hermas workflow image [--workflow NAME] <module.hscript> <output.hgi> <file.hschema>...\n  hermas image check <file.hgi>"
    );
}

fn print_usage() {
    println!(
        "Hermas verified action-graph compiler\n\n\
usage:\n  hermas schema check <file.hschema>...\n  \
hermas workflow check <module.hscript> <file.hschema>...\n  \
hermas workflow <explain|graph|resources|sources> [--workflow NAME] \
<module.hscript> <file.hschema>...\n  \
hermas workflow image [--workflow NAME] <module.hscript> \
<output.hgi> <file.hschema>...\n  \
hermas image check <file.hgi>\n  hermas --version"
    );
}
