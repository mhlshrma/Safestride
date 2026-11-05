# Workspace Hygiene & File Organization

- **No Ephemeral / Scratch Files in Workspace Root**:
  Never create temporary scratch scripts, one-off inspection utilities, vector SVG sources, or intermediate text dumps directly in the project root directory.

- **Use 'Antigravity assets/' for Scratch & Intermediate Files**:
  All helper scripts, inspection dumps, intermediate test files, SVG sources, and conversion artifacts must be created inside a dedicated subfolder named `Antigravity assets/`.

- **Preserve Backups in 'Antigravity assets/Archive/'**:
  Old revisions, legacy drafts, and backups must reside in `Antigravity assets/Archive/` to keep the root directory free of duplicate clutter.

- **Ephemeral Cleanup**:
  If a temporary file is only required for a single verification step, clean it up immediately after the step completes.

- **Protect Project Deliverables**:
  Keep the root directory reserved exclusively for primary deliverables (e.g., `Hit2Help_Presentation.pptx`, `Project_Report.docx` / `updated H2H.docx`, official templates, and final rendered assets).
