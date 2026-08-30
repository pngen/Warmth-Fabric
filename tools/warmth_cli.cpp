#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>

#include "warmth/fabric.hpp"
#include "warmth/cost_model.hpp"
#include "warmth/scheduler.hpp"
#include "warmth/explanation.hpp"
#include "warmth/persistence.hpp"
#include "warmth/coordinator.hpp"
#include "warmth/worker.hpp"
#include "warmth/messages.hpp"
#include "warmth/transport.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace warmth;
using namespace warmth::proto;

static std::string getarg(const std::vector<std::string>& a, const std::string& key, const std::string& def = {}) {
    for (std::size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == key) return a[i + 1];
    return def;
}
static bool hasarg(const std::vector<std::string>& a, const std::string& key) {
    for (const auto& s : a) if (s == key) return true;
    return false;
}

static WarmthFabric build_sample_fabric() {
    WarmthFabric::Config cfg;
    cfg.workload = WorkloadId::derive("demo-workload");
    cfg.node = NodeId::derive("node-0");
    cfg.policy = WarmthPolicy::defaults(PolicyGeneration(1));
    WarmthFabric f(cfg);
    const auto wm = ModelId::derive("demo-model");
    const auto art = ArtifactId::derive("demo-artifact");
    const auto repl = ReplicaId::derive("replica-0");
    const auto eng = EngineId::derive("engine-0");
    const auto idw = f.register_object(WarmthCategory::MODEL_WEIGHTS, "model.weights", wm, art, repl, eng);
    const auto ida = f.register_object(WarmthCategory::ADAPTERS, "model.adapters", wm, art, repl, eng);
    const auto idk = f.register_object(WarmthCategory::KERNEL_ARTIFACTS, "kernels", wm, art, repl, eng);
    const auto idg = f.register_object(WarmthCategory::EXECUTION_GRAPHS, "graphs", wm, art, repl, eng);
    const auto ide = f.register_object(WarmthCategory::ENGINE_RUNTIME_PROCESS, "engine", wm, art, repl, eng);
    WarmthDimensions d;
    d.model_residency = DimensionStatus::PARTIAL;
    d.artifact_availability = DimensionStatus::VALID;
    d.artifact_validation = DimensionStatus::VALID;
    d.local_dependency_readiness = DimensionStatus::VALID;
    d.tokenizer_readiness = DimensionStatus::VALID;
    f.set_dimensions(idw, d);
    f.set_dimension(ida, DimensionIndex::AdapterResidency, DimensionStatus::COLD);
    f.set_dimension(idk, DimensionIndex::KernelReadiness, DimensionStatus::COLD);
    f.set_dimension(idg, DimensionIndex::GraphReadiness, DimensionStatus::COLD);
    f.set_dimension(ide, DimensionIndex::EngineReadiness, DimensionStatus::COLD);
    return f;
}

static void cmd_help() {
    std::printf(
        "warmth_cli - Warmth Fabric command line\n"
        "Usage: warmth_cli <command> [options]\n"
        "Commands:\n"
        "  list                         list warmth objects\n"
        "  inspect <id>                 inspect a warmth object\n"
        "  readiness                    show workload readiness\n"
        "  warm <id>                    warm an object\n"
        "  invalidate <id>              invalidate an object\n"
        "  demote <id>                  demote an object\n"
        "  evict <id>                   evict an object\n"
        "  explain <id>                 explain readiness for an object\n"
        "  plan                         explain the warming plan\n"
        "  budgets                      show budgets and usage\n"
        "  snapshot <path>              print/encode a snapshot\n"
        "  save <path>                  persist a snapshot to a file\n"
        "  recover <path>               recover a snapshot from a file\n"
        "  synthetic                    run the synthetic cold->warm demo\n"
        "  multiprocess                 run the atomic multiprocess restart proof\n"
        "  cuda                         run the CUDA cold/warm proof\n"
        "  benchmark                    run the microbenchmarks\n"
        "  --coordinator [--port N]     run as a coordinator process\n"
        "  --worker (options)           run as a worker process\n"
        "Options: --json                emit JSON where applicable\n");
}

