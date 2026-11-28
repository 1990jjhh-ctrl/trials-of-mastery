#pragma once

namespace tom::stats
{
// Loads the INI and the four tables (docs/stats/PLAN.md, G1), logs what the
// load found and installs the StatsService (Service.h) built from them. Call
// once, at kDataLoaded. Without tables that can be built there is no service;
// the log then says why.
void loadConfigurationAndService();
}
