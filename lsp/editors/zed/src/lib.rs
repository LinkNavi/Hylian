use std::fs;
use zed_extension_api::{self as zed, LanguageServerId, Result};

struct HylianExtension {
    cached_binary_path: Option<String>,
}

impl HylianExtension {
    /// Walk up from the worktree root looking for the `hylian-lsp` binary.
    /// We check these locations in order:
    ///
    ///   1. `<worktree>/HylianLsp/hylian-lsp`          – built inside the project
    ///   2. `<worktree>/hylian-lsp`                     – project root convenience copy
    ///   3. Whatever `which hylian-lsp` resolves to     – installed system-wide
    ///
    /// Returns the absolute path to the binary, or an error string if none
    /// of the above exist.
    fn find_binary(&mut self, worktree: &zed::Worktree) -> Result<String> {
        // Return the cached path immediately if it still exists on disk.
        if let Some(ref path) = self.cached_binary_path {
            if fs::metadata(path).map(|m| m.is_file()).unwrap_or(false) {
                return Ok(path.clone());
            }
        }

        let worktree_root = worktree.root_path();

        // ── 1. HylianLsp/hylian-lsp (built inside the repo) ──────────────────
        let in_lsp_dir = format!("{}/HylianLsp/hylian-lsp", worktree_root);
        if fs::metadata(&in_lsp_dir)
            .map(|m| m.is_file())
            .unwrap_or(false)
        {
            self.cached_binary_path = Some(in_lsp_dir.clone());
            return Ok(in_lsp_dir);
        }

        // ── 2. hylian-lsp at the project root ─────────────────────────────────
        let at_root = format!("{}/hylian-lsp", worktree_root);
        if fs::metadata(&at_root)
            .map(|m| m.is_file())
            .unwrap_or(false)
        {
            self.cached_binary_path = Some(at_root.clone());
            return Ok(at_root);
        }

        // ── 3. System PATH via `which` ─────────────────────────────────────────
        if let Some(system_path) = worktree.which("hylian-lsp") {
            self.cached_binary_path = Some(system_path.clone());
            return Ok(system_path);
        }

        Err(
            "hylian-lsp not found. \
             Build it with `cd HylianLsp && bash build.sh` inside your project, \
             or place the binary on your PATH."
                .into(),
        )
    }
}

impl zed::Extension for HylianExtension {
    fn new() -> Self {
        Self {
            cached_binary_path: None,
        }
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<zed::Command> {
        let binary = self.find_binary(worktree)?;

        Ok(zed::Command {
            command: binary,
            args: vec![],
            env: vec![],
        })
    }

    fn language_server_initialization_options(
        &mut self,
        _language_server_id: &LanguageServerId,
        _worktree: &zed::Worktree,
    ) -> Result<Option<zed::serde_json::Value>> {
        // No custom initialization options needed — the LSP server
        // derives everything it needs from the workspace rootUri.
        Ok(None)
    }

    fn label_for_completion(
        &self,
        _language_server_id: &LanguageServerId,
        completion: zed::lsp::Completion,
    ) -> Option<zed::CodeLabel> {
        // Map LSP CompletionItemKind numbers to readable detail prefixes so
        // the completion menu looks tidy in Zed.
        let kind_prefix = match completion.kind? {
            zed::lsp::CompletionKind::Function | zed::lsp::CompletionKind::Method => "fn",
            zed::lsp::CompletionKind::Class | zed::lsp::CompletionKind::Struct => "class",
            zed::lsp::CompletionKind::Field | zed::lsp::CompletionKind::Property => "field",
            zed::lsp::CompletionKind::Variable => "let",
            zed::lsp::CompletionKind::Keyword => "kw",
            _ => return None,
        };

        let label = &completion.label;
        let detail = completion.detail.as_deref().unwrap_or("");

        // Display as e.g.  "fn takeDamage(int amount): void"
        // The `code` span covers the label; the detail is dimmed plain text.
        let display = if detail.is_empty() {
            format!("{} {}", kind_prefix, label)
        } else {
            format!("{} {}", kind_prefix, detail)
        };

        let highlight_end = kind_prefix.len() + 1 + label.len();

        Some(zed::CodeLabel {
            spans: vec![zed::CodeLabelSpan::code_range(0..highlight_end.min(display.len()))],
            filter_range: (kind_prefix.len() + 1..display.len()).into(),
            code: display,
        })
    }
}

zed::register_extension!(HylianExtension);