static void print_objects(const WarmthFabric& f, bool json) {
    const auto objs = f.objects();
    if (json) {
        json::Value o = json::Value::array();
        for (const auto& oo : objs) {
            ReadinessExplanation e;
            e.object = oo.id(); e.state = oo.state(); e.category = oo.category();
            e.composite = oo.composite_level(); e.execution_ready = oo.execution_ready();
            e.cost = cost_model::estimate(oo, 1ULL << 30);
            for (int i = 0; i < 12; ++i) e.dimensions.push_back({static_cast<DimensionIndex>(i), oo.dimension(static_cast<DimensionIndex>(i))});
            o.push(readiness_to_json(e));
        }
        std::printf("%s\n", o.dump(2).c_str());
    } else {
        for (const auto& oo : objs) {
            std::printf("%s  %-18s %-8s composite=%-10s ready=%s  ttf=%gms\n",
                        oo.id().to_string().c_str(), category_name(oo.category()),
                        to_string(oo.state()).data(), composite_name(oo.composite_level()),
                        oo.execution_ready() ? "yes" : "no",
                        cost_model::estimate(oo, 1ULL << 30).expected_ttfu_ms);
        }
    }
}

static int run_list(bool json) { auto f = build_sample_fabric(); print_objects(f, json); return 0; }

static int run_inspect(const std::vector<std::string>& a, bool json) {
    const std::string idstr = getarg(a, "inspect");
    auto id = Id128::from_string(idstr);
    if (!id) { std::fprintf(stderr, "invalid id\n"); return 1; }
    auto f = build_sample_fabric();
    const auto e = f.explain(*id);
    if (json) std::printf("%s\n", readiness_to_json(e).dump(2).c_str());
    else std::printf("%s", readiness_to_text(e).c_str());
    return 0;
}

static int run_readiness(const std::vector<std::string>& a, bool json) {
    (void)a;
    auto f = build_sample_fabric();
    const auto objs = f.objects();
    std::size_t ready = 0, total = objs.size();
    for (const auto& o : objs) if (o.execution_ready()) ++ready;
    if (json) {
        json::Value o = json::Value::object();
        o.set("workload", "demo-workload");
        o.set("total_objects", static_cast<std::int64_t>(total));
        o.set("execution_ready", static_cast<std::int64_t>(ready));
        std::printf("%s\n", o.dump(2).c_str());
    } else {
        std::printf("workload=demo-workload objects=%zu ready=%zu\n", total, ready);
    }
    return 0;
}

static int run_warm(const std::vector<std::string>& a) {
    const std::string idstr = getarg(a, "warm");
    auto id = Id128::from_string(idstr);
    if (!id) { std::fprintf(stderr, "invalid id\n"); return 1; }
    auto f = build_sample_fabric();
    double ms = 0; std::uint64_t bytes = 0;
    if (!f.warm(*id, WarmthAction::TRANSFER_HOST_DEVICE, &ms, &bytes)) { std::fprintf(stderr, "warm failed\n"); return 1; }
    const auto o = f.get(*id);
    if (o) std::printf("%s -> %s (measured %g ms, %llu bytes)\n", id->to_string().c_str(), to_string(o->state()).data(), ms, (unsigned long long)bytes);
    return 0;
}

static int run_invalidate(const std::vector<std::string>& a) {
    const std::string idstr = getarg(a, "invalidate");
    auto id = Id128::from_string(idstr);
    if (!id) return 1;
    auto f = build_sample_fabric();
    if (!f.invalidate(*id, InvalidationReason::MODEL_REVISION_CHANGE)) { std::fprintf(stderr, "invalidate failed\n"); return 1; }
    const auto o = f.get(*id);
    if (o) std::printf("%s -> %s (%s)\n", id->to_string().c_str(), to_string(o->state()).data(), o->invalidation_reason() ? o->invalidation_reason()->c_str() : "");
    return 0;
}

