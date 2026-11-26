# NSP package import and PFS0 inspection

## What an NSP is

An NSP is a package container whose root directory uses PFS0 (PartitionFS). The PFS0 directory lists files such as NCA content archives, tickets (`.tik`), certificates (`.cert`), and other metadata. The directory itself is readable separately from the NCA contents.

## Current import workflow

Windows:

    .\build\Release\lucidswitch2.exe --inspect-nsp .\your-game.nsp

Linux/macOS:

    ./build/lucidswitch2 --inspect-nsp ./your-game.nsp

The command verifies the PFS0 header, entry table, string table, filenames, data ranges, reserved fields, and overlapping ranges. It reports entry names, sizes, and counts for NCA, ticket, certificate, and CNMT-named NCA entries. The parser reads only the directory metadata; it does not read the NCA payloads.

## Library UI

The frontend file picker accepts `.nsp` files only. It reads the PFS0 directory metadata in the browser, stores the filename, size, and entry counts in local storage, and labels every imported package `Package recognized · not launchable yet`. The NSP bytes are not uploaded or persisted by the frontend.

## Why it cannot launch the title yet

An NSP being recognized is not the same as the title being executable. Its NCAs contain program, control, meta, and other content. The raw NCA header is encrypted; SwitchBrew documents AES-XTS encryption for the initial 0xC00 bytes, with further section encryption and content metadata. This project does not decrypt NCA headers or sections, process title-key material, or load protected content.

Beyond content loading, the engine still lacks the Horizon process runtime, complete AArch64 execution, service IPC, filesystem/input/audio/display services, and the guest GPU command processor. The current NRO runner is only a limited homebrew execution experiment and is not an NSP game launcher.

## Safety and compatibility boundaries

- NSP/PFS0 parsing is metadata-only.
- NCA contents are not opened, extracted, decrypted, or executed.
- Ticket and certificate entries are counted by filename only; their contents are not read.
- No production keys or title keys are imported, stored, searched, derived, or used.
- The current engine reports every NSP as not launchable.
- Switch 2 NSP support is not implemented; the PFS0 parser is only a generic container directory reader, not a Switch 2 title loader.

## Format references

- SwitchBrew NCA/PFS0 layout: https://switchbrew.org/wiki/NCA
- ReverseEngineering Wiki PFS0/HFS0 overview: https://reverseengineering.wiki/index.php/Format:PFS0_and_HFS0
