/**
 * @file openmp_affinity_probe.cpp
 * @brief Observe one standalone Linux OpenMP team, never run ARCH physics.
 * Workflow:
 * 1. Inherit the caller's allowed CPU set and explicit OpenMP environment.
 * 2. Record each actual team member's CPU, place and scheduler affinity.
 * 3. Emit observations only; do not infer physical/P-E cores or ARCH teams.
 */
#include <omp.h>
#include <sched.h>
#include <iostream>
#include <vector>
#include <utility>

struct Member {
    int thread=-1,cpu=-1,place=-1;
    std::vector<int> affinity;
    bool readable=false;
};
int main() {
    std::vector<Member> members;
    int team=0;
#pragma omp parallel
    {
#pragma omp single
        {team=omp_get_num_threads();members.resize(team);}
        const int id=omp_get_thread_num();
        Member m;m.thread=id;m.cpu=sched_getcpu();m.place=omp_get_place_num();
        cpu_set_t mask;CPU_ZERO(&mask);
        m.readable=sched_getaffinity(0,sizeof(mask),&mask)==0;
        if(m.readable)for(int i=0;i<CPU_SETSIZE;++i)if(CPU_ISSET(i,&mask))m.affinity.push_back(i);
        members[id]=std::move(m);
    }
    std::cout<<"{\"openmp_version\":"<<_OPENMP<<",\"team_size\":"<<team
        <<",\"num_places\":"<<omp_get_num_places()<<",\"proc_bind\":"<<int(omp_get_proc_bind())
        <<",\"members\":[";
    for(int i=0;i<team;++i){
        if(i)std::cout<<',';
        const auto& m=members[i];
        std::cout<<"{\"thread\":"<<m.thread<<",\"cpu\":"<<m.cpu<<",\"place\":"<<m.place
            <<",\"affinity_readable\":"<<(m.readable?"true":"false")<<",\"affinity\":[";
        for(std::size_t j=0;j<m.affinity.size();++j){if(j)std::cout<<',';std::cout<<m.affinity[j];}
        std::cout<<"]}";
    }
    std::cout<<"]}\n";
}
