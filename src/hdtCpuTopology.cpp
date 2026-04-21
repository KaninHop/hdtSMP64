#include "hdtCpuTopology.h"

#include <tbb/global_control.h>

#include <intrin.h>
#include <immintrin.h>
#include <windows.h>

#include <memory>
#include <vector>

namespace hdt::cpu
{
	namespace
	{
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

		std::unique_ptr<tbb::global_control>& tbbCap()
		{
			static std::unique_ptr<tbb::global_control> inst;
			return inst;
		}
	}

	bool supportsAVX512F()
	{
		static const bool v = detectAVX512();
		return v;
	}

	unsigned physicalCoreCount() { return topology().physical; }
	unsigned performanceCoreCount() { return topology().pCoreLogical; }
	bool     isHybrid() { return topology().hybrid; }

	unsigned recommendedWorkerCount()
	{
		const auto& t = topology();
		// Hybrid: size pools to P-core logicals. Uniform: use physical core count
		// to avoid SMT-sibling FPU contention in FMA-heavy skinning/solver work.
		const unsigned base = t.hybrid ? t.pCoreLogical : t.physical;
		return std::max<unsigned>(1, base);
	}

	bool initRuntime(std::string_view avxVariant)
	{
		const bool wantAvx512 = (avxVariant == "avx512");

		if (wantAvx512 && !supportsAVX512F()) {
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

		const auto& t = topology();

		if (wantAvx512 && t.hybrid && t.pCoreMask != 0) {
			// Intel hybrid CPUs fuse AVX-512 off on E-cores. If a user has
			// unlocked it on P-cores via BIOS, the OS can still schedule our
			// threads onto E-cores and trigger #UD. Pin process to P-cores.
			HANDLE proc = GetCurrentProcess();
			ULONG_PTR procMask = 0, sysMask = 0;
			if (GetProcessAffinityMask(proc, &procMask, &sysMask)) {
				ULONG_PTR target = t.pCoreMask & procMask;
				if (target != 0 && target != procMask) {
					if (SetProcessAffinityMask(proc, target)) {
						SKSE::log::warn(
							"AVX-512 build on hybrid CPU: restricted process affinity to P-cores "
							"(mask 0x{:X}) to avoid #UD on E-cores.",
							static_cast<unsigned long long>(target));
					}
				}
			}
		}

		const unsigned workers = recommendedWorkerCount();
		tbbCap() = std::make_unique<tbb::global_control>(
			tbb::global_control::max_allowed_parallelism, workers);

		SKSE::log::info(
			"CPU topology: {} logical / {} physical / {} P-core logical, hybrid={}. "
			"TBB max_allowed_parallelism={}.",
			t.logical, t.physical, t.pCoreLogical, t.hybrid, workers);

		return true;
	}
}
