import ctypes
from pathlib import Path
class Output(ctypes.Structure):
    _fields_=[('vertex',ctypes.c_void_p),('fragment',ctypes.c_void_p),('error',ctypes.c_void_p)]
dll=ctypes.CDLL(str(Path('../External_Lib/build/ShaderTranslator/Release/ShaderTranslator.dll').resolve()))
dll.TranslateSceneShaders.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.POINTER(Output)]
dll.FreeTranslatedProgram.argtypes=[ctypes.POINTER(Output)]
out=Output()
vs=b'#version 330 core\nlayout(location=0) in vec3 position; uniform mat4 matrix; out vec3 direction; void main(){direction=position;gl_Position=matrix*vec4(position,1);}'
ps=b'#version 330 core\nin vec3 direction; uniform vec3 tint; uniform sampler2D scene; out vec4 color; void main(){color=vec4(tint,1)*texture(scene,direction.xy);}'
try:
    result=dll.TranslateSceneShaders(vs,ps,ctypes.byref(out))
    if not result: raise RuntimeError(ctypes.string_at(out.error).decode())
    for name in ('vertex','fragment'):
        value=ctypes.string_at(getattr(out,name)).decode()
        Path('build/translated-'+name+'.hlsl').write_text(value)
        print(name, value[:1200])
finally:dll.FreeTranslatedProgram(ctypes.byref(out))
