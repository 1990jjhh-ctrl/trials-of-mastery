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

}
