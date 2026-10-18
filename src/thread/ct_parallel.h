#pragma once

// CtParallel: self-scheduled parallel algorithms (MT3).
//
// Part of the CRISP threading subsystem (crisp::thread). Implements
// docs/threading/03-rewrite-proposal.md §3.6: chunked parallel map / filter /
// reduce that dispatch each chunk to the shared CtThreadPool, with NO
// dependency on std::execution (PSTL — MinGW 8.1's libstdc++ lacks it; Qt
// also self-schedules via qtconcurrentiteratekernel's BlockSizeManager).
//
// Determinism: each chunk runs serially IN ORDER inside a single worker
// task; results are assembled IN CHUNK INDEX ORDER after all tasks settle
// (audit MUST fix: rejected chunks are parked in a per-chunk slot and
// concatenated at the end, so a mixed pool rejection cannot reorder the
// output).
//
// NOTE: these are BLOCKING calls (they wait for all chunks). Do not call
// them from inside a pool worker task body (it would deadlock waiting for
// itself).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "ct_thread_pool.h" // CtThreadPool, CtTaskHandle<T>, CtResult<T>

namespace crisp::thread {

/// Self-scheduled parallel algorithms over the shared thread pool.
class CtParallel {
public:
    /// Default chunk size (elements per worker task). Small enough to
    /// balance, large enough to amortize task overhead.
    static constexpr std::size_t kDefaultChunk = 256;

    /// Parallel map: for each element e in [first,last), result[i] = fn(e).
    /// Chunks are processed in parallel; the OUTPUT is assembled in input
    /// order (deterministic). If fn() throws, the FIRST exception (in chunk
    /// index order) is rethrown after all chunks settle. Mixed pool
    /// rejection + exception corner: a REJECTED (parked) chunk's exception
    /// propagates immediately from the submit loop; all accepted chunks
    /// still settle (handle destructors join) before the exception escapes.
    /// CONSTRAINT: fn must be COPY-constructible (each chunk gets its own
    /// copy; stateful functors are copied per chunk — pure per-element
    /// functions are recommended).
    template <typename Pool, typename It, typename Fn>
    static auto map(Pool& pool, It first, It last, Fn&& fn)
        -> std::vector<std::decay_t<decltype(fn(*first))>>;

    /// Parallel filter: collect elements of [first,last) for which pred(e)
    /// is true, in input order. Parallel predicate evaluation; ordered
    /// output. CONSTRAINT: pred must be COPY-constructible (see map).
    template <typename Pool, typename It, typename Pred>
    static std::vector<typename std::iterator_traits<It>::value_type>
    filter(Pool& pool, It first, It last, Pred&& pred);

    /// Parallel reduce: combine [first,last) with `init` using a binary
    /// `op`. Each chunk reduces serially IN ORDER; chunk partials are then
    /// combined IN CHUNK INDEX ORDER. DETERMINISTIC always; identical to a
    /// sequential left-fold when op is associative (non-associative ops get
    /// a deterministic chunked order, not the exact sequential fold — see
    /// the subtraction test).
    /// CONSTRAINT: op must be COPY-constructible (see map).
    template <typename Pool, typename It, typename T, typename Op>
    static T reduce(Pool& pool, It first, It last, T init, Op&& op);

private:
    /// Number of chunks for a workload of `n` elements (min 1).
    static std::size_t chunkCount(std::size_t n) noexcept {
        return (n + kDefaultChunk - 1) / kDefaultChunk;
    }

    /// Wait for all accepted chunk tasks; return the first exception (in
    /// chunk order), or null if none. Rethrows nothing (caller decides).
    template <typename Handle>
    static std::exception_ptr waitAllAndFirstException(
        std::vector<Handle>& handles) {
        std::exception_ptr firstEx;
        for (auto& h : handles) {
            if (!h.valid())
                continue;
            h.waitForDone(UINT32_MAX);
            if (auto* ex = std::get_if<std::exception_ptr>(&h.result())) {
                if (!firstEx)
                    firstEx = *ex;
            }
        }
        return firstEx;
    }

