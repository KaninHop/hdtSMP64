#include "hdtCpuTopology.h"

#include <tbb/info.h>
#include <tbb/task_arena.h>

#include <intrin.h>
#include <immintrin.h>
#include <windows.h>

#include <memory>
#include <vector>

namespace hdt::cpu
{
	namespace
	{
		// ---------------------------------------------------------------------
		// Topology probe (run once, cached)
		// ---------------------------------------------------------------------

		struct Topology
		{
			unsigned logical = 1;
			unsigned physical = 1;
			unsigned pCoreLogical = 1;
			bool hybrid = false;
			ULONG_PTR pCoreMask = 0;
		};

		const Topology& topology()
		{
			static const Topology t = [] {
				Topology r;
				SYSTEM_INFO si{};
				GetSystemInfo(&si);
				r.logical = std::max<unsigned>(1, si.dwNumberOfProcessors);
				r.physical = r.logical;
				r.pCoreLogical = r.logical;

				DWORD len = 0;
				GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
				if (len == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
					return r;

				auto buffer = std::make_unique<std::byte[]>(len);
				auto* base = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.get());
				if (!GetLogicalProcessorInformationEx(RelationProcessorCore, base, &len))
					return r;

				unsigned physical = 0;
				BYTE maxEff = 0;
				std::vector<std::pair<BYTE, ULONG_PTR>> cores;
				DWORD offset = 0;
				while (offset < len) {
					auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.get() + offset);
					if (info->Relationship == RelationProcessorCore && info->Processor.GroupCount >= 1) {
						ULONG_PTR mask = info->Processor.GroupMask[0].Mask;
						cores.emplace_back(info->Processor.EfficiencyClass, mask);
						++physical;
						maxEff = std::max(maxEff, info->Processor.EfficiencyClass);
					}
					offset += info->Size;
				}

				if (physical == 0)
					return r;

				r.physical = physical;
				r.pCoreMask = 0;
				unsigned pLogical = 0;
				bool mixed = false;
				for (auto& [eff, mask] : cores) {
					if (eff != maxEff)
						mixed = true;
					if (eff == maxEff) {
						r.pCoreMask |= mask;
						for (ULONG_PTR m = mask; m; m &= m - 1)
							++pLogical;
					}
				}
				r.hybrid = mixed;
				r.pCoreLogical = std::max<unsigned>(1, pLogical);
				return r;
			}();
			return t;
		}

		// ---------------------------------------------------------------------
		// CPU feature / vendor checks
		// ---------------------------------------------------------------------

		bool detectAVX512()
		{
			int info[4]{};
			__cpuid(info, 0);
			const int nIds = info[0];
			if (nIds < 7)
				return false;

			__cpuidex(info, 1, 0);
			const bool osUsesXSAVE = (info[2] & (1 << 27)) != 0;
			if (!osUsesXSAVE)
				return false;

			const unsigned long long xcr0 = _xgetbv(0);
			// XMM + YMM + opmask + ZMM hi256 + ZMM hi16
			constexpr unsigned long long kAVX512Mask = 0xE6ull;
			if ((xcr0 & kAVX512Mask) != kAVX512Mask)
				return false;

			__cpuidex(info, 7, 0);
			return (info[1] & (1 << 16)) != 0;  // AVX-512F
		}

		bool supportsAVX512F()
		{
			static const bool v = detectAVX512();
			return v;
		}

		bool isAmdVendor()
		{
			int info[4]{};
			__cpuid(info, 0);
			return info[1] == 0x68747541 &&  // "Auth"
			       info[3] == 0x69746e65 &&  // "enti"
			       info[2] == 0x444d4163;    // "cAMD"
		}

		// ---------------------------------------------------------------------
		// Helpers
		// ---------------------------------------------------------------------

		std::string maskToList(ULONG_PTR mask)
		{
			std::string s;
			for (ULONG_PTR m = mask; m; m &= m - 1) {
				unsigned long idx = 0;
				_BitScanForward64(&idx, static_cast<unsigned __int64>(m));
				if (!s.empty()) s += ',';
				s += std::to_string(idx);
			}
			return s;
		}

		tbb::task_arena::constraints buildArenaConstraints()
		{
			tbb::task_arena::constraints cs;
			auto types = tbb::info::core_types();
			// info::core_types() is ordered by performance class, ascending.
			// Last entry = highest-performance type: P-cores on hybrid Intel,
			// the only type on uniform CPUs.
			if (!types.empty())
				cs.set_core_type(types.back());
			return cs;
		}

		// ---------------------------------------------------------------------
		// initRuntime() pipeline stages
		// ---------------------------------------------------------------------