static int run_demote(const std::vector<std::string>& a) {
    const std::string idstr = getarg(a, "demote");
    auto id = Id128::from_string(idstr);
    if (!id) return 1;
    auto f = build_sample_fabric();
    if (!f.warm_to_ready(*id)) { std::fprintf(stderr, "warm failed\n"); return 1; }
    if (!f.demote(*id)) { std::fprintf(stderr, "demote failed\n"); return 1; }
    const auto o = f.get(*id);
    if (o) std::printf("%s -> %s\n", id->to_string().c_str(), to_string(o->state()).data());
    return 0;
}

static int run_evict(const std::vector<std::string>& a) {
    const std::string idstr = getarg(a, "evict");
    auto id = Id128::from_string(idstr);
    if (!id) return 1;
    auto f = build_sample_fabric();
    if (!f.warm_to_ready(*id)) { std::fprintf(stderr, "warm failed\n"); return 1; }
    if (!f.evict(*id)) { std::fprintf(stderr, "evict failed\n"); return 1; }
    const auto o = f.get(*id);
    if (o) std::printf("%s -> %s (residency=%s)\n", id->to_string().c_str(), to_string(o->state()).data(), residency_name(o->residency()));
    return 0;
}

static int run_explain(const std::vector<std::string>& a, bool json) {
    const std::string idstr = getarg(a, "explain");
    auto id = Id128::from_string(idstr);
    if (!id) return 1;
    auto f = build_sample_fabric();
    const auto e = f.explain(*id);
    if (json) std::printf("%s\n", readiness_to_json(e).dump(2).c_str());
    else std::printf("%s", readiness_to_text(e).c_str());
    return 0;
}

static int run_plan(bool json) {
    auto f = build_sample_fabric();
    const auto pe = f.explain_plan();
    if (json) std::printf("%s\n", plan_to_json(pe).dump(2).c_str());
    else std::printf("%s", plan_to_text(pe).c_str());
    return 0;
}

static int run_budgets(bool json) {
    auto f = build_sample_fabric();
    const std::size_t outstanding = f.outstanding_reservations();
    if (json) {
        json::Value o = json::Value::object();
        o.set("device_memory", static_cast<std::int64_t>(f.budget_usage(WarmthBudgetKind::DEVICE_MEMORY)));
        o.set("concurrent_warming_ops", static_cast<std::int64_t>(f.budget_usage(WarmthBudgetKind::CONCURRENT_WARMING_OPS)));
        o.set("outstanding_reservations", static_cast<std::int64_t>(outstanding));
        o.set("balanced", f.budgets_balanced());
        std::printf("%s\n", o.dump(2).c_str());
    } else {
        std::printf("device_memory=%llu concurrent_ops=%llu outstanding=%zu balanced=%s\n",
                    (unsigned long long)f.budget_usage(WarmthBudgetKind::DEVICE_MEMORY),
                    (unsigned long long)f.budget_usage(WarmthBudgetKind::CONCURRENT_WARMING_OPS),
                    outstanding, f.budgets_balanced() ? "yes" : "no");
    }
    return 0;
}

static int run_snapshot(const std::vector<std::string>& a, bool json) {
    (void)json;
    auto f = build_sample_fabric();
    const auto snap = f.snapshot();
    const auto bytes = encode_snapshot(snap);
    (void)getarg(a, "snapshot", "snapshot.wfbin");
    std::printf("snapshot: %zu bytes, %zu objects\n", bytes.size(), snap.objects.size());
    return 0;
}

static int run_save(const std::vector<std::string>& a) {
    const std::string path = getarg(a, "save", "snapshot.wfbin");
    auto f = build_sample_fabric();
    std::string err;
    if (!f.save(path, &err)) { std::fprintf(stderr, "save failed: %s\n", err.c_str()); return 1; }
    std::printf("saved %s\n", path.c_str());
    return 0;
}