    /// Append a chunk's result (accepted task or parked inline) to `out` in
    /// chunk order. `Handles` owns CtTaskHandle<std::vector<R>>; `Parked`
    /// owns std::vector<R>.
    template <typename R, typename Handles, typename Parked>
    static void assembleInChunkOrder(Handles& handles, Parked& parked,
                                     std::size_t chunks, std::vector<R>& out) {
        for (std::size_t c = 0; c < chunks; ++c) {
            if (handles[c].valid()) {
                const auto& res =
                    std::get<std::vector<R>>(handles[c].result());
                out.insert(out.end(), res.begin(), res.end());
            } else {
                out.insert(out.end(), parked[c].begin(), parked[c].end());
            }
        }
    }

    /// Serial left-fold of in[begin,end) starting from in[begin] (no init;
    /// partials combine later). Shared by the accepted-task lambda and the
    /// rejected-chunk park path (audit SUGGEST-2: no duplicate kernels).
    template <typename Elem, typename T, typename Op>
    static T reduceChunk(const std::vector<Elem>& in, const Op& op,
                         std::size_t begin, std::size_t end) {
        T acc = in[begin];
        for (std::size_t i = begin + 1; i < end; ++i)
            acc = op(std::move(acc), in[i]);
        return acc;
    }
};

// ---- implementation ----

template <typename Pool, typename It, typename Fn>
auto CtParallel::map(Pool& pool, It first, It last, Fn&& fn)
    -> std::vector<std::decay_t<decltype(fn(*first))>> {
    using Out = std::decay_t<decltype(fn(*first))>;

    // Materialize the input into a contiguous indexable vector (forward
    // iterators are copied once here; random-access iterators pay one copy
    // too — simpler and safe for MT3 scope).
    std::vector<std::decay_t<decltype(*first)>> in(first, last);
    const std::size_t n = in.size();
    std::vector<Out> out;
    out.reserve(n);

    if (n == 0)
        return out;

    // Single chunk: run serially (no pool task overhead for small inputs).
    if (n <= kDefaultChunk) {
        for (std::size_t i = 0; i < n; ++i)
            out.push_back(fn(in[i]));
        return out;
    }

    // Stable functor base: copied (never moved) so every chunk gets an
    // identical stateful functor (audit WARN: moving the rvalue functor into
    // chunk 0 and copying moved-from state into later chunks would corrupt
    // stateful functors).
    auto fnBase = std::decay_t<Fn>(fn);

    const std::size_t chunks = chunkCount(n);
    // Per-chunk result slots, indexed by chunk: either a task handle or a
    // parked inline result (pool rejection). Assembly is by chunk index
    // order (audit MUST).
    std::vector<CtTaskHandle<std::vector<Out>>> handles(chunks);
    std::vector<std::vector<Out>> parked(chunks);

    for (std::size_t c = 0; c < chunks; ++c) {
        const std::size_t begin = c * kDefaultChunk;
        const std::size_t end = std::min(n, begin + kDefaultChunk);
        auto handle = pool.template submit<std::vector<Out>>(
            [&in, fnBase, begin, end]() mutable {
                std::vector<Out> local;
                local.reserve(end - begin);
                for (std::size_t i = begin; i < end; ++i)
                    local.push_back(fnBase(in[i]));
                return local;
            });
        if (!handle.valid()) {
            // Pool rejected: park the serial result for THIS chunk (do not
            // append now — order must follow chunk index, audit MUST).
            std::vector<Out>& slot = parked[c];
            for (std::size_t i = begin; i < end; ++i)
                slot.push_back(fnBase(in[i]));
            continue;
        }
        handles[c] = std::move(handle);
    }

    // Wait for all accepted chunks; propagate the first exception (chunk order).
    if (auto firstEx = waitAllAndFirstException(handles))
        std::rethrow_exception(firstEx);

    // Assemble IN CHUNK INDEX ORDER: accepted task results and parked
    // inline results interleave correctly (audit MUST).
    assembleInChunkOrder(handles, parked, chunks, out);
    return out;
}

template <typename Pool, typename It, typename Pred>
std::vector<typename std::iterator_traits<It>::value_type>
CtParallel::filter(Pool& pool, It first, It last, Pred&& pred) {
    using T = typename std::iterator_traits<It>::value_type;
    // Materialize (forward iterators are single-pass).
    std::vector<T> in(first, last);
    const std::size_t n = in.size();
    std::vector<T> out;
    out.reserve(n);
    if (n == 0)
        return out;

    if (n <= kDefaultChunk) {
        for (std::size_t i = 0; i < n; ++i)
            if (pred(in[i]))
                out.push_back(in[i]);
        return out;
    }

    auto predBase = std::decay_t<Pred>(pred); // stable copy (audit WARN)

    const std::size_t chunks = chunkCount(n);
    std::vector<CtTaskHandle<std::vector<T>>> handles(chunks);
    std::vector<std::vector<T>> parked(chunks);

    for (std::size_t c = 0; c < chunks; ++c) {
        const std::size_t begin = c * kDefaultChunk;
        const std::size_t end = std::min(n, begin + kDefaultChunk);
        auto handle = pool.template submit<std::vector<T>>(
            [&in, predBase, begin, end]() mutable {
                std::vector<T> local;
                local.reserve(end - begin);
                for (std::size_t i = begin; i < end; ++i)
                    if (predBase(in[i]))
                        local.push_back(in[i]);
                return local;
            });
        if (!handle.valid()) {
            std::vector<T>& slot = parked[c];
            for (std::size_t i = begin; i < end; ++i)
                if (predBase(in[i]))
                    slot.push_back(in[i]);
            continue;
        }
        handles[c] = std::move(handle);
    }

    if (auto firstEx = waitAllAndFirstException(handles))
        std::rethrow_exception(firstEx);

    // Assemble in chunk index order (audit MUST).
    assembleInChunkOrder(handles, parked, chunks, out);
    return out;
}

template <typename Pool, typename It, typename T, typename Op>
T CtParallel::reduce(Pool& pool, It first, It last, T init, Op&& op) {
    using Elem = typename std::iterator_traits<It>::value_type;
    std::vector<Elem> in(first, last);
    const std::size_t n = in.size();

    if (n == 0)
        return init;
    if (n <= kDefaultChunk) {
        for (std::size_t i = 0; i < n; ++i)
            init = op(std::move(init), in[i]);
        return init;
    }

    auto opBase = std::decay_t<Op>(op); // stable copy (audit WARN)

    const std::size_t chunks = chunkCount(n);
    // Per-chunk partial slots: task result or parked inline partial.
    // optional<T> so T need NOT be default-constructible (audit WARN-1).
    std::vector<CtTaskHandle<T>> handles(chunks);
    std::vector<std::optional<T>> parked(chunks);

    for (std::size_t c = 0; c < chunks; ++c) {
        const std::size_t begin = c * kDefaultChunk;
        const std::size_t end = std::min(n, begin + kDefaultChunk);
        auto handle = pool.template submit<T>(
            [&in, opBase, begin, end]() mutable {
                return reduceChunk<Elem, T>(in, opBase, begin, end);
            });
        if (!handle.valid()) {
            // Park this chunk's partial (do not merge yet — audit MUST).
            parked[c] = reduceChunk<Elem, T>(in, opBase, begin, end);
            continue;
        }
        handles[c] = std::move(handle);
    }

    if (auto firstEx = waitAllAndFirstException(handles))
        std::rethrow_exception(firstEx);

    // Combine partials IN CHUNK INDEX ORDER with init (deterministic).
    for (std::size_t c = 0; c < chunks; ++c) {
        if (handles[c].valid())
            init = op(std::move(init), std::get<T>(handles[c].result()));
        else
            init = op(std::move(init), std::move(*parked[c]));
    }
    return init;
}

} // namespace crisp::thread