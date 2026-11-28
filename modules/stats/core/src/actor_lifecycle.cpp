#include "tom/stats/core/actor_lifecycle.h"

#include <iterator>
#include <utility>

namespace tom::stats
{

namespace
{
// Appends the deltas of from to those of the same actor in into.
void append(AvDeltasByActor& into, AvDeltasByActor from)
{
	for (auto& [actor, deltas] : from) {
		auto& target = into[actor];
		target.insert(target.end(), std::make_move_iterator(deltas.begin()), std::make_move_iterator(deltas.end()));
	}
}
}

std::vector<AvDelta> ensureActor(StatsService& service, ActorId actor, std::string_view race)
{
	if (!service.hasActor(actor)) {
		// Cannot fail: the actor is not known.
		return *service.addActor(actor, race);
	}
	// Cannot fail: the actor is known.
	return *service.setRace(actor, race);
}

AvDeltasByActor renewAfterLoad(StatsService& service, const ActorRaces& races)
{
	AvDeltasByActor deltas;
	for (const auto& [actor, race] : races) {
		auto changes = ensureActor(service, actor, race);
		if (!changes.empty()) {
			deltas.emplace(actor, std::move(changes));
		}
	}
	append(deltas, service.applyDefaultBases());
	return deltas;
}

}
