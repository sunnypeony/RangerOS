#!/usr/bin/env bash
# Record host and toolchain versions for docs/evidence.
set -u
echo "date:          $(date -Iseconds)"
echo "host:          $(. /etc/os-release; echo "$PRETTY_NAME") / $(uname -r) / $(uname -m)"
echo "clang:         $(clang --version | head -1)"
echo "ld.lld:        $(ld.lld --version | head -1)"
echo "nasm:          $(nasm -v)"
echo "readelf:       $(readelf --version | head -1)"
echo "nm:            $(nm --version | head -1)"
echo "grub-mkrescue: $(grub-mkrescue --version)"
echo "grub-file:     $(dpkg-query -W -f='grub-common ${Version} ${Architecture}' grub-common)"
echo "grub i386-pc:  $(sha256sum tools/grub/grub-pc-bin_2.04-1ubuntu26.17_amd64.deb | cut -d' ' -f1) grub-pc-bin_2.04-1ubuntu26.17_amd64.deb"
echo "xorriso:       $(xorriso -version 2>/dev/null | head -1)"
echo "mtools:        $(mformat --version | head -1)"
echo "qemu:          $(qemu-system-i386 --version | head -1)"
echo "make:          $(make --version | head -1)"
echo "python3:       $(python3 --version)"
