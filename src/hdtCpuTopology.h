#pragma once

#include <tbb/task_arena.h>

#include <string_view>

namespace hdt::cpu
{
	// Call once at plugin load. Returns false if the running binary requires an
	// ISA the current CPU does not provide (caller should abort load).
	// Configures the FSMP physics task arena and logs CPU topology diagnostics.
	bool initRuntime(std::string_view avxVariant);

	// FSMP physics task arena. On hybrid Intel CPUs the arena is constrained to
	// P-cores; on uniform CPUs it uses the single core type. Wrap any FSMP /
	// Bullet parallel work in `physicsArena().execute([&]{ ... });` so workers
	// are scoped to this arena rather than affecting the whole process.
	tbb::task_arena& physicsArena();
}
