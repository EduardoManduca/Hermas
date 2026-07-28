#![allow(dead_code)]

use std::error::Error;
use std::fmt;

use hermas::{
    ActionId, Catalog, CatalogError, GraphError, HScriptDiagnostic, SchemaContract,
    SchemaDiagnostic, TypeId, VerifiedGraph, compile_hscript, compile_schema,
};

const GRADE_LIST_SCHEMA: &str = include_str!("../../../../apps/grade-pipeline/grade-list.hschema");
const MEAN_CALCULATOR_SCHEMA: &str =
    include_str!("../../../../apps/grade-pipeline/mean-calculator.hschema");
const PRINTER_SCHEMA: &str = include_str!("../../../../apps/grade-pipeline/printer.hschema");
const GRADE_PIPELINE_SCRIPT: &str =
    include_str!("../../../../apps/grade-pipeline/grade-pipeline.hscript");

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GradeListContract {
    pub empty: TypeId,
    pub grades: TypeId,
    pub error: TypeId,
    pub get: ActionId,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MeanCalculatorContract {
    pub input: TypeId,
    pub mean: TypeId,
    pub error: TypeId,
    pub calculate: ActionId,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PrinterContract {
    pub input: TypeId,
    pub printed: TypeId,
    pub error: TypeId,
    pub print: ActionId,
}

pub fn declare_grade_list(catalog: &mut Catalog) -> Result<GradeListContract, CatalogError> {
    let contract = compile_schema(catalog, "grade-list.hschema", GRADE_LIST_SCHEMA)
        .map_err(schema_catalog_error)?;
    let empty = required_type(&contract, "Empty")?;
    let grades = required_type(&contract, "GradeList")?;
    let error = required_type(&contract, "GradeError")?;
    let get = required_action(&contract, "get")?;
    Ok(GradeListContract {
        empty,
        grades,
        error,
        get,
    })
}

pub fn declare_mean_calculator(
    catalog: &mut Catalog,
) -> Result<MeanCalculatorContract, CatalogError> {
    let contract = compile_schema(catalog, "mean-calculator.hschema", MEAN_CALCULATOR_SCHEMA)
        .map_err(schema_catalog_error)?;
    let input = required_type(&contract, "MeanInput")?;
    let mean = required_type(&contract, "Mean")?;
    let error = required_type(&contract, "MeanError")?;
    let calculate = required_action(&contract, "calculate")?;
    Ok(MeanCalculatorContract {
        input,
        mean,
        error,
        calculate,
    })
}

pub fn declare_printer(catalog: &mut Catalog) -> Result<PrinterContract, CatalogError> {
    let contract =
        compile_schema(catalog, "printer.hschema", PRINTER_SCHEMA).map_err(schema_catalog_error)?;
    let input = required_type(&contract, "PrintInput")?;
    let printed = required_type(&contract, "Printed")?;
    let error = required_type(&contract, "PrintError")?;
    let print = required_action(&contract, "print")?;
    Ok(PrinterContract {
        input,
        printed,
        error,
        print,
    })
}

fn schema_catalog_error(error: SchemaDiagnostic) -> CatalogError {
    CatalogError::InvalidRepresentation(error.to_string())
}

fn required_type(contract: &SchemaContract, name: &str) -> Result<TypeId, CatalogError> {
    contract.type_id(name).ok_or_else(|| {
        CatalogError::InvalidRepresentation(format!(
            "embedded schema is missing required type `{name}`"
        ))
    })
}

fn required_action(contract: &SchemaContract, name: &str) -> Result<ActionId, CatalogError> {
    contract.action_id(name).ok_or_else(|| {
        CatalogError::InvalidRepresentation(format!(
            "embedded schema is missing required Action `{name}`"
        ))
    })
}

#[derive(Debug)]
pub enum GradePipelineError {
    Catalog(CatalogError),
    HScript(HScriptDiagnostic),
    Graph(GraphError),
}

impl fmt::Display for GradePipelineError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Catalog(error) => write!(formatter, "catalog: {error}"),
            Self::HScript(error) => write!(formatter, "HScript: {error}"),
            Self::Graph(error) => write!(formatter, "graph: {error}"),
        }
    }
}

impl Error for GradePipelineError {}

impl From<CatalogError> for GradePipelineError {
    fn from(error: CatalogError) -> Self {
        Self::Catalog(error)
    }
}

impl From<GraphError> for GradePipelineError {
    fn from(error: GraphError) -> Self {
        Self::Graph(error)
    }
}

impl From<HScriptDiagnostic> for GradePipelineError {
    fn from(error: HScriptDiagnostic) -> Self {
        Self::HScript(error)
    }
}

pub fn build_grade_pipeline() -> Result<(Catalog, VerifiedGraph), GradePipelineError> {
    let mut catalog = Catalog::new();
    let grades = declare_grade_list(&mut catalog)?;
    let mean = declare_mean_calculator(&mut catalog)?;
    let printer = declare_printer(&mut catalog)?;

    debug_assert_eq!(catalog.type_name(grades.empty), "grade-list::Empty");
    debug_assert_eq!(catalog.type_name(printer.printed), "printer::Printed");
    debug_assert_eq!(catalog.type_name(grades.error), "grade-list::GradeError");
    debug_assert_eq!(catalog.type_name(mean.error), "mean-calculator::MeanError");
    debug_assert_eq!(catalog.type_name(printer.error), "printer::PrintError");
    debug_assert_eq!(catalog.action_name(grades.get), "grade-list/get");
    debug_assert_eq!(
        catalog.action_name(mean.calculate),
        "mean-calculator/calculate"
    );
    debug_assert_eq!(catalog.action_name(printer.print), "printer/print");

    let verified = compile_hscript(&catalog, "grade-pipeline.hscript", GRADE_PIPELINE_SCRIPT)?;
    Ok((catalog, verified))
}
