# LLVM-Learning

To compile 
```bash
clang++ -g -O3 main.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core` -o main
```
To run 
```
./main
```
