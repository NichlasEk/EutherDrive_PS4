#pragma once
#include <cstddef>
namespace ps4 {
struct LogSource { const char* path; const char* name; bool optional = false; bool wholeFile = false; };
struct LogExportResult {
    char directory[512]{};
    unsigned copied = 0;
    unsigned failed = 0;
    unsigned skipped = 0;
    bool reportSaved = false;
};
// Copies a bounded snapshot to a fresh directory. Never overwrites older runs.
LogExportResult exportLogs(const char* root, const LogSource* sources, size_t count);
}
