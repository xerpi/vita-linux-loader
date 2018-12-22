# PlayStation Vita Linux Loader

## What's this?

This is a kernel plugin that lets you run Linux in ARMv7 non-secure System mode.

## How does it work?

At first, the plugin allocates a couple of physically contiguous buffers where it loads the Linux kernel image and the [Device Tree Blob](https://en.wikipedia.org/wiki/Device_tree).

Then it triggers a power standby request and when PSVita OS is about to send the [Syscon](https://wiki.henkaku.xyz/vita/Syscon)
command to actually perform the standby, it changes the request type into a soft-reset and the resume routine address to a custom one (`resume.s`).

Once the PSVita wakes from the soft-reset, the custom resume routine executes and identity maps the scratchpad (address [0x1F000000](https://wiki.henkaku.xyz/vita/Physical_Memory)) using a 1MiB section. Afterwards, the Linux bootstrap code (`linux_bootstrap.s`) is copied to the scratchpad where it proceeds and jumps to (passing some parameters such as the Linux and DTB physical addresses).

Since the Linux bootstrap code is now in an identity-mapped location, it can proceed to disable the MMU (and the caches) and finally jump to the Linux kernel.

## Instructions

You will need a compiled Linux kernel image (placed at `ux0:/linux/zImage`) and the corresponding DTB file (placed at `ux0:/linux/vita.dtb`).

## Debugging

This Linux loader will print debug info over UART0. Check [UART Console](https://wiki.henkaku.xyz/vita/UART_Console) for the location of the pins.

## Credits
Thanks to everybody who has helped me, specially the [Team Molecule](https://twitter.com/teammolecule) (formed by [Davee](https://twitter.com/DaveeFTW), Proxima, [xyz](https://twitter.com/pomfpomfpomf3), and [YifanLu](https://twitter.com/yifanlu)), [TheFloW](https://twitter.com/theflow0), [motoharu](https://github.com/motoharu-gosuto), and everybody at the [HENkaku](https://discord.gg/m7MwpKA) Discord channel.
