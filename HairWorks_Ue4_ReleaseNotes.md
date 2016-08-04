﻿HairWorks Preview Release 3
=============
New:
* Upgraded to HairWorks 1.2.
* Upgraded to UE4.12.
* Added support for dynamic pin.
* Added support for actor instance editing. Now you can modify a HairWorks component in a level.
* Added support for IES lighting.
* Added support for light function.
* Made Override flag of HairWorks component writable in Blueprints.
* Added support for HairWorks components selection.
* Added support for asset thumbnail.
* Added stats information. Which can be toggled by r.HairWorks.Stats.
* Added HairWorks logger.
* Added experimental implementation of frame rate independent rendering. You can turn it on by r.HairWorks.FrameRateIndependentRendering.
* Improved performance.
Fix:
* Fixed a problem that D3D compiler DLL is not found.
* Removed the stutter when a HairWorks component is created. We now create HairWorks assets and compile shaders at loading time instead of rendering time.
* HairWorks component now has right bounding box.
* Fixed some problems when a HairWorks component is edited.
* Fixed a bug that hair transform is sometimes wrong.
* Fixed a bug that view frustum culling doesn’t work.
* Fixed a crash when the hair normal bone index is -1.
* Fixed hair shadow bug on Pascal GPU.
* Some refactoring of C++ and shader codes.

Artist Workflow
=============
HairWorks assets are imported directly into the content browser. All the attributes of a HairWorks asset are now directly editable on the asset’s Details panel. The details panel is set up to be consistent with the HairWorks Viewer’s UI as close as possible.

To use a HairWorks asset on a character, it must be added as a BluePrint component.

* http://docs.nvidia.com/gameworks/content/artisttools/hairworks/HairWorks_Tutorials.html

Known Issues
=============
* Indirect Lighting may not light Hair correctly on occluded side of character.
* HairWorks visualizers do work in play mode.
* HairWorks shadow maps are generally crisper when compared to other objects in scene.
* Duplicated property categories in HairWorks assets and components. 
* Needs to read data back from GPU buffer to CPU for dynamic pin, so there would be potential performance issue. 