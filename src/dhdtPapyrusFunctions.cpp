#include "dhdtPapyrusFunctions.h"
#include "dhdtOverrideManager.h"
#include "hdtSkyrimPhysicsWorld.h"

bool RegisterFuncs(RE::BSScript::IVirtualMachine* registry)
{
	//
	registry->RegisterFunction("ReloadPhysicsFile", "DynamicHDT", hdt::papyrus::ReloadPhysicsFile);
	registry->RegisterFunction("SwapPhysicsFile", "DynamicHDT", hdt::papyrus::SwapPhysicsFile);
	registry->RegisterFunction("QueryCurrentPhysicsFile", "DynamicHDT", hdt::papyrus::QueryCurrentPhysicsFile);
	registry->RegisterFunction("TogglePhysics", "DynamicHDT", hdt::papyrus::TogglePhysics);
	registry->RegisterFunction("ResetPhysics", "DynamicHDT", hdt::papyrus::ResetPhysics);
	//
	return true;
}

bool hdt::papyrus::RegisterAllFunctions(const SKSE::PapyrusInterface* a_papy_intfc)
{
	return a_papy_intfc->Register(RegisterFuncs);
}

//Some private/protected members are changed to public so that these functions can access them externally.
bool hdt::papyrus::ReloadPhysicsFile(RE::StaticFunctionTag*, RE::Actor* on_actor, RE::TESObjectARMA* on_item, RE::BSFixedString physics_file_path, bool persist, bool verbose_log)
{
	if (!(on_actor && on_item)) {
		if (verbose_log) {
			RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- Couldn't parse parameters: on_actor(ptr: %016X), on_item(ptr: %016X).", reinterpret_cast<uint64_t>(on_actor), reinterpret_cast<uint64_t>(on_item));
		}

		return false;
	}

	return impl::ReloadPhysicsFileImpl(on_actor->formID, on_item->formID, physics_file_path.c_str(), persist, verbose_log);
}

bool hdt::papyrus::SwapPhysicsFile(RE::StaticFunctionTag*, RE::Actor* on_actor, RE::BSFixedString old_physics_file_path, RE::BSFixedString new_physics_file_path, bool persist, bool verbose_log)
{
	if (!on_actor) {
		if (verbose_log) {
			RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- Couldn't parse parameters: on_actor(ptr: %016X).", reinterpret_cast<uint64_t>(on_actor));
		}

		return false;
	}

	return impl::SwapPhysicsFileImpl(on_actor->formID, old_physics_file_path.c_str(), new_physics_file_path.c_str(), persist, verbose_log);
}

RE::BSFixedString hdt::papyrus::QueryCurrentPhysicsFile(RE::StaticFunctionTag*, RE::Actor* on_actor, RE::TESObjectARMA* on_item, bool verbose_log)
{
	if (!(on_actor && on_item)) {
		if (verbose_log) {
			RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- Couldn't parse parameters: on_actor(ptr: %016X), on_item(ptr: %016X).", reinterpret_cast<uint64_t>(on_actor), reinterpret_cast<uint64_t>(on_item));
		}

		return "";
	}

	return impl::QueryCurrentPhysicsFileImpl(on_actor->formID, on_item->formID, verbose_log).c_str();
}

std::vector<bool> hdt::papyrus::TogglePhysics(RE::StaticFunctionTag*, RE::Actor* actor, std::vector<RE::BSFixedString> boneNames, bool on)
{
	if (!actor || boneNames.empty()) {
		return std::vector<bool>();
	}
	return impl::TogglePhysicsImpl(actor, boneNames, on);
}

