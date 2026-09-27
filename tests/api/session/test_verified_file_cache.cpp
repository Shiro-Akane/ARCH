#include "core/files/FileFingerprint.h"
#include "core/files/VerifiedFileCache.h"
#include "api/resources/WorkerLimits.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cmath>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace arch::core;
static void require(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
static void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc); file<<bytes;
    if(!file) throw std::runtime_error("Cannot write fixture");
}
int main(int argc,char** argv) {
    require(argc==2,"fixture directory required");
    std::filesystem::path dir=argv[1]; std::filesystem::create_directories(dir);
    const auto path=dir/"source",other=dir/"other";
    VerifiedFileCache cache(16*1024);
    {
        VerifiedFileCacheScope session(cache);
        write(path,"abc");
        const std::string abc="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        require(file_sha256(path.string())==abc,"cold digest differs from SHA-256 reference");
        require(file_sha256(path.string())==abc && cache.hits==1 && cache.hashes==1,"warm comparison did not reuse digest");
        auto stamp=std::filesystem::last_write_time(path);
        write(path,"abd"); std::filesystem::last_write_time(path,stamp);
        require(file_sha256(path.string())==string_sha256("abd") && cache.hashes==2,"same size/mtime hid content change");
        write(path,"");
        require(file_sha256(path.string())=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","truncation lost");
        require(file_sha256(path.string())==string_sha256(""),"empty cache hit failed");
        write(path,std::string(128*1024,'x'));
        const auto hashes=cache.hashes;
        require(file_sha256(path.string())==string_sha256(std::string(128*1024,'x')),"oversize digest failed");
        file_sha256(path.string());
        require(cache.hashes==hashes+2 && cache.retained_bytes()<=cache.max_bytes,"oversize source bypassed cache budget");
        write(path,std::string(12*1024,'a')); file_sha256(path.string());
        write(other,std::string(12*1024,'b')); file_sha256(other.string());
        require(cache.retained_bytes()<=cache.max_bytes,"multiple sources exceeded cache budget");
        {
            VerifiedFileCache nested(32);
            VerifiedFileCacheScope scope(nested);
            write(other,"new"); file_sha256(other.string());
            require(nested.hashes==1,"nested cache inactive");
        }
        const auto h=cache.hashes; file_sha256(other.string());
        require(cache.hashes==h+1,"outer cache not restored");
        std::filesystem::remove(other);
        bool missing=false; try { file_sha256(other.string()); } catch(const std::exception&) { missing=true; }
        require(missing,"missing file reused prior digest");
        cache.clear(); require(cache.retained_bytes()==0,"reset retained bytes");
    }
    const auto hashes=cache.hashes,hits=cache.hits;
    file_sha256(path.string());
    require(cache.hashes==hashes && cache.hits==hits,"session cache leaked into ordinary hashing");
#ifdef __linux__
    const auto child=fork(); require(child>=0,"cannot fork CPU limit test");
    if(child==0) {
        try {
            arch::api::SessionProcessLimits limits;
            rlimit current{};
            limits.begin_request(1);
            require(getrlimit(RLIMIT_CPU,&current)==0 && current.rlim_cur<10,"short request budget missing");
            // A short request must not permanently lower the hard ceiling for
            // the next initialization request in the same process.
            limits.begin_request(300);
            require(getrlimit(RLIMIT_CPU,&current)==0 && current.rlim_cur>=300,"cumulative CPU ceiling poisoned next request");
            _exit(0);
        } catch(...) { _exit(1); }
    }
    int status=0; require(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"session request budgets failed");
#endif
}
