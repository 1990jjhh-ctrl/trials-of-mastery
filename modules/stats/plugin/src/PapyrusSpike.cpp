#include "PapyrusSpike.h"

#include <cstdint>
#include <string_view>

namespace tom::stats
{
namespace
{
constexpr auto         kScript = "TrialsOfMasteryStatsSpike"sv;
constexpr std::int32_t kAnswer = 42;

std::int32_t spikeAnswer(RE::StaticFunctionTag*)
{
	SKSE::log::info("Papyrus spike: SpikeAnswer called, returns {}", kAnswer);
	return kAnswer;
}

void spikeLog(RE::StaticFunctionTag*, RE::BSFixedString msg)
{
	SKSE::log::info("Papyrus spike: SpikeLog called: {}", msg.c_str());
}

bool registerNatives(RE::BSScript::IVirtualMachine* vm)
{
	vm->RegisterFunction("SpikeAnswer"sv, kScript, spikeAnswer);
	vm->RegisterFunction("SpikeLog"sv, kScript, spikeLog);
	SKSE::log::info("Papyrus spike: natives of {} registered", kScript);
	return true;
}

// Logs the return value of a call dispatched from the plugin. Runs on a
// Papyrus thread once the call has finished.
class ResultLogger : public RE::BSScript::IStackCallbackFunctor
{
public:
	explicit ResultLogger(std::string_view function) : function_(function) {}

	void operator()(RE::BSScript::Variable result) override
	{
		if (result.IsInt()) {
			SKSE::log::info("Papyrus spike: {} returned {} to the plugin", function_, result.GetSInt());
			return;
		}
		SKSE::log::info("Papyrus spike: {} finished", function_);
	}

	void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

private:
	std::string_view function_;
};

void dispatch(RE::BSScript::Internal::VirtualMachine* vm, std::string_view function,
	RE::BSScript::IFunctionArguments* args)
{
	RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new ResultLogger(function) };
	const bool                                               dispatched = vm->DispatchStaticCall(kScript, function, args, callback);
	SKSE::log::info("Papyrus spike: dispatch of {}.{} {}", kScript, function,
		dispatched ? "queued" : "failed");
}
}

bool registerPapyrusSpike()
{
	return SKSE::GetPapyrusInterface()->Register(registerNatives);
}

void callPapyrusSpike()
{
	auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
	if (vm == nullptr) {
		SKSE::log::error("Papyrus spike: no Papyrus VM");
		return;
	}
	dispatch(vm, "SpikeAnswer"sv, RE::MakeFunctionArguments());
	dispatch(vm, "SpikeLog"sv, RE::MakeFunctionArguments(RE::BSFixedString{ "called from kPostLoadGame" }));
}
}
