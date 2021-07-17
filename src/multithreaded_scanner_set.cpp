#include "multithreaded_scanner_set.h"

#include <thread>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include <time.h>

int show_free()
{
    int rc;
    u_int page_size;
    struct vmtotal vmt;
    size_t vmt_size, uint_size;
    size_t val64, val64_size;

    vmt_size = sizeof(vmt);
    uint_size = sizeof(page_size);
    val64_size = sizeof(val64);

    val64=0;
    rc = sysctlbyname("hw.memsize", &val64, &val64_size, NULL, 0);
    if (rc==0) {
        std::cerr << "memsize: " << val64 << "\n";
    }

    val64=0;
    rc = sysctlbyname("vm.memory_pressure", &val64, &val64_size, NULL, 0);
    if (rc==0) {
        std::cerr << "memory_pressure: " << val64 << "\n";
    }

    rc = sysctlbyname("vm.vmtotal", &vmt, &vmt_size, NULL, 0);
    if (rc ==0){
        rc = sysctlbyname("vm.stats.vm.v_page_size", &page_size, &uint_size, NULL, 0);
        if (rc ==0 ){
            std::cerr << "Free memory       : " <<  vmt.t_free * page_size << "\n";
            std::cerr << "Available memory  : " << vmt.t_avm * page_size << "\n";
        }
    }
    return 0;
}

/*
 * Called in the worker thread to process the sbuf.
 */
void multithreaded_scanner_set::work_unit::process() const
{
    std::cerr << std::this_thread::get_id() << " multithreaded_scanner_set::work_unit::process " << *sbuf << "\n";
    ss.process_sbuf(sbuf);
}

multithreaded_scanner_set::multithreaded_scanner_set(const scanner_config& sc_,
                                                     const feature_recorder_set::flags_t& f_, class dfxml_writer* writer_):
    scanner_set(sc_, f_, writer_)
{
}


void multithreaded_scanner_set::set_status(const std::string &status)
{
    if (tp) {
        tp->set_status(status);
    }
}

void multithreaded_scanner_set::process_sbuf(sbuf_t *sbufp)
{
    set_status(sbufp->pos0.str() + " process_sbuf");
    scanner_set::process_sbuf(sbufp);
}

void multithreaded_scanner_set::schedule_sbuf(sbuf_t *sbufp)
{
    /* Run in same thread? */
    if (tp==nullptr || (sbufp->depth() > 0 && sbufp->bufsize < SAME_THREAD_SBUF_SIZE)) {
        std::cerr << std::this_thread::get_id() << " same thread: " << sbufp->pos0 << " len=" << sbufp->bufsize << "\n";
        process_sbuf(sbufp);
        return;
    }

    std::cerr << std::this_thread::get_id() << "                new thread: " << sbufp->pos0 << " depth: " << sbufp->depth() << "\n";
    if (sbufp->depth()==0) {
        sbuf_depth0 += 1;
    }

    /* Run in a different thread */
    struct work_unit wu(*this, sbufp);
    tp->push( [wu]{ wu.process(); } );
    print_tp_status();
}

void multithreaded_scanner_set::delete_sbuf(sbuf_t *sbufp)
{
    if (sbufp->depth()==0){
        sbuf_depth0 -= 1;
        std::cerr << std::this_thread::get_id() << " deleted sbuf. " << *sbufp << " total depth0 now=" << sbuf_depth0 << "\n";
    }
    set_status(sbufp->pos0.str() + " delete_sbuf");
    scanner_set::delete_sbuf(sbufp);
}

void multithreaded_scanner_set::launch_workers(int count)
{
    tp = new threadpool(count);
}

void multithreaded_scanner_set::join()
{
    if (tp != nullptr) {
        tp->join();
    }
}

void multithreaded_scanner_set::notify_thread()
{
    while(true){
        time_t rawtime = time (0);
        struct tm *timeinfo = localtime (&rawtime);
        std::cerr << asctime(timeinfo) << "\n";
        print_tp_status();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void multithreaded_scanner_set::run_notify_thread()
{
    nt = new std::thread(&multithreaded_scanner_set::notify_thread, this);
}


/*
 * Print the status of each thread in the threadpool.
 */
void multithreaded_scanner_set::print_tp_status()
{
    std::cerr << "thread count " << tp->thread_count() << "\n";
    std::cerr << "active count " << tp->active_count() << "\n";
    std::cerr << "task count " << tp->task_count() << "\n";
    std::cerr << "total depth0 " << sbuf_depth0 << "\n";
    show_free();
    tp->dump_status(std::cerr);
    std::cout << "-------------------\n";
}