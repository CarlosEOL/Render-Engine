# Third-party notices

The full DX12 renderer builds its shader translator using official [glslang](https://github.com/KhronosGroup/glslang) and [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) sources under the sibling `External_Lib` directory. Their notices are preserved in `licenses/glslang.txt` and `licenses/SPIRV-Cross.txt`. Pinned revisions are recorded in `tools/BuildShaderTools.cmd` and the migration record.

Optional native DLSS integration uses [NVIDIA's official DLSS SDK](https://github.com/NVIDIA/DLSS), subject to `licenses/NVIDIA-DLSS.txt`. The SDK source, libraries and runtime DLL are downloaded to the dependency directory and are not committed here. The DX12 post-build step deploys the redistributable with its licence. This software contains source code provided by NVIDIA Corporation through the SDK helper headers used during compilation.

GLFW, GLAD, GLM, Assimp and other existing dependencies retain their original licences in the dependency distribution. This notice does not replace those licences. Application screenshots and native UI vector decoration in this update are generated from this project's own rendering.