static int run_recover(const std::vector<std::string>& a) {
    const std::string path = getarg(a, "recover", "snapshot.wfbin");
    WarmthFabric::Config cfg;
    cfg.workload = WorkloadId::derive("demo-workload");
    cfg.node = NodeId::derive("node-0");
    WarmthFabric f(cfg);
    std::string err;
    if (!f.recover(path, &err)) { std::fprintf(stderr, "recover failed: %s\n", err.c_str()); return 1; }
    std::printf("recovered %zu objects (live dims must be revalidated)\n", f.object_count());
    return 0;
}

static int run_synthetic() {
    WarmthFabric::Config cfg;
    cfg.workload = WorkloadId::derive("demo-workload");
    cfg.node = NodeId::derive("node-0");
    WarmthFabric f(cfg);
    const auto wm = ModelId::derive("demo-model");
    const auto id = f.register_object(WarmthCategory::MODEL_WEIGHTS, "model.weights", wm);
    std::printf("cold: state=%s composite=%s\n", to_string(f.get(id)->state()).data(), composite_name(f.get(id)->composite_level()));
    double ms = 0; std::uint64_t bytes = 0;
    f.warm(id, WarmthAction::TRANSFER_HOST_DEVICE, &ms, &bytes);
    std::printf("warm: measured %gms, %llu bytes, state=%s execution_ready=%s\n", ms, (unsigned long long)bytes, to_string(f.get(id)->state()).data(), f.get(id)->execution_ready() ? "yes" : "no");
    f.invalidate(id, InvalidationReason::MODEL_REVISION_CHANGE);
    std::printf("invalidated: state=%s reason=%s\n", to_string(f.get(id)->state()).data(), f.get(id)->invalidation_reason() ? f.get(id)->invalidation_reason()->c_str() : "");
    f.warm(id, WarmthAction::INIT_ENGINE, &ms, &bytes);
    std::printf("rewarmed: state=%s ready=%s\n", to_string(f.get(id)->state()).data(), f.get(id)->execution_ready() ? "yes" : "no");
    std::printf("budgets_balanced=%s\n", f.budgets_balanced() ? "yes" : "no");
    return 0;
}

#ifdef _WIN32
struct ChildProc { HANDLE h = nullptr; unsigned long pid = 0; };
static std::string self_exe_path() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    const int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, s.data(), n, nullptr, nullptr);
    return s;
}
static bool spawn_child(const std::string& exe, const std::vector<std::string>& args, ChildProc& out) {
    std::string cmdline = "\"" + exe + "\"";
    for (const auto& a : args) { cmdline += " \"" + a + "\""; }
    const int n = MultiByteToWideChar(CP_UTF8, 0, cmdline.c_str(), -1, nullptr, 0);
    std::wstring wcmd(static_cast<std::size_t>(n) - 1, L' ');
    MultiByteToWideChar(CP_UTF8, 0, cmdline.c_str(), -1, wcmd.data(), n);
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    out.h = pi.hProcess;
    out.pid = static_cast<unsigned long>(pi.dwProcessId);
    CloseHandle(pi.hThread);
    return true;
}
static void terminate_child(ChildProc& c) {
    if (c.h) { TerminateProcess(c.h, 3); WaitForSingleObject(c.h, INFINITE); CloseHandle(c.h); }
    if (c.pid) { char cmd[64]; std::snprintf(cmd, sizeof(cmd), "taskkill /F /PID %lu >nul 2>&1", c.pid); std::system(cmd); }
    c.h = nullptr; c.pid = 0;
}
static bool child_running(ChildProc& c) { if (!c.h) return false; return WaitForSingleObject(c.h, 0) == WAIT_TIMEOUT; }
#endif

static int run_worker_mode(const std::vector<std::string>& a) {
    Worker::Config cfg;
    cfg.host = getarg(a, "--coordinator-host", "127.0.0.1");
    cfg.port = static_cast<std::uint16_t>(std::atoi(getarg(a, "--coordinator-port", "0").c_str()));
    cfg.worker_id = Id128::from_string(getarg(a, "--worker-id", "")).value_or(Id128::derive("worker"));
    cfg.boot = Id128::from_string(getarg(a, "--boot", "")).value_or(Id128::derive("boot"));
    cfg.node = NodeId::derive(getarg(a, "--node", "node"));
    cfg.workload = WorkloadId::derive(getarg(a, "--workload", "workload"));
    cfg.backend = "cuda";
    cfg.devices = {"cuda:0"};
    cfg.actions = {0,1,2,3,4,5,6,7,8,9,10,11,12,13};
    Worker w(cfg);
    if (!w.connect()) { std::fprintf(stderr, "worker connect failed\n"); return 1; }
    SyntheticWarmHook hook;
    w.run(hook);
    return 0;
}