		// Refuse to load an AVX-512 binary on a CPU without AVX-512F.
		bool checkAvxRequirement(std::string_view avxVariant)
		{
			if (avxVariant != "avx512" || supportsAVX512F())
				return true;

			SKSE::log::critical(
				"AVX-512 build loaded on a CPU without AVX-512F support. "
				"Use the AVX2 or AVX build. Refusing to load.");
			MessageBoxA(
				nullptr,
				"hdtSMP64: AVX-512 build loaded on a CPU that does not support AVX-512F.\n"
				"Install the AVX2 (or AVX) variant instead.",
				"hdtSMP64",
				MB_OK | MB_ICONERROR);
			return false;
		}

		void logTopologySummary(const Topology& t, int arenaConcurrency)
		{
			SKSE::log::info(
				"CPU topology: {} logical / {} physical / {} P-core logical, hybrid={}. "
				"Physics arena concurrency={}.",
				t.logical, t.physical, t.pCoreLogical, t.hybrid, arenaConcurrency);
		}

		// On hybrid Intel CPUs, log P-core / E-core processor index lists at debug level.
		void logHybridProcessorIndices(const Topology& t)
		{
			if (!t.hybrid)
				return;

			ULONG_PTR allMask = (t.logical < 64) ? ((ULONG_PTR(1) << t.logical) - 1) : ~ULONG_PTR(0);
			ULONG_PTR eMask = allMask & ~t.pCoreMask;
			SKSE::log::debug("P-core logical processors: [{}]", maskToList(t.pCoreMask));
			if (eMask)
				SKSE::log::debug("E-core logical processors: [{}]", maskToList(eMask));
		}

		// Enumerate L3 caches into {size, mask} pairs. One entry per CCD on AMD parts.
		std::vector<std::pair<DWORD, ULONG_PTR>> enumerateL3Caches()
		{
			std::vector<std::pair<DWORD, ULONG_PTR>> l3s;

			DWORD len = 0;
			GetLogicalProcessorInformationEx(RelationCache, nullptr, &len);
			if (len == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
				return l3s;

			auto buf = std::make_unique<std::byte[]>(len);
			auto* base = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.get());
			if (!GetLogicalProcessorInformationEx(RelationCache, base, &len))
				return l3s;

			DWORD offset = 0;
			while (offset < len) {
				auto* ci = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.get() + offset);
				if (ci->Relationship == RelationCache &&
				    ci->Cache.Level == 3 &&
				    ci->Cache.Type != CacheInstruction)
					l3s.emplace_back(ci->Cache.CacheSize, ci->Cache.GroupMask.Mask);
				offset += ci->Size;
			}
			return l3s;
		}

		// Diagnostic only: log multi-CCD AMD topology and current process pinning.
		// No affinity is changed; CCD pinning is left to the OS / chipset driver.
		void logAmdMultiCcdDiagnostic(const Topology& t)
		{
			if (t.hybrid || !isAmdVendor())
				return;

			auto l3s = enumerateL3Caches();
			if (l3s.size() <= 1)
				return;

			DWORD maxSize = 0, minSize = ~DWORD(0);
			for (auto& [sz, _] : l3s) {
				maxSize = std::max(maxSize, sz);
				minSize = std::min(minSize, sz);
			}
			const bool asymmetric = (maxSize != minSize);

			std::string sizeList;
			for (auto& [sz, _] : l3s) {
				if (!sizeList.empty()) sizeList += '/';
				sizeList += std::to_string(sz / (1024 * 1024)) + "MB";
			}

			SKSE::log::info(
				"AMD multi-CCD detected: {} CCDs, L3 sizes {} (asymmetric={}{}).",
				l3s.size(), sizeList, asymmetric,
				asymmetric ? " — likely X3D V-Cache part" : "");

			ULONG_PTR procMask = 0, sysMask = 0;
			if (!GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask))
				return;

			int ccdsTouched = 0;
			ULONG_PTR pinnedCcdMask = 0;
			for (auto& [sz, mask] : l3s) {
				if (procMask & mask) {
					++ccdsTouched;
					pinnedCcdMask = mask;
				}
			}
			if (ccdsTouched == 1) {
				SKSE::log::info(
					"Process is already pinned to a single CCD (mask 0x{:X}).",
					static_cast<unsigned long long>(pinnedCcdMask));
			} else {
				SKSE::log::info(
					"Process can run across {} CCDs; OS / chipset driver will steer scheduling.",
					ccdsTouched);
			}
		}
	}

	tbb::task_arena& physicsArena()
	{
		// Allocated once and intentionally leaked: tbb::task_arena is neither
		// copyable nor movable, and a singleton's lifetime is process-bound.
		static tbb::task_arena* arena = new tbb::task_arena(buildArenaConstraints());
		return *arena;
	}

	bool initRuntime(std::string_view avxVariant)
	{
		if (!checkAvxRequirement(avxVariant))
			return false;

		auto& arena = physicsArena();
		arena.initialize();

		const auto& t = topology();
		logTopologySummary(t, arena.max_concurrency());
		logHybridProcessorIndices(t);
		logAmdMultiCcdDiagnostic(t);

		return true;
	}
}
