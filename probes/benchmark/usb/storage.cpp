// Adapted from ut99-orbis (see LICENSE and README.md): export names only.
#include "storage.h"
#include <orbis/libkernel.h>
#include "libjbc.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>

namespace ps4 {
namespace {
unsigned ownedMounts;
unsigned ownedLogMounts;

bool gameReadable(const char* folder) {
    char file[600];
    const int length = std::snprintf(file, sizeof(file), "%s/System/UnrealTournament.exe", folder);
    if (length < 0 || size_t(length) >= sizeof(file)) return false;
    int fd = sceKernelOpen(file, O_RDONLY, 0);
    if (fd < 0) return false;
    char header[2]{};
    auto bytes = sceKernelRead(fd, header, sizeof(header));
    sceKernelClose(fd);
    return bytes == 2 && header[0] == 'M' && header[1] == 'Z';
}

bool restore(const jbc_cred& original) {
    // Do not continue to game startup when restoration fails.
    for (int attempt = 0; attempt < 3; ++attempt)
        if (!jbc_set_cred(&original)) return true;
    sceKernelDebugOutText(0, "EutherDrive USB: credential restoration failed\n");
    return false;
}

StorageResult result(StorageStatus status, const char* path = "") {
    StorageResult out;
    out.status = status;
    std::snprintf(out.path, sizeof(out.path), "%s", path);
    char message[600];
    std::snprintf(message, sizeof(message), "EutherDrive USB: status=%d path=%s\n", int(status), out.path);
    sceKernelDebugOutText(0, message);
    return out;
}
}

StorageResult resolveGameData(const char* preferred, bool allowSandboxMapping) {
    if (!preferred || preferred[0] != '/' || std::strlen(preferred) >= 480)
        return result(StorageStatus::InvalidPath);
    if (gameReadable(preferred)) return result(StorageStatus::Ready, preferred);
    // An explicit internal/custom path must not silently select unrelated USB data.
    if (std::strncmp(preferred, "/mnt/usb", 8) || preferred[8] < '0' || preferred[8] > '7' || preferred[9] != '/')
        return result(StorageStatus::NotFound);
    const char* suffix = preferred + 10;
    char candidates[8][512];
    char aliases[8][512];
    for (int slot = 0; slot < 8; ++slot) {
        std::snprintf(candidates[slot], sizeof(candidates[slot]), "/mnt/usb%d/%s", slot, suffix);
        std::snprintf(aliases[slot], sizeof(aliases[slot]), "/ut99_usb%d/%s", slot, suffix);
        if (gameReadable(candidates[slot])) return result(StorageStatus::Ready, candidates[slot]);
        if (gameReadable(aliases[slot])) return result(StorageStatus::Ready, aliases[slot]);
    }
    if (!allowSandboxMapping) return result(StorageStatus::NotFound);

    jbc_cred original{};
    if (jbc_get_cred(&original)) return result(StorageStatus::ServiceUnavailable);
    jbc_cred external = original;
    if (jbc_jailbreak_cred(&external) || jbc_set_cred(&external)) {
        return result(restore(original) ? StorageStatus::AccessFailed : StorageStatus::RestoreFailed);
    }
    unsigned present = 0;
    for (int slot = 0; slot < 8; ++slot)
        if (gameReadable(candidates[slot])) present |= 1u << slot;
    if (!restore(original)) return result(StorageStatus::RestoreFailed);
    if (!present) return result(StorageStatus::NotFound);

    for (int slot = 0; slot < 8; ++slot) {
        if (!(present & (1u << slot))) continue;
        char source[32], alias[32];
        std::snprintf(source, sizeof(source), "/mnt/usb%d", slot);
        std::snprintf(alias, sizeof(alias), "ut99_usb%d", slot);
        const int error = jbc_mount_in_sandbox(source, alias);
        if (!error) ownedMounts |= 1u << slot;
        // The helper temporarily changes credentials too, including on errors.
        if (!restore(original)) return result(StorageStatus::RestoreFailed);
        if (gameReadable(aliases[slot])) return result(StorageStatus::Ready, aliases[slot]);
    }
    return result(StorageStatus::MountFailed);
}

StorageResult resolveLogDirectory(bool allowSandboxMapping) {
    auto accessible = [](const char* root, char* directory) {
        OrbisKernelStat st{};
        if (sceKernelStat(root, &st) < 0 || !S_ISDIR(st.st_mode)) return false;
        std::snprintf(directory, 512, "%s/EutherDriveBench", root);
        sceKernelMkdir(directory, 0777);
        if (sceKernelStat(directory, &st) < 0 || !S_ISDIR(st.st_mode)) return false;
        // An existing directory can still be on a read-only mount. Verify writes
        // without truncating any existing file, then remove our own probe only.
        char probe[600];
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            std::snprintf(probe, sizeof probe, "%s/.write-test-%u", directory, attempt);
            const int fd = sceKernelOpen(probe, O_WRONLY | O_CREAT | O_EXCL, 0666);
            if (fd < 0) continue;
            bool ok = sceKernelWrite(fd, "1", 1) == 1;
            if (sceKernelFsync(fd) < 0) ok = false;
            if (sceKernelClose(fd) < 0) ok = false;
            if (sceKernelUnlink(probe) < 0) ok = false;
            return ok;
        }
        return false;
    };
    char root[32], directory[512];
    for (int slot=0;slot<8;++slot) {
        std::snprintf(root,sizeof root,"/mnt/usb%d",slot);
        if (accessible(root,directory)) return result(StorageStatus::Ready,directory);
        std::snprintf(root,sizeof root,"/eutherbench_logs%d",slot);
        if (accessible(root,directory)) return result(StorageStatus::Ready,directory);
    }
    if (!allowSandboxMapping) return result(StorageStatus::NotFound);
    jbc_cred original{};
    if (jbc_get_cred(&original)) return result(StorageStatus::ServiceUnavailable);
    jbc_cred external=original;
    if (jbc_jailbreak_cred(&external) || jbc_set_cred(&external))
        return result(restore(original) ? StorageStatus::AccessFailed : StorageStatus::RestoreFailed);
    unsigned present=0;
    for (int slot=0;slot<8;++slot) {
        std::snprintf(root,sizeof root,"/mnt/usb%d",slot);
        OrbisKernelStat st{};
        if (sceKernelStat(root,&st)>=0 && S_ISDIR(st.st_mode)) present |= 1u<<slot;
    }
    if (!restore(original)) return result(StorageStatus::RestoreFailed);
    for (int slot=0;slot<8;++slot) {
        if (!(present & (1u<<slot))) continue;
        char alias[32];
        std::snprintf(root,sizeof root,"/mnt/usb%d",slot);
        // Separate alias: resolveGameData may already own a read-only mapping.
        std::snprintf(alias,sizeof alias,"eutherbench_logs%d",slot);
        if (!jbc_mount_in_sandbox_rw(root,alias)) ownedLogMounts |= 1u<<slot;
        if (!restore(original)) return result(StorageStatus::RestoreFailed);
        std::snprintf(root,sizeof root,"/eutherbench_logs%d",slot);
        if (accessible(root,directory)) return result(StorageStatus::Ready,directory);
    }
    return result(present ? StorageStatus::MountFailed : StorageStatus::NotFound);
}

void releaseGameDataMounts() {
    if (!ownedMounts && !ownedLogMounts) return;
    jbc_cred original{};
    if (jbc_get_cred(&original)) return;
    for (int slot = 0; slot < 8; ++slot) {
        if (ownedLogMounts & (1u << slot)) {
            char name[32];
            std::snprintf(name, sizeof(name), "eutherbench_logs%d", slot);
            const int error = jbc_unmount_in_sandbox(name);
            if (!restore(original)) return;
            if (!error) ownedLogMounts &= ~(1u << slot);
        }
        if (!(ownedMounts & (1u << slot))) continue;
        char name[32];
        std::snprintf(name, sizeof(name), "ut99_usb%d", slot);
        const int error = jbc_unmount_in_sandbox(name);
        if (!restore(original)) return;
        if (!error) ownedMounts &= ~(1u << slot);
    }
}
}
