# Security policy

## Preview commands

The interactive \`--preview COMMAND\` option deliberately evaluates a command through the user's shell. Treat the command itself as trusted code: it can run programs, access files, inherit the process environment, and block the menu while it runs. Preview commands are not sandboxed and have no execution timeout. Do not run a command supplied by an untrusted party.

The \`{}\`, \`{index}\`, and \`{query}\` placeholders are replaced with shell-quoted values for the selected record, its original zero-based index, and the current query. Do not add your own shell quotes around these placeholders; mmenu applies quoting automatically. This protects data inserted through placeholders, but it does not make an untrusted preview command safe.

Preview output is bounded to a fixed display buffer and terminal control bytes are sanitized before rendering. This is display hardening, not a security boundary for the command itself.

## Input and API boundaries

- Use mmenu with the privileges required for the task, not elevated privileges by default.
- Batch matching is byte-oriented; embedded NUL records are rejected. \`--read0\`/\`--print0\` support NUL as a record delimiter, not as data inside a record.
- The C API validates public arguments before initializing ncurses. Callers must free the index array returned by \`mmenuMulti\`/\`mmenuMultiConfigured\`.
- The preview callback API is generic. Applications that provide their own callback are responsible for the safety of that callback and its dependencies.

## Reporting a vulnerability

Please avoid publishing exploit details before a fix is available. Use GitHub's private vulnerability reporting for this repository if it is enabled; otherwise contact the maintainer privately through their GitHub profile. Include the affected revision, reproduction steps, and impact. Do not include credentials or unrelated private data.
