# Research source notes

## User-supplied ROMSLAB link

The supplied site, https://romslab.com/, was reviewed as requested. The pages currently indexed under its emulator/tools section list Switch Army Knife, prod-key links for Yuzu/Ryujinx, and other utilities. Individual Switch game pages describe NSP/XCI downloads and workflows for using other emulators. These pages are useful as a record of the workflow the project aims eventually to replace, but they do not provide primary technical documentation for Switch CPU, memory, Horizon OS, executable loading, or GPU behavior.

LucidSwitch2 does not download game packages or keys from ROMSLAB. The site is not treated as a source of architecture specifications.

## Primary and community architecture references

- libnx NRO structure: https://github.com/switchbrew/libnx/blob/master/nx/include/switch/nro.h
- SwitchBrew NRO0 format notes: https://switchbrew.org/wiki/NRO0
- SwitchBrew Homebrew ABI notes: https://switchbrew.org/wiki/Homebrew_ABI
- SwitchBrew NSO0 format notes: https://switchbrew.org/wiki/NSO0
- SwitchBrew NCA format notes: https://switchbrew.org/wiki/NCA
- Nintendo official Switch 2 specifications: https://www.nintendo.com/us/gaming-systems/switch-2/tech-specs/

Use the official manufacturer specifications for official claims. Mark reverse-engineered implementation detail as community research. A format reference does not by itself establish loader compatibility.

- SwitchBrew NCA/PFS0 layout: https://switchbrew.org/wiki/NCA
- ReverseEngineering Wiki PFS0/HFS0 overview: https://reverseengineering.wiki/index.php/Format:PFS0_and_HFS0
