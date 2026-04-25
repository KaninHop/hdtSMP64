#pragma once

#include <string_view>

namespace hdt::cpu
{
	// Call once at plugin load. Returns false if the running binary requires an
	// ISA the current CPU does not provide (caller should abort load).
	// Installs a TBB concurrency cap sized to P-core logicals (hybrid) or physical
	// cores (uniform) and logs CPU topology diagnostics.
	bool initRuntime(std::string_view avxVariant);
}
