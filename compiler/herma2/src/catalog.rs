use std::collections::BTreeMap;
use std::error::Error;
use std::fmt;

const MAX_LIST_BOUND: u16 = 256;
const MAX_BYTE_BOUND: u32 = 1_048_576;

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct AppId(u16);

impl AppId {
    pub const fn from_raw(raw: u16) -> Self {
        Self(raw)
    }

    pub const fn raw(self) -> u16 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct TypeId(u16);

impl TypeId {
    pub const fn from_raw(raw: u16) -> Self {
        Self(raw)
    }

    pub const fn raw(self) -> u16 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct ActionId(u16);

impl ActionId {
    pub const fn from_raw(raw: u16) -> Self {
        Self(raw)
    }

    pub const fn raw(self) -> u16 {
        self.0
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Representation {
    Unit,
    Integer,
    Boolean,
    String {
        maximum: u32,
    },
    Bytes {
        maximum: u32,
    },
    Record(BTreeMap<String, Representation>),
    List {
        element: Box<Representation>,
        maximum: u16,
    },
    Variant(BTreeMap<String, Representation>),
}

impl Representation {
    pub fn list(element: Representation, maximum: u16) -> Self {
        Self::List {
            element: Box::new(element),
            maximum,
        }
    }

    pub fn maximum_wire_size(&self) -> Option<usize> {
        match self {
            Self::Unit => Some(0),
            Self::Integer => Some(8),
            Self::Boolean => Some(1),
            Self::String { maximum } | Self::Bytes { maximum } => {
                usize::try_from(*maximum).ok()?.checked_add(8)
            }
            Self::Record(fields) => fields.values().try_fold(0usize, |size, field| {
                size.checked_add(field.maximum_wire_size()?)
            }),
            Self::List { element, maximum } => {
                let element_size = element.maximum_wire_size()?;
                element_size
                    .checked_mul(usize::from(*maximum))
                    .and_then(|payload| payload.checked_add(8))
            }
            Self::Variant(cases) => cases
                .values()
                .try_fold(0usize, |largest, case| {
                    Some(largest.max(case.maximum_wire_size()?))
                })
                .and_then(|payload| payload.checked_add(8)),
        }
    }

    fn validate(&self, path: &str) -> Result<(), CatalogError> {
        match self {
            Self::Unit | Self::Integer | Self::Boolean => Ok(()),
            Self::String { maximum } | Self::Bytes { maximum } => {
                if !(1..=MAX_BYTE_BOUND).contains(maximum) {
                    return Err(CatalogError::InvalidRepresentation(format!(
                        "{path}: byte bound {maximum} is outside 1..={MAX_BYTE_BOUND}"
                    )));
                }
                Ok(())
            }
            Self::Record(fields) => {
                for (name, field) in fields {
                    validate_name(name)?;
                    field.validate(&format!("{path}.{name}"))?;
                }
                Ok(())
            }
            Self::List { element, maximum } => {
                if !(1..=MAX_LIST_BOUND).contains(maximum) {
                    return Err(CatalogError::InvalidRepresentation(format!(
                        "{path}: list bound {maximum} is outside 1..={MAX_LIST_BOUND}"
                    )));
                }
                element.validate(&format!("{path}.element"))
            }
            Self::Variant(cases) => {
                if cases.is_empty() {
                    return Err(CatalogError::InvalidRepresentation(format!(
                        "{path}: variant must declare at least one case"
                    )));
                }
                for (name, payload) in cases {
                    validate_name(name)?;
                    payload.validate(&format!("{path}::{name}"))?;
                }
                Ok(())
            }
        }
    }

    fn compatible_with(&self, destination: &Representation, path: &str) -> Result<(), String> {
        match (self, destination) {
            (Self::Unit, Self::Unit)
            | (Self::Integer, Self::Integer)
            | (Self::Boolean, Self::Boolean) => Ok(()),
            (
                Self::String {
                    maximum: source_maximum,
                },
                Self::String {
                    maximum: destination_maximum,
                },
            )
            | (
                Self::Bytes {
                    maximum: source_maximum,
                },
                Self::Bytes {
                    maximum: destination_maximum,
                },
            ) => {
                if source_maximum > destination_maximum {
                    return Err(format!(
                        "{path}: source byte bound {source_maximum} exceeds destination bound {destination_maximum}"
                    ));
                }
                Ok(())
            }
            (Self::Record(source_fields), Self::Record(destination_fields)) => {
                if source_fields.len() != destination_fields.len() {
                    return Err(format!("{path}: record field counts differ"));
                }
                for (name, source_field) in source_fields {
                    let destination_field = destination_fields
                        .get(name)
                        .ok_or_else(|| format!("{path}.{name}: destination field is missing"))?;
                    source_field.compatible_with(destination_field, &format!("{path}.{name}"))?;
                }
                Ok(())
            }
            (
                Self::List {
                    element: source_element,
                    maximum: source_maximum,
                },
                Self::List {
                    element: destination_element,
                    maximum: destination_maximum,
                },
            ) => {
                if source_maximum > destination_maximum {
                    return Err(format!(
                        "{path}: source list bound {source_maximum} exceeds destination bound {destination_maximum}"
                    ));
                }
                source_element.compatible_with(destination_element, &format!("{path}.element"))
            }
            (Self::Variant(source_cases), Self::Variant(destination_cases)) => {
                if source_cases.len() != destination_cases.len() {
                    return Err(format!("{path}: variant case counts differ"));
                }
                for (name, source_payload) in source_cases {
                    let destination_payload = destination_cases
                        .get(name)
                        .ok_or_else(|| format!("{path}::{name}: destination case is missing"))?;
                    source_payload
                        .compatible_with(destination_payload, &format!("{path}::{name}"))?;
                }
                Ok(())
            }
            _ => Err(format!(
                "{path}: source representation {} does not match destination {}",
                self.kind_name(),
                destination.kind_name()
            )),
        }
    }

    fn kind_name(&self) -> &'static str {
        match self {
            Self::Unit => "Unit",
            Self::Integer => "Integer",
            Self::Boolean => "Boolean",
            Self::String { .. } => "String",
            Self::Bytes { .. } => "Bytes",
            Self::Record(_) => "Record",
            Self::List { .. } => "List",
            Self::Variant(_) => "Variant",
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AppDef {
    pub id: AppId,
    pub name: String,
    pub fingerprint: Option<[u8; 32]>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NominalType {
    pub id: TypeId,
    pub app: AppId,
    pub name: String,
    pub representation: Representation,
    pub variant_cases: BTreeMap<String, TypeId>,
    pub list_element: Option<TypeId>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ActionDef {
    pub id: ActionId,
    pub app: AppId,
    pub name: String,
    pub input: TypeId,
    pub success: TypeId,
    pub error: TypeId,
    pub compensation: Compensation,
    pub fingerprint: Option<[u8; 32]>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Compensation {
    None,
    Action(ActionId),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CompensationDeclaration {
    None,
    Action(String),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ActionDeclaration {
    pub name: String,
    pub input: TypeId,
    pub success: TypeId,
    pub error: TypeId,
    pub compensation: CompensationDeclaration,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CatalogError {
    InvalidName(String),
    InvalidRepresentation(String),
    DuplicateApp(String),
    DuplicateType {
        app: String,
        name: String,
    },
    DuplicateAction {
        app: String,
        name: String,
    },
    UnknownApp(AppId),
    UnknownType(TypeId),
    UnknownAction(ActionId),
    ForeignActionType {
        app: String,
        type_name: String,
    },
    UnknownCompensation {
        action: String,
        compensation: String,
    },
    SelfCompensation(String),
    IncompatibleCompensation {
        action: String,
        reason: String,
    },
    LimitExceeded(&'static str),
}

impl fmt::Display for CatalogError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidName(name) => write!(formatter, "invalid catalog name `{name}`"),
            Self::InvalidRepresentation(message) => formatter.write_str(message),
            Self::DuplicateApp(app) => write!(formatter, "duplicate app `{app}`"),
            Self::DuplicateType { app, name } => {
                write!(formatter, "duplicate type `{app}::{name}`")
            }
            Self::DuplicateAction { app, name } => {
                write!(formatter, "duplicate action `{app}/{name}`")
            }
            Self::UnknownApp(app) => write!(formatter, "unknown app ID {}", app.raw()),
            Self::UnknownType(type_id) => {
                write!(formatter, "unknown type ID {}", type_id.raw())
            }
            Self::UnknownAction(action) => {
                write!(formatter, "unknown Action ID {}", action.raw())
            }
            Self::ForeignActionType { app, type_name } => write!(
                formatter,
                "action owned by `{app}` cannot declare foreign type `{type_name}`"
            ),
            Self::UnknownCompensation {
                action,
                compensation,
            } => write!(
                formatter,
                "action `{action}` names unknown compensation `{compensation}`"
            ),
            Self::SelfCompensation(action) => {
                write!(formatter, "action `{action}` cannot compensate itself")
            }
            Self::IncompatibleCompensation { action, reason } => {
                write!(formatter, "invalid compensation for `{action}`: {reason}")
            }
            Self::LimitExceeded(kind) => write!(formatter, "{kind} limit exceeded"),
        }
    }
}

impl Error for CatalogError {}

#[derive(Clone, Debug, Default)]
pub struct Catalog {
    apps: Vec<AppDef>,
    types: Vec<NominalType>,
    actions: Vec<ActionDef>,
}

impl Catalog {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn declare_app(&mut self, name: &str) -> Result<AppId, CatalogError> {
        validate_name(name)?;
        if self.apps.iter().any(|app| app.name == name) {
            return Err(CatalogError::DuplicateApp(name.to_owned()));
        }
        let id = AppId(next_id(self.apps.len(), "app")?);
        self.apps.push(AppDef {
            id,
            name: name.to_owned(),
            fingerprint: None,
        });
        Ok(id)
    }

    pub fn set_app_fingerprint(
        &mut self,
        app: AppId,
        fingerprint: [u8; 32],
    ) -> Result<(), CatalogError> {
        let app_def = self
            .apps
            .get_mut(usize::from(
                app.raw()
                    .checked_sub(1)
                    .ok_or(CatalogError::UnknownApp(app))?,
            ))
            .ok_or(CatalogError::UnknownApp(app))?;
        app_def.fingerprint = Some(fingerprint);
        Ok(())
    }

    pub fn set_action_fingerprint(
        &mut self,
        action: ActionId,
        fingerprint: [u8; 32],
    ) -> Result<(), CatalogError> {
        let action_def = self
            .actions
            .get_mut(usize::from(
                action
                    .raw()
                    .checked_sub(1)
                    .ok_or(CatalogError::UnknownAction(action))?,
            ))
            .ok_or(CatalogError::UnknownAction(action))?;
        action_def.fingerprint = Some(fingerprint);
        Ok(())
    }

    pub fn declare_type(
        &mut self,
        app: AppId,
        name: &str,
        representation: Representation,
    ) -> Result<TypeId, CatalogError> {
        let app_name = self
            .app(app)
            .ok_or(CatalogError::UnknownApp(app))?
            .name
            .clone();
        validate_name(name)?;
        representation.validate(name)?;
        if self
            .types
            .iter()
            .any(|type_def| type_def.app == app && type_def.name == name)
        {
            return Err(CatalogError::DuplicateType {
                app: app_name,
                name: name.to_owned(),
            });
        }
        let id = TypeId(next_id(self.types.len(), "type")?);
        self.types.push(NominalType {
            id,
            app,
            name: name.to_owned(),
            representation,
            variant_cases: BTreeMap::new(),
            list_element: None,
        });
        Ok(id)
    }

    pub fn set_variant_cases(
        &mut self,
        variant: TypeId,
        cases: BTreeMap<String, TypeId>,
    ) -> Result<(), CatalogError> {
        let variant_index = usize::from(
            variant
                .raw()
                .checked_sub(1)
                .ok_or(CatalogError::UnknownType(variant))?,
        );
        let type_def = self
            .types
            .get(variant_index)
            .ok_or(CatalogError::UnknownType(variant))?;
        let Representation::Variant(representations) = &type_def.representation else {
            return Err(CatalogError::InvalidRepresentation(format!(
                "{} is not a variant",
                type_def.name
            )));
        };
        if representations.len() != cases.len() {
            return Err(CatalogError::InvalidRepresentation(format!(
                "{} variant case metadata is incomplete",
                type_def.name
            )));
        }
        for (name, payload) in &cases {
            let payload_type = self
                .type_def(*payload)
                .ok_or(CatalogError::UnknownType(*payload))?;
            let expected = representations.get(name).ok_or_else(|| {
                CatalogError::InvalidRepresentation(format!(
                    "{} has no variant case `{name}`",
                    type_def.name
                ))
            })?;
            if &payload_type.representation != expected {
                return Err(CatalogError::InvalidRepresentation(format!(
                    "{}::{name} payload metadata has a different representation",
                    type_def.name
                )));
            }
        }
        self.types[variant_index].variant_cases = cases;
        Ok(())
    }

    pub fn set_list_element(&mut self, list: TypeId, element: TypeId) -> Result<(), CatalogError> {
        let element_representation = self
            .type_def(element)
            .ok_or(CatalogError::UnknownType(element))?
            .representation
            .clone();
        let list_type = self
            .types
            .get_mut(usize::from(
                list.raw()
                    .checked_sub(1)
                    .ok_or(CatalogError::UnknownType(list))?,
            ))
            .ok_or(CatalogError::UnknownType(list))?;
        let Representation::List {
            element: expected, ..
        } = &list_type.representation
        else {
            return Err(CatalogError::InvalidRepresentation(format!(
                "{} is not a list",
                list_type.name
            )));
        };
        if expected.as_ref() != &element_representation {
            return Err(CatalogError::InvalidRepresentation(format!(
                "{} list element metadata does not match its representation",
                list_type.name
            )));
        }
        list_type.list_element = Some(element);
        Ok(())
    }

    pub fn list_element(&self, list: TypeId) -> Option<(TypeId, u16)> {
        let type_def = self.type_def(list)?;
        let Representation::List { maximum, .. } = &type_def.representation else {
            return None;
        };
        Some((type_def.list_element?, *maximum))
    }

    pub fn variant_cases(&self, variant: TypeId) -> Option<&BTreeMap<String, TypeId>> {
        let type_def = self.type_def(variant)?;
        (!type_def.variant_cases.is_empty()).then_some(&type_def.variant_cases)
    }

    pub fn declare_action(
        &mut self,
        app: AppId,
        name: &str,
        input: TypeId,
        success: TypeId,
        error: TypeId,
        compensation: CompensationDeclaration,
    ) -> Result<ActionId, CatalogError> {
        let ids = self.declare_actions(
            app,
            vec![ActionDeclaration {
                name: name.to_owned(),
                input,
                success,
                error,
                compensation,
            }],
        )?;
        Ok(ids[0])
    }

    pub fn declare_actions(
        &mut self,
        app: AppId,
        declarations: Vec<ActionDeclaration>,
    ) -> Result<Vec<ActionId>, CatalogError> {
        let app_name = self
            .app(app)
            .ok_or(CatalogError::UnknownApp(app))?
            .name
            .clone();
        let mut names = BTreeMap::new();
        for (offset, declaration) in declarations.iter().enumerate() {
            validate_name(&declaration.name)?;
            if self
                .actions
                .iter()
                .any(|action| action.app == app && action.name == declaration.name)
                || names.insert(declaration.name.clone(), offset).is_some()
            {
                return Err(CatalogError::DuplicateAction {
                    app: app_name.clone(),
                    name: declaration.name.clone(),
                });
            }
            for type_id in [declaration.input, declaration.success, declaration.error] {
                let type_def = self
                    .type_def(type_id)
                    .ok_or(CatalogError::UnknownType(type_id))?;
                if type_def.app != app {
                    return Err(CatalogError::ForeignActionType {
                        app: app_name.clone(),
                        type_name: self.type_name(type_id),
                    });
                }
            }
        }
        let base = self.actions.len();
        let ids = (0..declarations.len())
            .map(|offset| {
                base.checked_add(offset)
                    .ok_or(CatalogError::LimitExceeded("action"))
                    .and_then(|index| next_id(index, "action"))
                    .map(ActionId)
            })
            .collect::<Result<Vec<_>, _>>()?;
        let mut resolved = Vec::with_capacity(declarations.len());
        for (index, declaration) in declarations.iter().enumerate() {
            let compensation = match &declaration.compensation {
                CompensationDeclaration::None => Compensation::None,
                CompensationDeclaration::Action(compensation) => {
                    if compensation == &declaration.name {
                        return Err(CatalogError::SelfCompensation(declaration.name.clone()));
                    }
                    let compensation_offset =
                        names.get(compensation).copied().ok_or_else(|| {
                            CatalogError::UnknownCompensation {
                                action: declaration.name.clone(),
                                compensation: compensation.clone(),
                            }
                        })?;
                    let compensation_declaration = &declarations[compensation_offset];
                    self.representation_compatible_ids(
                        declaration.success,
                        compensation_declaration.input,
                    )
                    .map_err(|reason| {
                        CatalogError::IncompatibleCompensation {
                            action: declaration.name.clone(),
                            reason,
                        }
                    })?;
                    Compensation::Action(ids[compensation_offset])
                }
            };
            resolved.push(ActionDef {
                id: ids[index],
                app,
                name: declaration.name.clone(),
                input: declaration.input,
                success: declaration.success,
                error: declaration.error,
                compensation,
                fingerprint: None,
            });
        }
        self.actions.extend(resolved);
        Ok(ids)
    }

    pub fn app(&self, id: AppId) -> Option<&AppDef> {
        indexed(&self.apps, id.raw())
    }

    pub fn find_app(&self, name: &str) -> Option<AppId> {
        self.apps
            .iter()
            .find(|item| item.name == name)
            .map(|item| item.id)
    }

    pub fn type_def(&self, id: TypeId) -> Option<&NominalType> {
        indexed(&self.types, id.raw())
    }

    pub fn action(&self, id: ActionId) -> Option<&ActionDef> {
        indexed(&self.actions, id.raw())
    }

    pub fn find_type(&self, app: AppId, name: &str) -> Option<TypeId> {
        self.types
            .iter()
            .find(|item| item.app == app && item.name == name)
            .map(|item| item.id)
    }

    pub fn find_action(&self, app: AppId, name: &str) -> Option<ActionId> {
        self.actions
            .iter()
            .find(|item| item.app == app && item.name == name)
            .map(|item| item.id)
    }

    pub fn resolve_type(&self, app: &str, name: &str) -> Option<TypeId> {
        self.find_app(app)
            .and_then(|app_id| self.find_type(app_id, name))
    }

    pub fn resolve_action(&self, app: &str, name: &str) -> Option<ActionId> {
        self.find_app(app)
            .and_then(|app_id| self.find_action(app_id, name))
    }

    pub fn app_name(&self, id: AppId) -> String {
        self.app(id)
            .map_or_else(|| format!("<app:{}>", id.raw()), |app| app.name.clone())
    }

    pub fn type_name(&self, id: TypeId) -> String {
        self.type_def(id).map_or_else(
            || format!("<type:{}>", id.raw()),
            |type_def| format!("{}::{}", self.app_name(type_def.app), type_def.name),
        )
    }

    pub fn action_name(&self, id: ActionId) -> String {
        self.action(id).map_or_else(
            || format!("<action:{}>", id.raw()),
            |action| format!("{}/{}", self.app_name(action.app), action.name),
        )
    }

    pub fn representation_compatible(
        &self,
        source: TypeId,
        destination: TypeId,
    ) -> Result<(), String> {
        let source_type = self
            .type_def(source)
            .ok_or_else(|| format!("unknown source type ID {}", source.raw()))?;
        let destination_type = self
            .type_def(destination)
            .ok_or_else(|| format!("unknown destination type ID {}", destination.raw()))?;
        source_type
            .representation
            .compatible_with(&destination_type.representation, "$")
    }

    fn representation_compatible_ids(
        &self,
        source: TypeId,
        destination: TypeId,
    ) -> Result<(), String> {
        let source_type = self
            .type_def(source)
            .ok_or_else(|| format!("unknown source type ID {}", source.raw()))?;
        let destination_type = self
            .type_def(destination)
            .ok_or_else(|| format!("unknown destination type ID {}", destination.raw()))?;
        source_type
            .representation
            .compatible_with(&destination_type.representation, "$")
    }
}

fn indexed<T>(items: &[T], raw: u16) -> Option<&T> {
    raw.checked_sub(1)
        .and_then(|index| items.get(usize::from(index)))
}

fn next_id(length: usize, kind: &'static str) -> Result<u16, CatalogError> {
    let one_based = length
        .checked_add(1)
        .ok_or(CatalogError::LimitExceeded(kind))?;
    u16::try_from(one_based).map_err(|_| CatalogError::LimitExceeded(kind))
}

fn validate_name(name: &str) -> Result<(), CatalogError> {
    let mut bytes = name.bytes();
    let Some(first) = bytes.next() else {
        return Err(CatalogError::InvalidName(name.to_owned()));
    };
    if !(first.is_ascii_alphabetic() || first == b'_')
        || !bytes.all(|byte| byte.is_ascii_alphanumeric() || byte == b'_' || byte == b'-')
    {
        return Err(CatalogError::InvalidName(name.to_owned()));
    }
    Ok(())
}
