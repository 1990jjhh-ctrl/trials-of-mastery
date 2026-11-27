// SKSE entry point of the Stats plugin.

#include "Messaging.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// Opens Documents/My Games/Skyrim Special Edition/SKSE/<plugin name>.log.
	// The trampoline holds a 14-byte jump for each of the level-up hook's two
	// 5-byte calls and for the two vanilla calls of the CSF skill spike (S7,
	// throwaway).
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 56 });
	SKSE::log::info("{} {} loading on Skyrim {}", SKSE::GetPluginName(),
		SKSE::GetPluginVersion().string("."), a_skse->RuntimeVersion().string("."));

	if (!tom::stats::registerMessageListener()) {
		SKSE::log::critical("Could not register the SKSE message listener");
		return false;
	}
	return true;
}
