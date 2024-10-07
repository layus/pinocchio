# tested with 
# $ clang-tidy --version
# LLVM (http://llvm.org/):
#   LLVM version 17.0.6
#   Optimized build.
clang-tidy include/pinocchio/multibody/data.hpp --checks=performance-noexcept-move-constructor -- -isystem stdc++11/usr/include/c++/11 -isystem stdc++11/usr/include/x86_64-linux-gnu/c++/11  x86_64-linux-gnu/c++/11 -I include -std=c++20
