use binaryninja::add_optional_plugin_dependency;

#[no_mangle]
#[allow(non_snake_case)]
pub extern "C" fn CorePluginDependencies() {
    add_optional_plugin_dependency("arch_x86");
    add_optional_plugin_dependency("arch_arm64");
}

#[no_mangle]
#[allow(non_snake_case)]
pub extern "C" fn CorePluginInit() -> bool {
    binaryninja::tracing_init!("Plugin.Swift");

    true
}
