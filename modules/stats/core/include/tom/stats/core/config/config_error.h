#pragma once

#include <string>

namespace tom::stats
{

// A configuration value that was rejected, for the log.
struct ConfigError
{
	std::string file;  // the file or files that set the value; empty for the INI
	std::string key;   // the value's place in the file
	std::string message;
};

// A placeholder value in use, for the log (Stats plan, "Conventions"): its
// note names the open question, e.g. "TODO(OQ-2): ...".
struct Placeholder
{
	std::string file;  // as in ConfigError
	std::string key;   // the object that holds the placeholder
	std::string note;
};

}