std::vector<bool> hdt::papyrus::impl::TogglePhysicsImpl(RE::Actor* actor, std::vector<RE::BSFixedString>& boneNames, bool on)
{
	std::vector<bool> result(boneNames.size(), false);

	const auto AM = hdt::ActorManager::instance();
	auto guard = AM->lockGuard();
	auto& skeletons = AM->getSkeletons();

	for (auto& skeleton : skeletons) {
		if (!skeleton.skeleton) {
			continue;
		}
		auto owner = skeleton.skeleton->GetUserData();
		if (!owner || owner->formID != actor->formID) {
			continue;
		}

		{
			auto simLock = hdt::SkyrimPhysicsWorld::get()->lockSimulation();

			for (size_t i = 0; i < boneNames.size(); ++i) {
				bool foundAny = false;

				auto processBone = [&](SkinnedMeshBone* bone) {
					if (!bone)
						return;

					if (!foundAny) {
						result[i] = !bone->m_rig.isStaticOrKinematicObject();
						foundAny = true;
					}

					if (on) {
						bone->m_rig.setCollisionFlags(bone->m_rig.getCollisionFlags() & ~btCollisionObject::CF_KINEMATIC_OBJECT);
					} else {
						bone->m_rig.setCollisionFlags(bone->m_rig.getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
					}

					bone->m_rig.setLinearVelocity(btVector3(0, 0, 0));
					bone->m_rig.setAngularVelocity(btVector3(0, 0, 0));
				};

				for (auto& armor : skeleton.getArmors()) {
					if (!armor.m_physics)
						continue;
					processBone(armor.m_physics->findBone(boneNames[i]));
				}

				for (auto& headPart : skeleton.head.headParts) {
					if (!headPart.m_physics)
						continue;
					processBone(headPart.m_physics->findBone(boneNames[i]));
				}
			}
		}
	}

	return result;
}

void hdt::papyrus::ResetPhysics(RE::StaticFunctionTag*, RE::Actor* actor, bool full)
{
	if (!actor) {
		return;
	}
	impl::ResetPhysicsImpl(actor, full);
}

void hdt::papyrus::impl::ResetPhysicsImpl(RE::Actor* actor, bool full)
{
	const auto AM = hdt::ActorManager::instance();
	auto guard = AM->lockGuard();
	auto& skeletons = AM->getSkeletons();

	for (auto& skeleton : skeletons) {
		if (!skeleton.skeleton) {
			continue;
		}
		auto owner = skeleton.skeleton->GetUserData();
		if (!owner || owner->formID != actor->formID) {
			continue;
		}

		{
			auto simLock = hdt::SkyrimPhysicsWorld::get()->lockSimulation();
			if (full) {
				skeleton.reloadMeshes();
			} else {
				skeleton.softReloadMeshes();
			}
		}

		break;
	}
}

//
//UInt32 hdt::papyrus::FindOrCreateAnonymousSystem(StaticFunctionTag*, TESObjectARMA* system_model, bool verbose_log)
//{
//
//	return UInt32();
//}
//
//UInt32 hdt::papyrus::AttachAnonymousSystem(StaticFunctionTag*, Actor* on_actor, UInt32 system_handle, bool verbose_log)
//{
//	if (!on_actor || !system_handle) {
//		if (verbose_log)
//			Console_Print("[DynamicHDT] -- Couldn't parse parameters: on_actor(ptr: %016X), system_handle(%08X).", reinterpret_cast<UInt64>(on_actor), system_handle);
//		return false;
//	}
//
//
//
//	return UInt32();
//}
//
//UInt32 hdt::papyrus::DetachAnonymousSystem(StaticFunctionTag* base, Actor* on_actor, UInt32 system_handle, bool verbose_log)
//{
//	if (!on_actor || !system_handle) {
//		if (verbose_log)
//			Console_Print("[DynamicHDT] -- Couldn't parse parameters: on_actor(ptr: %016X), system_handle(%08X).", reinterpret_cast<UInt64>(on_actor), system_handle);
//		return false;
//	}
//
//	return UInt32();
//}

bool hdt::papyrus::impl::ReloadPhysicsFileImpl(uint32_t on_actor_formID, uint32_t on_item_formID, std::string physics_file_path, bool persist, bool verbose_log)
{
	const auto& AM = hdt::ActorManager::instance();

	auto guard = AM->lockGuard();
	auto& skeletons = AM->getSkeletons();

	bool character_found = false, armor_addon_found = false, succeeded = false;

	std::string old_physics_file_path;

	for (auto& skeleton : skeletons) {
		if (succeeded) {
			break;
		}

		if (!skeleton.skeleton) {
			continue;
		}

		auto owner = skeleton.skeleton->GetUserData();

		if (owner && owner->formID == on_actor_formID) {
			character_found = true;

			auto& armors = skeleton.getArmors();

			for (auto& armor : armors) {
				if (succeeded) {
					break;
				}

				if (!armor.armorWorn) {
					continue;
				}

				std::string armorName(armor.armorWorn->name);

				char buffer[16];
				sprintf_s(buffer, "%08X", on_item_formID);

				if (armorName.find(buffer) != std::string::npos) {
					armor_addon_found = true;
					//Force replacing and reloading. This could lead to assess violation
					try {
						if (armor.physicsFile.first == std::string(physics_file_path)) {
							if (verbose_log) {
								RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- Physics file paths are identical, skipping replacing.");
							}

							succeeded = true;
							continue;
						}

						old_physics_file_path = armor.physicsFile.first;
						armor.physicsFile.first = std::string(physics_file_path);
					} catch (std::exception& e) {
						RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] ERROR! -- Replacing physics file for ArmorAddon (%08X) on Character (%08X) failed.", on_item_formID, on_actor_formID);
						RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] Error(): {}\nWhat():\n\t{}", typeid(e).name(), e.what());

						return false;
					}

					RE::BSTSmartPointer<SkyrimSystem> system;

					{
						auto simLock = hdt::SkyrimPhysicsWorld::get()->lockSimulation();
						system = SkyrimSystemCreator().createOrUpdateSystem(skeleton.npc.get(), armor.armorWorn.get(), &armor.physicsFile, std::move(armor.renameMap), armor.m_physics.get());

						if (!system) {
							if (armor.hasPhysics()) {
								armor.clearPhysics();
							}
						} else {
							system->block_resetting = true;

							if (armor.hasPhysics()) {
								util::transferCurrentPosesBetweenSystems(armor.m_physics.get(), system.get());
							}

							armor.setPhysics(system, true);

							system->block_resetting = false;
						}
					}

					if (verbose_log) {
						RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- Physics file path switched, now is: \"{}\".", armor.physicsFile.first.c_str());
					}

					succeeded = true;
				}
			}
		}
	}

	//Push into global override data
	if (persist) {
		auto OM = Override::OverrideManager::GetSingleton();
		OM->registerOverride(on_actor_formID, old_physics_file_path, std::string(physics_file_path));
	}

	if (verbose_log) {
		RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- Character (%08X) {}, ArmorAddon (%08X) {}.", on_actor_formID, character_found ? "found" : "not found", on_item_formID, armor_addon_found ? "found" : "not found");
	}

	if (verbose_log && succeeded) {
		RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- ReloadPhysicsFile() succeeded.");
	}

	return succeeded;
}

