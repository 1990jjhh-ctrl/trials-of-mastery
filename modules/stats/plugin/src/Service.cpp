#include "Service.h"

#include <atomic>

namespace tom::stats
{
namespace service_detail
{
std::recursive_mutex& mutex()
{
	static std::recursive_mutex instance;
	return instance;
}

std::unique_ptr<StatsService>& instance()
{
	static std::unique_ptr<StatsService> instance;
	return instance;
}
}

bool installService(std::unique_ptr<StatsService> service)
{
	// Checked before the lock is taken, so a call from inside a callable of
	// withService(), which can only run once a service exists, is refused
	// without touching the service it runs on.
	static std::atomic<bool> installed{ false };
	if (installed.exchange(true)) {
		SKSE::log::error("The stats service is already installed; the new one is discarded");
		return false;
	}
	const std::scoped_lock lock(service_detail::mutex());
	service_detail::instance() = std::move(service);
	return true;
}
}
