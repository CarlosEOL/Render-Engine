# Backend separation

This branch (`build/opengl`) remains the complete OpenGL application. The native DX12 project, shader translation backend and experimental DLSS support are published on [build/dx12](https://github.com/CarlosEOL/Render-Engine/tree/build/dx12).

See the [full migration record and reasons](https://github.com/CarlosEOL/Render-Engine/blob/build/dx12/docs/DX12_MIGRATION.md) for current native implementation and validation. The OpenGL project has no DX12/DLSS SDK dependency.

Shared improvements on this branch are documented in [UI polish](UI_POLISH_2026-11-12.md), [star generation and selection](STAR_GENERATION_AND_SELECTION_2026-11-12.md), [Kerr observer](KERR_OBSERVER_2026-11-12.md), and the [atmosphere/Moon fix](ATMOSPHERE_AND_MOON_2026-11-12.md). The Assimp C API import fix, node transforms and reserved shader-word rename are also shared; their rationale is recorded in the cross-branch migration record.
