// =============================================================================
// ContextTest.cxx - Phase 4 validation for explicit worker render contexts.
// =============================================================================

#include "Execution.hxx"
#include "Senders.hxx"
#include "Task.hxx"
#include "ThreadPool.hxx"
#include "ThreadPoolScheduler.hxx"
#include "WorkerContextAccess.hxx"
#include "WorkerRenderContext.hxx"

#include <atomic>
#include <cstdio>
#include <vector>

namespace {

std::atomic<int> g_failures{0};
std::atomic<int> g_checks{0};

void Report(bool ok, const char* expr, const char* file, int line) {
    g_checks.fetch_add(1, std::memory_order_relaxed);
    if (!ok) {
        g_failures.fetch_add(1, std::memory_order_relaxed);
        std::printf("  [FAIL] %s  (%s:%d)\n", expr, file, line);
    }
}

#define CHECK(cond) Report((cond), #cond, __FILE__, __LINE__)

void Section(const char* name) {
    std::printf("\n== %s ==\n", name);
    std::fflush(stdout);
}

struct RecordContext {
    std::vector<std::uint64_t>* command_pool_ids{};
    std::vector<std::size_t>* worker_indices{};
};

static void RecordContextJob(void* ctx) {
    auto* record = static_cast<RecordContext*>(ctx);
    Engine::WorkerRenderContext* render_context = Engine::WorkerRenderContext::Current();
    if (render_context == nullptr) {
        return;
    }
    (*record->command_pool_ids)[render_context->WorkerIndex()] = render_context->CommandPoolId();
    (*record->worker_indices)[render_context->WorkerIndex()] = render_context->WorkerIndex();
}

void TestWorkerContextInitialization(Engine::ThreadPool& pool, std::size_t worker_count) {
    Section("Test 4.1: each worker initializes a unique render context");
    std::vector<std::uint64_t> command_pool_ids(worker_count, 0);
    std::vector<std::size_t> worker_indices(worker_count, worker_count + 999);
    RecordContext record{&command_pool_ids, &worker_indices};

    for (std::size_t target = 0; target < worker_count; ++target) {
        pool.Submit(
            Engine::Job{RecordContextJob, &record},
            Engine::TaskOptions{.target_worker = target, .pinned = true});
    }
    pool.WaitIdle();

    for (std::size_t target = 0; target < worker_count; ++target) {
        CHECK(command_pool_ids[target] != 0);
        CHECK(worker_indices[target] == target);
    }

    for (std::size_t left = 0; left < worker_count; ++left) {
        for (std::size_t right = left + 1; right < worker_count; ++right) {
            CHECK(command_pool_ids[left] != command_pool_ids[right]);
        }
    }
}

void TestMainThreadHasNoContext() {
    Section("Test 4.2: main thread has no bound render context");
    CHECK(Engine::WorkerRenderContext::Current() == nullptr);
}

void TestSubmitWithContext(Engine::ThreadPool& pool, std::size_t worker_count) {
    Section("Test 4.3: SubmitWithContext passes thread-local context");
    std::vector<std::atomic<int>> draw_counts(worker_count);
    for (auto& counter : draw_counts) {
        counter.store(0, std::memory_order_relaxed);
    }

    for (std::size_t target = 0; target < worker_count; ++target) {
        Engine::SubmitWithContext(
            pool,
            [&draw_counts](Engine::WorkerRenderContext& ctx) {
                ctx.BeginFrame(1);
                ctx.RecordDrawCall();
                ctx.RecordDrawCall();
                draw_counts[ctx.WorkerIndex()].fetch_add(
                    ctx.RecordedDrawCalls(), std::memory_order_relaxed);
            },
            Engine::TaskOptions{.target_worker = target, .pinned = true});
    }
    pool.WaitIdle();

    for (std::size_t target = 0; target < worker_count; ++target) {
        CHECK(draw_counts[target].load() == 2);
    }
}

Engine::Task<int> RecordDrawsOnWorker(Engine::ThreadPoolScheduler scheduler, std::size_t expected_worker) {
    auto& ctx = co_await Engine::with_worker_context(
        scheduler,
        Engine::TaskOptions{.target_worker = expected_worker, .pinned = true});
    CHECK(ctx.WorkerIndex() == expected_worker);
    ctx.BeginFrame(7);
    ctx.RecordDrawCall();
    ctx.RecordDrawCall();
    ctx.RecordDrawCall();
    co_return ctx.RecordedDrawCalls();
}

void TestCoroutineContextAccess(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 4.4: coroutine resumes with worker render context");
    auto task = RecordDrawsOnWorker(scheduler, 0);
    task.Start();
    pool.WaitIdle();
    CHECK(task.Result() == 3);
}

void TestSenderPipelineWithContext(Engine::ThreadPool& pool, Engine::ThreadPoolScheduler scheduler) {
    Section("Test 4.5: sender pipeline uses TLS render context on worker");
    std::atomic<int> total{0};
    Engine::SubmitWithContext(
        pool,
        [&total, scheduler](Engine::WorkerRenderContext&) {
            total.store(
                Engine::exec::sync_wait(Engine::exec::then(
                    Engine::exec::schedule(scheduler),
                    []() {
                        Engine::WorkerRenderContext* ctx = Engine::WorkerRenderContext::Current();
                        ctx->BeginFrame(42);
                        ctx->RecordDrawCall();
                        ctx->RecordDrawCall();
                        return ctx->RecordedDrawCalls();
                    })),
                std::memory_order_relaxed);
        });
    pool.WaitIdle();
    CHECK(total.load() == 2);
}

void TestExplicitContextMatchesTls(Engine::ThreadPool& pool) {
    Section("Test 4.6: explicit context argument matches TLS lookup");
    std::atomic<bool> matched{false};
    Engine::SubmitWithContext(
        pool,
        [&matched](Engine::WorkerRenderContext& explicit_ctx) {
            Engine::WorkerRenderContext* tls_ctx = Engine::WorkerRenderContext::Current();
            matched.store(
                tls_ctx != nullptr && tls_ctx->CommandPoolId() == explicit_ctx.CommandPoolId()
                    && tls_ctx->WorkerIndex() == explicit_ctx.WorkerIndex(),
                std::memory_order_release);
        },
        Engine::TaskOptions{.target_worker = 1, .pinned = true});
    pool.WaitIdle();
    CHECK(matched.load(std::memory_order_acquire));
}

} // namespace

int main() {
    std::printf("Worker render context (Phase 4) test suite\n");
    std::fflush(stdout);

    Engine::ThreadPool pool(4);
    const Engine::ThreadPoolScheduler scheduler = pool.GetScheduler();
    const std::size_t worker_count = pool.WorkerCount();

    TestMainThreadHasNoContext();
    TestWorkerContextInitialization(pool, worker_count);
    TestSubmitWithContext(pool, worker_count);
    TestCoroutineContextAccess(pool, scheduler);
    TestSenderPipelineWithContext(pool, scheduler);
    TestExplicitContextMatchesTls(pool);

    pool.Shutdown();

    const int checks = g_checks.load();
    const int failures = g_failures.load();
    std::printf("\n----------------------------------------\n");
    std::printf("Checks run : %d\n", checks);
    std::printf("Failures   : %d\n", failures);
    std::printf("Result     : %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
