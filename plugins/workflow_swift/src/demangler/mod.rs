use binaryninja::architecture::CoreArchitecture;
use binaryninja::binary_view::BinaryView;
use binaryninja::demangle::CustomDemangler;
use binaryninja::rc::Ref;
use binaryninja::types::{QualifiedName, Type};

pub struct SwiftDemangler;

impl CustomDemangler for SwiftDemangler {
    fn is_mangled_string(&self, name: &str) -> bool {
        name.starts_with("$s")
            || name.starts_with("_$s")
            || name.starts_with("$S")
            || name.starts_with("_$S")
            || name.starts_with("$e")
            || name.starts_with("_$e")
            || name.starts_with("_T")
    }

    fn demangle(
        &self,
        _arch: &CoreArchitecture,
        name: &str,
        _view: Option<Ref<BinaryView>>,
    ) -> Option<(QualifiedName, Option<Ref<Type>>)> {
        let ctx = swift_demangler::Context::new();
        let symbol = swift_demangler::Symbol::parse(&ctx, name)?;
        // Use the canonical demangled form from the parsed node tree.
        // This matches what `xcrun swift-demangle` produces.
        // TODO: Use the structured Symbol API to also reconstruct BN Types.
        let demangled = symbol.display();
        Some((QualifiedName::from(demangled), None))
    }
}
