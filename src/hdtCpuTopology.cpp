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

		bool supportsAVX512F()
		{
			static const bool v = detectAVX512();
			return v;
		}

		// Hybrid: size pools to P-core logicals. Uniform: use physical core count
		// to avoid SMT-sibling FPU contention in FMA-heavy skinning/solver work.
		unsigned recommendedWorkerCount()
		{
			const auto& t = topology();
			const unsigned base = t.hybrid ? t.pCoreLogical : t.physical;
			return std::max<unsigned>(1, base);
		}
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

		if (t.hybrid && t.pCoreMask != 0) {
			HANDLE proc = GetCurrentProcess();
			ULONG_PTR procMask = 0, sysMask = 0;
			if (GetProcessAffinityMask(proc, &procMask, &sysMask)) {
				ULONG_PTR overlap = t.pCoreMask & procMask;
				if (overlap == 0) {
					if (SetProcessAffinityMask(proc, t.pCoreMask)) {
						SKSE::log::warn(
							"Existing process affinity (0x{:X}) had no overlap with P-cores (0x{:X}); "
							"overriding to recover from misconfigured external pinning.",
							static_cast<unsigned long long>(procMask),
							static_cast<unsigned long long>(t.pCoreMask));
					}
				} else if (overlap != procMask) {
					if (SetProcessAffinityMask(proc, overlap)) {
						SKSE::log::info(
							"Restricted process affinity to P-cores (mask 0x{:X}).",
							static_cast<unsigned long long>(overlap));
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

		auto maskToList = [](ULONG_PTR mask) {
			std::string s;
			for (ULONG_PTR m = mask; m; m &= m - 1) {
				unsigned long idx = 0;
				_BitScanForward64(&idx, static_cast<unsigned __int64>(m));
				if (!s.empty()) s += ',';
				s += std::to_string(idx);
			}
			return s;
		};
		if (t.hybrid) {
			ULONG_PTR allMask = (t.logical < 64) ? ((ULONG_PTR(1) << t.logical) - 1) : ~ULONG_PTR(0);
			ULONG_PTR eMask = allMask & ~t.pCoreMask;
			SKSE::log::debug("P-core logical processors: [{}]", maskToList(t.pCoreMask));
			if (eMask)
				SKSE::log::debug("E-core logical processors: [{}]", maskToList(eMask));
		}

		// Diagnostic only: detect multi-CCD AMD parts via L3 topology and log whether
		// the process is already masked to a single CCD. We do not change affinity here;
		// CCD pinning (V-Cache awareness, etc.) is left to the OS / chipset driver.
		{
			int cpuInfo[4]{};
			__cpuid(cpuInfo, 0);
			const bool isAMD = (cpuInfo[1] == 0x68747541 &&  // "Auth"
			                    cpuInfo[3] == 0x69746e65 &&  // "enti"
			                    cpuInfo[2] == 0x444d4163);   // "cAMD"
			if (isAMD && !t.hybrid) {
				DWORD clen = 0;
				GetLogicalProcessorInformationEx(RelationCache, nullptr, &clen);
				if (clen > 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
					auto cbuf = std::make_unique<std::byte[]>(clen);
					auto* cbase = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(cbuf.get());
					if (GetLogicalProcessorInformationEx(RelationCache, cbase, &clen)) {
						std::vector<std::pair<DWORD, ULONG_PTR>> l3s;
						DWORD coffset = 0;
						while (coffset < clen) {
							auto* ci = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(cbuf.get() + coffset);
							if (ci->Relationship == RelationCache &&
							    ci->Cache.Level == 3 &&
							    ci->Cache.Type != CacheInstruction)
								l3s.emplace_back(ci->Cache.CacheSize, ci->Cache.GroupMask.Mask);
							coffset += ci->Size;
						}
						if (l3s.size() > 1) {
							DWORD maxSize = 0, minSize = ~DWORD(0);
							for (auto& [sz, _] : l3s) { maxSize = std::max(maxSize, sz); minSize = std::min(minSize, sz); }
							const bool asymmetric = (maxSize != minSize);
							SKSE::log::info(
								"AMD multi-CCD detected: {} CCDs, L3 sizes {} (asymmetric={}{}).",
								l3s.size(),
								[&] {
									std::string s;
									for (auto& [sz, _] : l3s) { if (!s.empty()) s += '/'; s += std::to_string(sz / (1024 * 1024)) + "MB"; }
									return s;
								}(),
								asymmetric,
								asymmetric ? " — likely X3D V-Cache part" : "");

							ULONG_PTR procMask = 0, sysMask = 0;
							if (GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask)) {
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
					}
				}
			}
		}

		return true;
	}
}
