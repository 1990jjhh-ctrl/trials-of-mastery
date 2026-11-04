#pragma once

#include <string>

namespace tom::stats
{
// Why a core operation failed, as a message for the log.
struct Error
{
	std::string message;
};
}
