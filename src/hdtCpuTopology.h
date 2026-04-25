#pragma once

#include <string_view>

namespace hdt::cpu
{
	// Call once at plugin load. Returns false if the running binary requires an
	// ISA the current CPU does not provide (caller should abort load).
	// Also installs a process-wide TBB concurrency cap and, on hybrid Intel CPUs,
	// pins process affinity to P-cores.
	bool initRuntime(std::string_view avxVariant);
}
