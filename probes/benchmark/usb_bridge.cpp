// SPDX-License-Identifier: MIT
#include "usb/storage.h"
#include "usb/log_export.h"
#include <cstdio>
#include <cstring>
extern "C" void benchmark_note(const char* message);
static ps4::StorageResult usb;
static char last_directory[512];
extern "C" int benchmark_prepare_usb(void) {
    usb=ps4::resolveLogDirectory(true);
    char line[640];
    std::snprintf(line,sizeof line,"USB status=%d path=%s",int(usb.status),usb.path);
    benchmark_note(line);
    return usb.status == ps4::StorageStatus::RestoreFailed ? -1 : 0;
}
extern "C" int benchmark_export(const char* directory) {
    if (usb.status != ps4::StorageStatus::Ready) return 0;
    if (!directory || std::strncmp(directory,"/data/eutherdrive-bench/run-",std::strlen("/data/eutherdrive-bench/run-")) ||
        std::strstr(directory,"..") || std::strlen(directory)>400) return -1;
    std::snprintf(last_directory,sizeof last_directory,"%s",directory);
    ps4::LogSource sources[26];
    char paths[26][512], names[26][40];
    size_t count=0;
    auto add=[&](const char* name, bool optional) {
        std::snprintf(names[count],sizeof names[count],"%s",name);
        std::snprintf(paths[count],sizeof paths[count],"%s/%s",directory,name);
        sources[count]={paths[count],names[count],optional,true}; ++count;
    };
    add("summary.log",false); add("build-info.json",false); add("suite.tsv",false);
    add("complete.txt",true);
    for(int game=0;game<5;++game) for(int repeat=1;repeat<=2;++repeat) {
        char name[40];
        std::snprintf(name,sizeof name,"case-%02d-r%d.log",game,repeat); add(name,true);
        std::snprintf(name,sizeof name,"case-%02d-r%d.ppm",game,repeat); add(name,true);
    }
    sources[count++]={"/data/eutherdrive-bench/native-probe.log","native-probe.log",false,false};
    const auto result=ps4::exportLogs(usb.path,sources,count);
    char line[640];
    std::snprintf(line,sizeof line,"USB snapshot %u copied, %u errors: %s",result.copied,result.failed,result.directory);
    benchmark_note(line);
    return result.failed || !result.reportSaved ? -1 : int(result.copied);
}
extern "C" void benchmark_finish(void) { if (*last_directory) { char path[512]; std::snprintf(path,sizeof path,"%s",last_directory); benchmark_export(path); } }
extern "C" void benchmark_release_usb(void) { ps4::releaseGameDataMounts(); }