bool hdt::papyrus::impl::SwapPhysicsFileImpl(uint32_t on_actor_formID, std::string old_physics_file_path, std::string new_physics_file_path, bool persist, bool verbose_log)
{
	const auto& AM = hdt::ActorManager::instance();

	auto guard = AM->lockGuard();
	auto& skeletons = AM->getSkeletons();

	bool character_found = false, armor_addon_found = false, succeeded = false;

	for (auto& skeleton : skeletons) {
		if (succeeded) {
			break;
		}

		if (!skeleton.skeleton) {
			continue;
		}

		auto owner = skeleton.skeleton->GetUserData();

		if (owner && owner->formID == on_actor_formID) {
			character_found = true;

			auto& armors = skeleton.getArmors();

			for (auto& armor : armors) {
				if (succeeded)
					break;

				if (armor.physicsFile.first == old_physics_file_path.c_str()) {
					armor_addon_found = true;

					//Force replacing and reloading. This could lead to assess violation
					try {
						if (armor.physicsFile.first == std::string(new_physics_file_path)) {
							if (verbose_log) {
								RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- Physics file paths are identical, skipping replacing.");
							}

							succeeded = true;
							continue;
						}

						armor.physicsFile.first = std::string(new_physics_file_path);
					} catch (std::exception& e) {
						std::string armorName(armor.armorWorn->name);

						uint32_t form_ID = util::splitArmorAddonFormID(armorName);

						RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] ERROR! -- Replacing physics file for ArmorAddon (%08X) on Character (%08X) failed.", form_ID, on_actor_formID);
						RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] Error(): {}\nWhat():\n\t{}", typeid(e).name(), e.what());

						return false;
					}

					RE::BSTSmartPointer<SkyrimSystem> system;

					{
						auto simLock = SkyrimPhysicsWorld::get()->lockSimulation();
						system = SkyrimSystemCreator().createOrUpdateSystem(skeleton.npc.get(), armor.armorWorn.get(), &armor.physicsFile, std::move(armor.renameMap), armor.m_physics.get());

						if (!system) {
							if (armor.hasPhysics()) {
								armor.clearPhysics();
							}
						} else {
							system->block_resetting = true;

							if (armor.hasPhysics()) {
								util::transferCurrentPosesBetweenSystems(armor.m_physics.get(), system.get());
							}

							armor.setPhysics(system, true);
							system->block_resetting = false;
						}
					}

					if (verbose_log) {
						RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- Physics file path switched, now is: \"{}\".", armor.physicsFile.first.c_str());
					}

					succeeded = true;
				}
			}
		}
	}

	if (persist) {
		auto OM = Override::OverrideManager::GetSingleton();
		OM->registerOverride(on_actor_formID, old_physics_file_path.c_str(), new_physics_file_path.c_str());
	}

	if (verbose_log) {
		RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- Character (%08X) {}, Physics file path {}.", on_actor_formID, character_found ? "found" : "not found", armor_addon_found ? "found" : "not found");
	}

	if (verbose_log && succeeded) {
		RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- SwapPhysicsFile() succeeded.");
	}

	return succeeded;
}

