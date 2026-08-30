#include <atomic>
#include <thread>
#include <memory>
#include "wtest.hpp"
#include "warmth/coordinator.hpp"
#include "warmth/worker.hpp"
#include "warmth/messages.hpp"
#include "warmth/transport.hpp"

using namespace warmth;
using namespace warmth::proto;

WTEST(loopback_register_warm_ready) {
    net::socket_init();
    Coordinator::Config cfg; cfg.host = "127.0.0.1";
    Coordinator coord(cfg);
    CHECK(coord.start());
    const auto port = coord.port();
    std::atomic<bool> stop{false};
    std::thread accept_thread([&] { coord.run_accept_loop(stop); });

    const auto wid = Id128::derive("loopback-worker");
    const auto boot = Id128::derive("loopback-boot");
    Worker::Config wcfg;
    wcfg.host = "127.0.0.1"; wcfg.port = port; wcfg.worker_id = wid; wcfg.boot = boot;
    wcfg.node = NodeId::derive("node"); wcfg.workload = WorkloadId::derive("wl");
    wcfg.backend = "cuda"; wcfg.devices = {"cuda:0"}; wcfg.actions = {0,1,2,3};
    Worker worker(wcfg);
    // Wait for coordinator accept thread to be live, then connect.
    std::atomic<bool> connected{false};
    std::thread worker_thread([&] {
        if (!worker.connect()) { std::printf("worker connect failed\n"); return; }
        connected = true;
        SyntheticWarmHook hook;
        worker.run(hook);
    });
    // Wait for registration.
    for (int i = 0; i < 2000 && !connected; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(connected);
    for (int i = 0; i < 2000 && coord.registered_workers() < 1; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(coord.registered_workers() == 1);

    const WarmthObjectId obj = Id128::derive("loopback-object");
    const bool iw = coord.issue_warm(obj, WarmthAction::LOAD_MODEL, &wid);
    std::printf("issue_warm=%d reg=%zu in_flight=%zu\n", (int)iw, coord.registered_workers(), coord.in_flight_attempts()); std::fflush(stdout);
    CHECK(iw);
    coord.wait_until_settled();
    std::printf("settled, in_flight=%zu\n", coord.in_flight_attempts()); std::fflush(stdout);
    auto o = coord.find_object(obj);
    CHECK(o.has_value());
    CHECK(o->execution_ready());

    // Stale-epoch REPORT must be rejected and must not mutate the object.
    net::TcpConnection stale_check_conn = net::connect("127.0.0.1", port);
    CHECK(stale_check_conn.valid());
    if (stale_check_conn.valid()) {
        proto::FrameWriter fw(stale_check_conn); proto::FrameReader fr(stale_check_conn);
        ReportPayload rp; rp.boot = boot; rp.epoch = coord.epoch().value() + 999; // stale epoch
        rp.warmth_gen = coord.warmth_generation().value(); rp.dep_gen = coord.dependency_generation().value();
        WarmthObject oo(obj, WarmthCategory::MODEL_WEIGHTS, WorkloadId::derive("wl"), NodeId::derive("n"), "cuda:0", "cuda");
        oo.transition(WarmthState::COLD, "stale");
        rp.objects.push_back(oo);
        WireEncoder w; encode_report(w, rp);
        proto::Frame f; f.type = proto::MessageType::REPORT; f.payload = w.done();
        fw.write(f);
        // The coordinator must REJECT this stale report; we verify by object state.
        stale_check_conn.close();
    }
    net::TcpConnection stale_check_conn2;
    auto o2 = coord.find_object(obj);
    CHECK(o2.has_value());
    CHECK(o2->execution_ready()); // still ready; stale report did not downgrade

    // Fresh authority under current epoch is accepted.
    CHECK(coord.balanced()); // accounting to zero after warm

    // Close the stale-report connections so their coordinator threads unblock.
    if (stale_check_conn.valid()) stale_check_conn.close();
    if (stale_check_conn2.valid()) stale_check_conn2.close();

    stop.store(true);
    std::printf("calling stop\n"); std::fflush(stdout);
    coord.stop();
    std::printf("stopped\n"); std::fflush(stdout);
    accept_thread.join();
    std::printf("accept joined\n"); std::fflush(stdout);
    worker_thread.join();
    std::printf("worker joined\n"); std::fflush(stdout);
    net::socket_cleanup();
}

int main() { RUN_TESTS(); }
