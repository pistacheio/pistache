#pragma once

namespace Pistache {

#define REQUIRES(condition) typename std::enable_if<(condition), int>::type = 0

} // namespace Pistache