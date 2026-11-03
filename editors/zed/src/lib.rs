// Starts dsls, the DScript language server (src/lsp/dsls_main.cpp in this repository).
// Where it is: the Zed setting lsp.dsls.binary.path if set, else dsls on the PATH.
//   "lsp": { "dsls": { "binary": { "path": "/path/to/DragonVM/bin/dsls" } } }
use zed_extension_api::{self as zed, settings::LspSettings, LanguageServerId, Result};

struct DragonVmExtension;

impl zed::Extension for DragonVmExtension {
    fn new() -> Self {
        DragonVmExtension
    }

    fn language_server_command(&mut self, language_server_id: &LanguageServerId, worktree: &zed::Worktree) -> Result<zed::Command> {
        let binary = LspSettings::for_worktree(language_server_id.as_ref(), worktree)
            .ok()
            .and_then(|settings| settings.binary);
        let path = binary
            .as_ref()
            .and_then(|b| b.path.clone())
            .or_else(|| worktree.which("dsls"))
            .ok_or_else(|| {
                "dsls (the DScript language server) was not found: add DragonVM's bin directory to the PATH, \
                 or set lsp.dsls.binary.path in the Zed settings"
                    .to_string()
            })?;
        let args = binary.and_then(|b| b.arguments).unwrap_or_default();
        Ok(zed::Command {
            command: path,
            args,
            env: worktree.shell_env(),
        })
    }
}

zed::register_extension!(DragonVmExtension);
