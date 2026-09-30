// Adapted from ut99-orbis (see LICENSE and README.md): export names only.
#include "log_export.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ps4 {
namespace {
constexpr off_t limit = 16 * 1024 * 1024;
bool writeAll(int fd, const void* data, size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size) {
        const auto n = write(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n; size -= size_t(n);
    }
    return true;
}
bool safeName(const char* name) {
    return name && *name && std::strcmp(name,".") && std::strcmp(name,"..") &&
        !std::strchr(name,'/') && !std::strchr(name,'\\');
}
bool copyLog(const LogSource& source, const char* destination, off_t& total, off_t& start) {
    total = start = -1;
    int in = open(source.path, O_RDONLY);
    if (in < 0) return false;
    total = lseek(in, 0, SEEK_END);
    if (source.wholeFile && total > limit) { close(in); return false; }
    start = total > limit ? total-limit : 0;
    if (total < 0 || lseek(in,start,SEEK_SET) < 0) { close(in); return false; }
    char partial[600];
    if (std::snprintf(partial,sizeof partial,"%s.partial",destination) >= int(sizeof partial)) { close(in); return false; }
    int out = open(partial, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (out < 0) { close(in); return false; }
    bool ok = true;
    off_t remaining = total-start;
    char buffer[16384];
    while (remaining > 0) {
        const auto n = read(in,buffer,remaining > off_t(sizeof buffer) ? sizeof buffer : size_t(remaining));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || !writeAll(out,buffer,size_t(n))) { ok=false; break; }
        remaining -= n;
    }
    if (ok && fsync(out)) ok=false;
    if (close(out)) ok=false;
    close(in);
    if (ok && rename(partial,destination)) ok=false;
    return ok;
}
}
LogExportResult exportLogs(const char* root, const LogSource* sources, size_t count) {
    LogExportResult result;
    result.failed = unsigned(count);
    if (!root || !sources || !count) return result;
    bool created = false;
    for (unsigned run=1;run<=9999;++run) {
        if (std::snprintf(result.directory,sizeof result.directory,"%s/run-%04u",root,run) >= int(sizeof result.directory)) return result;
        if (mkdir(result.directory,0777)==0) { created=true; break; }
        if (errno != EEXIST) return result;
    }
    if (!created) return result;
    char reportPath[600];
    std::snprintf(reportPath,sizeof reportPath,"%s/export.txt",result.directory);
    int report = open(reportPath,O_WRONLY|O_CREAT|O_EXCL,0666);
    if (report < 0) return result;
    const char* header = "EutherDrive benchmark export 0.01\nExisting internal logs; may predate this run. Not a crash memory dump.\nText logs: last 16 MiB. Binary snapshots: whole file or failure, never truncated.\n";
    bool reportOk=writeAll(report,header,std::strlen(header));
    for (size_t i=0;i<count;++i) {
        off_t total=-1,start=-1;
        char destination[600],line[1200];
        const bool named=safeName(sources[i].name) &&
            std::snprintf(destination,sizeof destination,"%s/%s",result.directory,sources[i].name)<int(sizeof destination);
        struct stat sourceStat{};
        const bool missing = named && sources[i].optional &&
            stat(sources[i].path,&sourceStat) != 0 && errno == ENOENT;
        if (missing) { ++result.skipped; --result.failed; }
        bool ok=!missing && named && copyLog(sources[i],destination,total,start);
        if (ok) { ++result.copied; --result.failed; }
        const int n=std::snprintf(line,sizeof line,"%s: %s; source=%s; source_bytes=%lld; copied_from=%lld\n",
            sources[i].name ? sources[i].name : "(invalid)",ok?"OK":(missing?"ABSENT (optional)":"FAILED"),sources[i].path,
            static_cast<long long>(total),static_cast<long long>(start));
        reportOk = n>=0 && n<int(sizeof line) && writeAll(report,line,size_t(n)) && reportOk;
    }
    if (fsync(report)) reportOk=false;
    if (close(report)) reportOk=false;
    result.reportSaved=reportOk;
    return result;
}
}
