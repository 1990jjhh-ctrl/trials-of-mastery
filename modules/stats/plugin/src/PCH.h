#pragma once

// CommonLibSSE-NG headers expect to be compiled after its own PCH header.
#include <SKSE/Impl/PCH.h>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

// The plugin version data that target_commonlibsse_properties() generates uses
// "..."sv without declaring the literal operator itself.
using namespace std::literals;
