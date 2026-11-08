// WebAssembly entry point of the track compiler (docs/roadmap-track-editor.md, parity rule 2).
//
// The browser and Node cannot load ObjFac1.so, so the object factory is compiled into this program and registered before the compiler
// (NetTarget/MazeCompiler/main.cpp, built with its `main` renamed HoverNetMazeCompilerMain) starts. The compiler code itself is the same code
// the native HoverNetMazeCompiler and CI run: nothing is re-implemented. Like the native program it ends with exit(), so the editor starts
// a fresh instance of this module for every compile.
// The build renames main in every file of this program (main=HoverNetMazeCompilerMain); this file needs the real one.
#undef main

void HoverNetInitializeWebFactory();
int HoverNetMazeCompilerMain(int argc, const char** argv);

int main(int argc, const char** argv)
{
    HoverNetInitializeWebFactory();
    return HoverNetMazeCompilerMain(argc, argv);
}
