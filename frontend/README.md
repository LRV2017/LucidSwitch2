# LucidSwitch2 UI prototype

Open `index.html` in a modern browser. It contains a console-inspired home screen, game library, settings page, local `.nsp` file picker, PFS0 directory validation, and basic Gamepad API detection.

When an `.nsp` is selected, the UI reads only the PFS0 header, entry table, and string table, validates file ranges, and saves a summary (filename, size, entry count, NCA count, ticket count, and certificate count) to browser local storage. The package bytes are not uploaded or stored by the UI. Imported items are marked `Package recognized · not launchable yet` because the engine does not process NCA content or provide the Horizon runtime.

This is a visual/interaction prototype, not the final native desktop UI. A native shell and a secure local file-provider API are still required. The frontend does not decrypt, install, or launch games.
