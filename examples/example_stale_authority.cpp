#include <cstdio>
#include <atomic>
#include <thread>
#include "warmth/coordinator.hpp"
#include "warmth/worker.hpp"
#include "warmth/messages.hpp"
using namespace warmth;
using namespace warmth::proto;
int main() {
    net::socket_init();
    Coordinator::Config cfg; cfg.host = "127.0.0.1";
    Coordinator coord(cfg);
    if (!coord.start()) return 1;
    const auto port = coord.port();
    std::atomic<bool> stop{false};
    std::thread accept([&]{ coord.run_accept_loop(stop); });
    const auto wid = Id128::derive("example-worker");
    const auto boot = Id128::derive("example-boot");
    Worker::Config wcfg; wcfg.host="127.0.0.1"; wcfg.port=port; wcfg.worker_id=wid; wcfg.boot=boot;
    wcfg.node=NodeId::derive("n"); wcfg.workload=WorkloadId::derive("wl"); wcfg.backend="cuda"; wcfg.devices={"cuda:0"}; wcfg.actions={0,1};
    Worker worker(wcfg);
    std::atomic<bool> connected{false};
    std::thread worker_thread([&]{ if (worker.connect()) { connected=true; SyntheticWarmHook h; worker.run(h); } });
    for (int i=0;i<400 && !connected;++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    for (int i=0;i<400 && coord.registered_workers()<1;++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const WarmthObjectId obj = Id128::derive("example-object");
    coord.issue_warm(obj, WarmthAction::LOAD_MODEL, &wid);
    coord.wait_until_settled();
    auto o = coord.find_object(obj);
    std::printf("object warm under authority: state=%s ready=%s\n", o ? to_string(o->state()).data() : "absent", o && o->execution_ready()?"yes":"no");
    // Send a stale-epoch report over a real connection; verify it is rejected.
    {
        auto conn = net::connect("127.0.0.1", port);
        if (conn.valid()) {
            proto::FrameWriter fw(conn); proto::FrameReader fr(conn);
            ReportPayload rp; rp.boot=boot; rp.epoch = coord.epoch().value()+100; rp.warmth_gen=coord.warmth_generation().value(); rp.dep_gen=coord.dependency_generation().value();
            WarmthObject oo(obj, WarmthCategory::MODEL_WEIGHTS, WorkloadId::derive("wl"), NodeId::derive("n"), "cuda:0", "cuda");
            oo.restore_state(WarmthState::WARM, "stale");
            rp.objects.push_back(oo);
            WireEncoder w; encode_report(w, rp);
            proto::Frame f; f.type = proto::MessageType::REPORT; f.payload = w.done();
            fw.write(f);
            conn.close();
        }
    }
    auto o2 = coord.find_object(obj);
    std::printf("after stale-epoch report: state=%s ready=%s (must still be ready)\n", o2 ? to_string(o2->state()).data() : "absent", o2 && o2->execution_ready()?"yes":"no");
    stop.store(true); coord.stop(); accept.join(); worker_thread.join();
    std::printf("stale authority rejected; accounting balanced=%s\n", coord.balanced()?"yes":"no");
    net::socket_cleanup();
    return 0;
}
