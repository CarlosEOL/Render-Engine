# Endurance exterior

The exterior now uses a single static procedural mesh instead of a torus, twelve ellipsoids and four broad spokes. Twelve hard-sided, bevel-edged modules sit around an open ring. Their axial faces have dark recessed radiator panels, slats, perimeter rails, service hatches and small ochre markings. Narrow paired rails, diagonal braces and cylindrical engine housings connect the modules. A slender lattice docking spine, central collar and communications mast add depth through the open centre.

The visual reference is [Ely HADDAD's Endurance model on Sketchfab](https://sketchfab.com/3d-models/the-endurance-interstellar-space-exploration-f5573181da72474cb9b214bef153e3b3). Its publicly viewable preview informed the box-module silhouette, dark panels and connecting pods. This implementation is original geometry; the commercial model and its textures were not downloaded or imported. It is an interpretation, not a replica of that asset.

Coordinates are in metres. The existing 25-metre module-centre radius, spacecraft flight history, light-travel observation, ring rotation, selection radius and the two Ranger positions are preserved. Two intermodule gaps contain docking collars at the existing Ranger locations. The mesh is uploaded once with position and material colour, then submitted in one draw; metre-space face normals retain the existing spacecraft illumination without the old repeated sphere submissions.

Validation: the 12,084-triangle mesh contains no non-finite coordinates or degenerate triangles; its maximum radius is 30.8055 metres. Front and oblique rendered views passed in both Release renderers, alongside the existing close-range lensing/foreground checks.
