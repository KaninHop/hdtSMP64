# hdtSMP with CUDA for Skyrim SE/AE/VR

Fork of [https://github.com/Karonar1/hdtSMP64] by Karonar1, of fork of [version](https://github.com/aers/hdtSMP64) by aers, from
[original code](https://github.com/HydrogensaysHDT/hdt-skyrimse-mods) by hydrogensaysHDT

## Changes

- CommonLibSSE-NG used for building instead of staticlly linking with SKSE.
- Added maximum angle in ActorManager. A max angle can be used to specify physics on NPCs within a field of view.
  0 degrees represents straight in front of the camera. Default is 45 which is treated as + or - 45 degrees,
  so 90 total degrees. 180 would be all around.
- Merged the CUDA version in the master branch through preprocessor directives to allow a continuous evolution,
  and easier building.
- Added CUDA support for several parts of collision detection (still a work in progress). This includes
  everything that had OpenCL support in earlier releases, as well as the final collision check. CPU collision
  is still fully supported, and is used as a fallback if a CUDA-capable GPU is not available.
- Added distance check in ActorManager to disable NPCs more than a certain distance from the player. This
  resolves the massive FPS drop in certain cell transitions (such as Blue Palace -> Solitude). Default
  maximum distance is 500.
- New method for dynamically updating timesteps. This is technically less stable in terms of physics
  accuracy, but avoids a problem where one very slow frame (for example, where NPCs are added to the scene)
  causes a feedback loop that drops the framerate. If you saw framerates drop to 12-13 in the Blue Palace or
  at the Fire Festival, this should help.
- Added can-collide-with-bone to mesh definitions, as a natural counterpart to no-collide-with-bone.
- Added new "external" sharing option, for objects that should collide with other NPCs, but not this one.
  Good for defining the whole body as a collision mesh for interactions between characters.
- Significant refactoring of armor handling in ActorManager, to be much stricter about disabling systems and
  reducing gradual FPS loss.
- Changed prefix mechanism to use a simple incrementing value instead of trying to use pointers to objects.
  Previously this could lead to prefix collisions with a variety of weird effects and occasional crashes.
- Skeletons should remain active as long as they are in the same cell as (and close enough to) the player
  character. Resolves an issue where entering the Ancestor Glade often incorrectly marked skeletons as
  inactive and disabled physics.
- Added `smp list` console command to list tracked NPCs without so much detail - useful for checking which
  NPCs are active in crowded areas. NPCs are now sorted according to active status, with active ones last.
- New mechanism for remapping mesh names in the defaultBBPs.xml file, allowing much more concise ways of
  defining complex collision objects for lots of armors at once.
- The code to scan defaultBBPs.xml can now handle the structure of facegen files, which means head part
  physics should work (with limitations) on NPCs without having to manually edit the facegen data.
- New bones from facegen files should now be added to the head instead of the NPC root, so they should be
  positioned correctly if there is no physics for them or after a reset.

## Note about NPC head parts

Head parts work fine for NPCs without valid facegen data, but this isn't very useful because it triggers the
infamous dark face bug. Special restrictions apply to NPCs that do have facegen data:

- Only one XML file can be used per face, even if was built from multiple physics-enabled parts.
- NiStringExtraData nodes aren't automatically copied into the facegen file, so physics won't work
  automatically. Either do it manually in NifSkope or map one of the head parts to a file in defaultBBPs.xml.
- Bones that aren't explicitly referenced by any mesh are removed when facegen data is generated, and can't
  be used as kinematic objects in constraints. Replace references to these with the NPC head (which should
  always be present). You may also need to set the frame of reference origin to the position of the missing
  bone relative to the head to get correct constraint behavior.

## Console commands

The `smp` console command will print some basic information about the number of tracked and active objects.
The plugin recognizes the following optional parameters:

- `smp reset` reloads the configs.xml file, attempts to reload all meshes and reset the whole HDT-SMP system.
  However, it is a little buggy and may fail to reload some meshes or constraints properly.
- `smp gpu` toggles the CUDA collision algorithm, if there is at least one CUDA device available. If there is
  no device available, it does nothing.
- `smp timing` starts a timing sequence for the collision detection algorithm. The next 200 frames will
  switch between CPU and GPU collision. Once complete, mean and standard deviation of timings for the two
  collision algorithms are displayed on the console.
- `smp dumptree` dumps the entire node tree of the current targeted NPC to the log file.
- `smp detail` shows extended details of all tracked actors, including active and inactive armour and head
  parts.
- `smp list` shows a more concise list of tracked actors.

## Coming soon (maybe)

- Reworked tag system for better compartmentalized .xml files.
- Continued work on CUDA algorithms.

## Known issues

- Several options, including shape and collision definitions on bones, exist but don't seem to do anything.
- Sphere-triangle collision check without penetration defined is obviously wrong, but fixing the test
  doesn't improve things. Needs further investigation.
- Smp reset doesn't reload meshes correctly for some items (observed with Artesian Cloaks of Skyrim).
  Suspect references to the original triangle meshes are being dropped when they're no longer needed. We
  could keep ownership of the meshes, but it seems pretty marginal and a waste of memory. It also breaks
  some constraints for NPCs with facegen data.
- It's not possible to refer to bones in facegen data that aren't used explicitly by at least one mesh. Most
  HDT hairs won't work as non-wig hair on NPCs without altering constraints. Probably possible but annoying
  to fix.
- Physics enabled hair colours are sometimes wrong. This appears to happen there are two NPCs nearby using
  the same hair model.
- Physics may be less stable due to the new dynamic timestep calculation.
- Probably any open bug listed on Nexus that isn't resolved in changes, above. This list only contains
  issues I have personally observed.
- [Known bugs on the Nexus](https://nexusmods.com/skyrimspecialedition/mods/57339?tab=bugs&BH=5)

## Build Instructions And Requirements

- [CMake](https://cmake.org/)
  - Add this to your `PATH`
- [PowerShell](https://github.com/PowerShell/PowerShell/releases/latest)
- [Vcpkg](https://github.com/microsoft/vcpkg)
  - Add the environment variable `VCPKG_ROOT` with the value as the path to the folder containing vcpkg
- [Visual Studio Community 2022](https://visualstudio.microsoft.com/)
  - Desktop development with C++
- [CommonLibSSENG](https://github.com/alandtse/CommonLibVR/tree/ng)
  - Add this as as an environment variable `CommonLibSSEPath`

## User Requirements

- [Address Library for SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
  - Needed for SSE/AE
- [VR Address Library for SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/58101)
  - Needed for VR

## Register Visual Studio as a Generator

- Open `x64 Native Tools Command Prompt`
- Run `cmake`
- Close the cmd window

## Building

```
#
git clone -b master https://github.com/DaymareOn/hdtSMP64.git

#
cd hdtSMP64

#
git submodule update --init --recursive

#
cmake --preset vs2022-windows-nocuda-avx2

#
cmake --build --preset avx2-release
```

## Build Targets

vs2022-windows-nocuda<br>
vs2022-windows-nocuda-avx<br>
vs2022-windows-nocuda-avx2<br>
vs2022-windows-nocuda-avx512<br>

## Credits

- hydrogensaysHDT - Creating this plugin
- aers - fixes and improvements
- ousnius - some fixes, and "consulting"
- karonar1 - bug fixes (the real work), I'm just building it
