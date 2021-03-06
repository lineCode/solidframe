#include "solid/system/exception.hpp"
#include "solid/utility/workpool.hpp"
#include <atomic>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

using namespace solid;
using namespace std;
namespace {
const LoggerT logger("test_basic");
}

int test_workpool_basic(int argc, char* argv[])
{
    solid::log_start(std::cerr, {".*:EWS", "test_basic:VIEWS"});
    using WorkPoolT  = WorkPool<size_t>;
    using AtomicPWPT = std::atomic<WorkPoolT*>;
    
    solid_log(logger, Statistic, "thread concurrency: "<<thread::hardware_concurrency());
    
    const int           wait_seconds = 500;
    int                 loop_cnt     = 5;
    const size_t        cnt{5000000};
    const size_t        v = (((cnt - 1) * cnt)) / 2;
    std::atomic<size_t> val{0};
    promise<void>       prom;
    AtomicPWPT          pwp{nullptr};
    thread              wait_thread(
        [](promise<void>& _rprom, AtomicPWPT& _rpwp, const int _wait_time_seconds) {
            if (_rprom.get_future().wait_for(chrono::seconds(_wait_time_seconds)) != future_status::ready) {
                if (_rpwp != nullptr) {
                    _rpwp.load()->dumpStatistics();
                }
                solid_throw(" Test is taking too long - waited " << _wait_time_seconds << " secs");
            }
        },
        std::ref(prom), std::ref(pwp), wait_seconds);

    if (argc > 1) {
        loop_cnt = atoi(argv[1]);
    }

    for (int i = 0; i < loop_cnt; ++i) {
        {
            WorkPoolT wp{
                2,
                WorkPoolConfiguration(),
                [&val](const size_t _v) {
                    val += _v;
                }};
            pwp = &wp;
            for (size_t i = 0; i < cnt; ++i) {
                wp.push(i);
            };
            pwp = nullptr;
        }
        solid_log(logger, Verbose, "after loop");
        solid_check(v == val, "val = " << val << " v = " << v);
        val = 0;
    }
    prom.set_value();
    solid_log(logger, Verbose, "after promise set value - before join");
    wait_thread.join();
    solid_log(logger, Verbose, "after join");

    return 0;
}
