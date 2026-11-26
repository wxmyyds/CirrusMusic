# Cirrus: third-party software

Cirrus is a third-party NetEase Cloud Music client. It is not affiliated with, endorsed
by or sponsored by NetEase. Cirrus itself is licensed under the MIT License (`LICENSE`
next to this file).

Cirrus uses the following software. The license texts are in `licenses/` next to this
file. The upstream LGPL sources are unmodified. The application dynamically links the
bundled libraries. On Fedora, the private EAC3 shared library contains statically linked
FFmpeg objects; its complete bridge sources and build configuration are under
`packaging/linux/fedora/` in the Cirrus source. Compatible shared libraries can be replaced
in the package's `lib/` or `lib64/`, next to `Cirrus.exe`, or in the macOS Frameworks directory.

| Component | Version | License | Use |
| --- | --- | --- | --- |
| Qt | Distribution version on Linux; 6.11 on Windows/macOS | Bundled builds: LGPL-3.0-only (`licenses/LGPL-3.0-only.txt`, `licenses/GPL-3.0-only.txt`); system builds: distribution package terms | Application framework |
| FFmpeg (Qt Multimedia, Windows/macOS) | 7.1 | LGPL-2.1-or-later (`licenses/LGPL-2.1-or-later.txt`) | Qt Multimedia backend |
| FFmpeg (Cirrus decoder) | Distribution version on Linux; 9.0 on Windows/macOS | Bundled builds: LGPL-2.1-or-later (`licenses/LGPL-2.1-or-later.txt`); system builds: distribution package terms | Audio decoding (MP3, FLAC, AAC; E-AC-3 when available) |
| FFmpeg (Fedora private EAC3 backend) | 9.0.2 | LGPL-2.1-or-later (`licenses/LGPL-2.1-or-later.txt`) | EAC3-only application entrypoint; includes upstream-required AC3 dependencies |
| QtKeychain | Distribution version on Linux; 0.17 on Windows/macOS | BSD-3-Clause (below) | Storing the sign-in in the system keyring |
| OpenSSL | 3 | Apache-2.0 (`licenses/Apache-2.0.txt`) | Cryptography for the service protocol |
| zlib | 1.x | Zlib (below) | Decompression |
| Zstandard (zstd) | 1.5 | BSD-3-Clause (below) | Compressing the listening logs |
| Material Symbols (Google) | 2.973 | Apache-2.0 | Icons; subset to the glyphs Cirrus uses |
| material-color-utilities (Google) | `5b3618b` | Apache-2.0 | Colour schemes |
| m3shapes | `8a6fe89` | Apache-2.0 | Shapes and morphing |
| QR Code generator library (Project Nayuki) | 1.8.0 | MIT (`licenses/qrcodegen-MIT.txt`) | Sign-in QR code |
| NeteaseCloudMusicApi Enhanced | `a8c781f` | MIT (`licenses/api-enhanced-MIT.txt`) | Reference for the service's interface |

Which libraries a package carries:

- **deb and RPM** use official distribution Qt, QtKeychain, FFmpeg, OpenSSL, zlib,
  zstd and libsecret packages, including native QML and Qt plugin runtime dependencies.
  ICU comes through the distribution's Qt dependencies; Cirrus does not bundle it or
  specify an ICU version. Debian uses an application-only Qt 6.8 compatibility patch.
  Fedora additionally carries `libcirrus_eac3.so.1`: a private, symbol-isolated EAC3
  decoder selected by a Fedora-only Cirrus build patch. Other audio continues to use
  the official system libraries.
- **Windows installer and zip, macOS dmg** carry Qt, both FFmpeg builds and
  QtKeychain (on Windows linked into `Cirrus.exe`). The Windows packages also carry OpenSSL,
  zlib, zstd and the Microsoft Visual C++ runtime; the macOS dmg also carries OpenSSL and
  zstd, and uses the system's zlib.
- **Native Arch packages** use the distribution's libraries.

## Sources

The bundled libraries are unmodified releases:

- Qt 6.11: <https://download.qt.io/archive/qt/6.11/> (the official binaries; Qt
  Multimedia's FFmpeg 7.1 is part of them).
- FFmpeg 9.0.2: <https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz>. macOS packages
  use the configuration in `packaging/build-ffmpeg.sh` of the Cirrus source (shared,
  LGPL, MP3, FLAC, and MP4/MOV AAC/E-AC-3); Windows packages use vcpkg's `ffmpeg` port with the features
  `avcodec`, `avformat` and `swresample` (no `gpl`). Fedora's private EAC3 build uses
  `packaging/linux/fedora/build-eac3-ffmpeg.sh`; upstream FFmpeg itself is not patched.
  It enables EAC3 and the AC3/parser dependencies selected by upstream configure,
  with only avcodec, avutil and swresample linked into the isolated bridge.
- Linux Qt, QtKeychain, ICU and FFmpeg: matching source packages from Arch Linux /
  Arch Linux ARM, Debian 13 and Fedora 44 official repositories. Cirrus does not
  redistribute their shared libraries. Fedora's isolated EAC3 bridge is the only
  additional private FFmpeg component on Linux; its build sources are included above.
- QtKeychain 0.17.0: <https://github.com/frankosterfeld/qtkeychain/releases/tag/0.17.0>
- OpenSSL 3, zlib and zstd on Windows: vcpkg's `openssl`, `zlib` and `zstd` ports. OpenSSL 3
  and zstd on macOS: Homebrew's `openssl@3` and `zstd`.

On request, the maintainer provides the exact source of every LGPL library in a
release for at least three years after it.

## QtKeychain

```
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to
   endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## zlib

```
(C) 1995-2024 Jean-loup Gailly and Mark Adler

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## Zstandard

```
BSD License

For Zstandard software

Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name Facebook, nor Meta, nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