static int run_coordinator_mode(const std::vector<std::string>& a) {
    Coordinator::Config cfg;
    cfg.host = "127.0.0.1";
    cfg.port = static_cast<std::uint16_t>(std::atoi(getarg(a, "--port", "0").c_str()));
    Coordinator coord(cfg);
    if (!coord.start()) { std::fprintf(stderr, "coordinator bind failed\n"); return 1; }
    std::printf("coordinator listening on %u\n", coord.port());
    std::fflush(stdout);
    std::atomic<bool> stop{false};
    coord.run_accept_loop(stop);
    return 0;
}

#ifdef _WIN32
static int run_multiprocess() {
    const std::string exe = self_exe_path();
    Coordinator::Config cfg; cfg.host = "127.0.0.1";
    Coordinator coord(cfg);
    if (!coord.start()) { std::fprintf(stderr, "coordinator start failed\n"); return 1; }
    std::atomic<bool> accept_stop{false};
    std::thread accept_thread([&] { coord.run_accept_loop(accept_stop); });
    const std::uint16_t port = coord.port();
    const auto workerA = Id128::derive("worker-A");
    const auto workerB = Id128::derive("worker-B");
    const auto bootA1 = Id128::derive("boot-A-1");
    const auto bootB = Id128::derive("boot-B");
    const auto bootA2 = Id128::derive("boot-A-2");
    ChildProc pa, pb;
    std::printf("A=%s B=%s bA1=%s bB=%s\n", workerA.to_string().c_str(), workerB.to_string().c_str(), bootA1.to_string().c_str(), bootB.to_string().c_str());
    std::fflush(stdout);
    const std::string sport = std::to_string(port);
    auto spawnW = [&](const Id128& wid, const Id128& boot, ChildProc& out) {
        return spawn_child(exe, {"--worker", "--coordinator-port", sport, "--worker-id", wid.to_string(), "--boot", boot.to_string(), "--node", "node-0", "--workload", "workload-0"}, out);
    };
    if (!spawnW(workerA, bootA1, pa)) { std::fprintf(stderr, "spawn worker A failed\n"); return 1; }
    if (!spawnW(workerB, bootB, pb)) { std::fprintf(stderr, "spawn worker B failed\n"); return 1; }
    for (int i = 0; i < 400 && coord.registered_workers() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::printf("registered=%zu pa_running=%d pb_running=%d\n", coord.registered_workers(), child_running(pa)?1:0, child_running(pb)?1:0);
    std::fflush(stdout);
    if (coord.registered_workers() < 2) { std::fprintf(stderr, "workers did not register\n"); terminate_child(pa); terminate_child(pb); return 1; }
    const WarmthObjectId obj = Id128::derive("object-X");
    if (!coord.issue_warm(obj, WarmthAction::LOAD_MODEL, &workerA)) { std::fprintf(stderr, "issue_warm A failed\n"); terminate_child(pa); terminate_child(pb); return 1; }
    coord.wait_until_settled();
    auto o = coord.find_object(obj);
    if (!o || !o->execution_ready()) { std::fprintf(stderr, "warm on A not ready\n"); terminate_child(pa); terminate_child(pb); return 1; }
    const auto epoch0 = coord.epoch().value();
    const auto wgen0 = coord.warmth_generation().value();
    const auto dgen0 = coord.dependency_generation().value();
    std::printf("warmed object on A under epoch %llu, warm-gen %llu, dep-gen %llu\n", (unsigned long long)epoch0, (unsigned long long)wgen0, (unsigned long long)dgen0);
    coord.roll_epoch();
    terminate_child(pa);
    for (int i = 0; i < 200 && coord.registered_workers() >= 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    {
        auto ob = coord.find_object(obj);
        std::printf("after kill+roll: object state=%s ready=%s\n", ob ? to_string(ob->state()).data() : "absent", ob && ob->execution_ready() ? "yes" : "no");
        if (ob && ob->execution_ready()) { std::fprintf(stderr, "recovered warmth resurrected\n"); terminate_child(pb); return 1; }
    }
    if (!spawnW(workerA, bootA2, pa)) { std::fprintf(stderr, "spawn worker A v2 failed\n"); terminate_child(pb); return 1; }
    for (int i = 0; i < 400 && coord.registered_workers() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (coord.registered_workers() < 2) { std::fprintf(stderr, "worker A v2 not registered\n"); terminate_child(pa); terminate_child(pb); return 1; }
    {
        bool stale_rejected = true;
        auto conn = net::connect("127.0.0.1", port);
        if (conn.valid()) {
            proto::FrameWriter fw(conn); proto::FrameReader fr(conn);
            Hello h; h.worker = workerA; h.boot = bootA1; h.node = NodeId::derive("node-0"); h.epoch_guess = epoch0; h.backend = "cuda";
            WireEncoder w; encode_hello(w, h);
            proto::Frame f; f.type = proto::MessageType::HELLO; f.payload = w.done();
            const bool wrote = fw.write(f);
            proto::Frame reply;
            if (wrote && fr.read(reply) && reply.type == proto::MessageType::HELLO_ACK) {
                WireDecoder d(reply.payload); HelloAck ack;
                if (!decode_hello_ack(d, ack)) stale_rejected = true;
                else stale_rejected = !ack.accepted;
            } else stale_rejected = true;
        }
        auto conn2 = net::connect("127.0.0.1", port);
        if (conn2.valid()) {
            proto::FrameReader fr2(conn2); proto::FrameWriter fw2(conn2);
            ReportPayload rp; rp.boot = bootA1; rp.epoch = epoch0; rp.warmth_gen = wgen0; rp.dep_gen = dgen0;
            WarmthObject oo(obj, WarmthCategory::MODEL_WEIGHTS, WorkloadId::derive("wl"), NodeId::derive("n"), "cuda:0", "cuda");
            oo.transition(WarmthState::WARM, "stale");
            rp.objects.push_back(oo);
            WireEncoder w2; encode_report(w2, rp);
            proto::Frame f2; f2.type = proto::MessageType::REPORT; f2.payload = w2.done();
            fw2.write(f2);
            proto::Frame resp; (void)fr2.read(resp);
        }
        auto ob = coord.find_object(obj);
        if (ob && ob->execution_ready()) stale_rejected = false;
        if (!stale_rejected) { std::fprintf(stderr, "stale authority not rejected\n"); terminate_child(pa); terminate_child(pb); return 1; }
        std::printf("stale boot/epoch/generation authority rejected\n");
    }
    if (!coord.issue_warm(obj, WarmthAction::LOAD_MODEL, &workerA)) { std::fprintf(stderr, "fresh issue_warm failed\n"); terminate_child(pa); terminate_child(pb); return 1; }
    coord.wait_until_settled();
    o = coord.find_object(obj);
    if (!o || !o->execution_ready()) { std::fprintf(stderr, "fresh warm not ready\n"); terminate_child(pa); terminate_child(pb); return 1; }
    std::printf("fresh warming under current authority accepted: state=%s\n", to_string(o->state()).data());
    if (!coord.balanced()) { std::fprintf(stderr, "budget not balanced\n"); terminate_child(pa); terminate_child(pb); return 1; }
    std::printf("budget accounting returns to zero (outstanding=%zu)\n", coord.outstanding_budget());
    terminate_child(pa); terminate_child(pb);
    accept_stop.store(true);
    coord.stop();
    accept_thread.join();
    std::printf("atomic multiprocess restart proof PASSED\n");
    return 0;
}
#endif

static int run_benchmark() {
    WarmthFabric::Config cfg;
    cfg.workload = WorkloadId::derive("bench"); cfg.node = NodeId::derive("n");
    WarmthFabric f(cfg);
    const int N = 2000;
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<WarmthObjectId> ids;
    for (int i = 0; i < N; ++i) ids.push_back(f.register_object(WarmthCategory::APPLICATION_DEFINED, "bench-object-" + std::to_string(i)));
    const auto t1 = std::chrono::steady_clock::now();
    const double tcreate = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const auto t2 = std::chrono::steady_clock::now();
    std::size_t ready = 0;
    for (const auto& id : ids) if (f.get(id) && f.get(id)->execution_ready()) ++ready;
    const auto t3 = std::chrono::steady_clock::now();
    const double tquery = std::chrono::duration<double, std::milli>(t3 - t2).count();
    const auto t4 = std::chrono::steady_clock::now();
    for (int r = 0; r < 3; ++r) for (const auto& id : ids) (void)f.get(id);
    const auto t5 = std::chrono::steady_clock::now();
    const double tread = std::chrono::duration<double, std::milli>(t5 - t4).count();
    std::printf("objects=%d create_ms=%g query_ms=%g read3x_ms=%g ready=%zu\n", N, tcreate, tquery, tread, ready);
    return 0;
}

#ifdef _WIN32
static int run_cuda_demo() {
    const std::string exe = self_exe_path();
    const std::string dir = exe.substr(0, exe.find_last_of("\\"));
    const std::string cuda_exe = dir + "\\warmth_cuda_demo.exe";
    ChildProc c;
    if (!spawn_child(cuda_exe, {}, c)) { std::fprintf(stderr, "CUDA demo not available (%s)\n", cuda_exe.c_str()); return 1; }
    WaitForSingleObject(c.h, INFINITE);
    DWORD code = 0; GetExitCodeProcess(c.h, &code);
    CloseHandle(c.h);
    return static_cast<int>(code);
}
#endif

int main(int argc, char** argv) {
    net::socket_init();
    std::vector<std::string> a(argv + 1, argv + argc);
    const bool json = hasarg(a, "--json");
    if (a.empty()) { cmd_help(); net::socket_cleanup(); return 0; }
    const std::string cmd = a[0];
    int rc = 0;
    if (cmd == "list") rc = run_list(json);
    else if (cmd == "inspect") rc = run_inspect(a, json);
    else if (cmd == "readiness") rc = run_readiness(a, json);
    else if (cmd == "warm") rc = run_warm(a);
    else if (cmd == "invalidate") rc = run_invalidate(a);
    else if (cmd == "demote") rc = run_demote(a);
    else if (cmd == "evict") rc = run_evict(a);
    else if (cmd == "explain") rc = run_explain(a, json);
    else if (cmd == "plan") rc = run_plan(json);
    else if (cmd == "budgets") rc = run_budgets(json);
    else if (cmd == "snapshot") rc = run_snapshot(a, json);
    else if (cmd == "save") rc = run_save(a);
    else if (cmd == "recover") rc = run_recover(a);
    else if (cmd == "synthetic") rc = run_synthetic();
    else if (cmd == "benchmark") rc = run_benchmark();
#ifdef _WIN32
    else if (cmd == "multiprocess") rc = run_multiprocess();
    else if (cmd == "cuda") rc = run_cuda_demo();
#else
    else if (cmd == "multiprocess") { std::fprintf(stderr, "multiprocess requires Windows\n"); rc = 1; }
    else if (cmd == "cuda") { std::fprintf(stderr, "cuda requires Windows\n"); rc = 1; }
#endif
    else if (cmd == "--worker") rc = run_worker_mode(a);
    else if (cmd == "--coordinator") rc = run_coordinator_mode(a);
    else if (cmd == "help" || cmd == "--help" || cmd == "-h") cmd_help();
    else { cmd_help(); rc = 1; }
    net::socket_cleanup();
    return rc;
}
