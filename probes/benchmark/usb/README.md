USB storage and bounded atomic log export adapted from ut99-orbis
platform/ps4/{storage,log_export}.{cpp,h}, local working-tree snapshot.
The last committed storage baseline was
1b3fa52781e45c054bd21c2f26ea429ff2b6bdbc, but storage.cpp/storage.h were modified
and log_export.cpp/log_export.h were untracked at capture. The original file
hashes below identify the code actually reused. See LICENSE.
Changes: separate EutherDriveBench directory, writable sandbox alias names,
and diagnostic labels. The private pinned libjbc dependency is built by the
existing ut99-orbis/scripts/build-ps4-usb.py adapter. No runtime binaries vendored.

Original file SHA256 values:

ea011e6148a9977c9e421237c7dc70a59ca6b8cfb9b0dc3b05ef1c23dee9d6e8  storage.cpp
12bce65310c2ec47836dd96bbc70b24c0737d0a50c6c4817fa31ac7216f0cdce  storage.h
77234043107fcaccb8b9ffa6330f81783796c57128db9e591c113ba25727a3cd  log_export.cpp
cb3e9d285aafe01dd67a8d1310a5ad038960e2fc329975e01b342623b573873c  log_export.h
