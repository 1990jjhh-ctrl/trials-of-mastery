#pragma once

#include "tom/stats/core/stats_service.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace tom::stats
{
// The plugin's one StatsService and its thread rule (docs/stats/PLAN.md,
// assumption 15, set by G1). Every hook, sink, API call (A1) and Papyrus
// native (A2) follows it:
//
// - The service is reached only through withService(), from any thread. Hooks
//   that need a value at once inside the game's own computation (skill XP
//   factor, damage split, crit chance, checks) call it on the thread they run
//   on; the spikes saw them on worker threads and on the main loop's thread
//   (the one that receives kPostLoad), and kDataLoaded arrives on yet another.
//   Deferring with SKSE's AddTask cannot serve those hooks, so one lock around
//   the service serializes every caller instead.
// - The lock is recursive, so code that the service calls while it is held,
//   such as another module's crit shift or an event sink, may call
//   withService() again on the same thread.
// - The callable does service calls and copies results out, nothing else: no
//   game calls, no message boxes, no file I/O. The caller acts on the results
//   (applies the AvDeltas, shows a box) after withService() returns.
// - Nothing of the service leaves the callable but copies. withService()
//   rejects a callable that returns a reference or a pointer; no trait can
//   catch the rest, so these are forbidden too: views (string_view, span),
//   iterators, reference_wrappers, and pointers or references into the
//   service written into captured variables.
// - Nothing that holds the lock waits for another thread, for example for a
//   task queued with AddTask, a future, or another mutex: the thread that
//   holds what it waits for may be waiting for the stats lock. This holds for
//   the other modules' code the service calls too (crit shifts, event sinks):
//   it runs under the lock, so it queues game work and returns.
// - A module does not call the stats API while it holds a lock of its own
//   that its crit shift or event sink also takes: the service may call that
//   code under the stats lock on another thread, and each thread would wait
//   for the other's lock.
// - There is no service before kDataLoaded, nor after a load whose tables
//   could not be built. withService() then does not call the callable, and
//   the caller leaves the game as it is.

namespace service_detail
{
std::recursive_mutex&          mutex();
std::unique_ptr<StatsService>& instance();
}

// Sets the service, once at kDataLoaded and never from inside a callable of
// withService(). It takes the lock, so a hook on another thread sees the
// service either fully built or not at all. A second call is logged and
// refused, and returns false: replacing the service would destroy it under a
// callable that may still use it.
[[nodiscard]] bool installService(std::unique_ptr<StatsService> service);

// Calls `function` with the service under the lock. Returns whether it was
// called, or for a callable that returns a value, that value or nullopt
// without a service. So for a callable that returns a bool, the optional says
// whether a service exists, and the bool inside it is the callable's answer:
//
//     const auto getsPoints = withService([&](const StatsService& service) {
//         return service.registry().getsFreePoints(stat);
//     });
//     if (!getsPoints.value_or(false)) {
//         return;  // no service, or the stat gets no free points
//     }
template <class Function>
auto withService(Function&& function)
{
	using Result = std::invoke_result_t<Function, StatsService&>;
	static_assert(!std::is_reference_v<Result>, "return a copy: a reference would outlive the lock");
	static_assert(!std::is_pointer_v<Result>, "return a copy: a pointer would outlive the lock");

	const std::scoped_lock lock(service_detail::mutex());
	auto&                  service = service_detail::instance();
	if constexpr (std::is_void_v<Result>) {
		if (!service) {
			return false;
		}
		std::invoke(std::forward<Function>(function), *service);
		return true;
	} else {
		if (!service) {
			return std::optional<Result>{};
		}
		return std::optional<Result>{ std::invoke(std::forward<Function>(function), *service) };
	}
}
}