std::string hdt::papyrus::impl::QueryCurrentPhysicsFileImpl(uint32_t on_actor_formID, uint32_t on_item_formID, bool verbose_log)
{
	const auto& AM = hdt::ActorManager::instance();

	auto guard = AM->lockGuard();
	auto& skeletons = AM->getSkeletons();

	bool character_found = false, armor_addon_found = false, succeeded = false;

	std::string physics_file_path;

	for (auto& skeleton : skeletons) {
		if (succeeded) {
			break;
		}

		if (!skeleton.skeleton) {
			continue;
		}

		auto owner = skeleton.skeleton->GetUserData();

		if (owner && owner->formID == on_actor_formID) {
			character_found = true;

			auto& armors = skeleton.getArmors();

			for (auto& armor : armors) {
				if (succeeded) {
					break;
				}

				if (!armor.armorWorn) {
					continue;
				}

				std::string armorName(armor.armorWorn->name);

				char buffer[16];
				sprintf_s(buffer, "%08X", on_item_formID);

				if (armorName.find(buffer) != std::string::npos) {
					armor_addon_found = true;
					physics_file_path = armor.physicsFile.first;
					succeeded = true;
				}
			}
		}
	}

	if (verbose_log) {
		RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- Character (%08X) {}, ArmorAddon (%08X) {}.", on_actor_formID, character_found ? "found" : "not found", on_item_formID, armor_addon_found ? "found" : "not found");
	}

	if (verbose_log && succeeded) {
		RE::ConsoleLog::GetSingleton()->Print("[DynamicHDT] -- QueryCurrentPhysicsFile() querying successful.");
	}

	return physics_file_path.c_str();
}
