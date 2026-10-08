Vallado SGP4 C++ sources from https://github.com/brandon-rhodes/python-sgp4
Commit: 8126f773923c2902719d45aab8bdb5cce86cf39d
SGP4.h is unchanged. SGP4.cpp has two mechanical compiler-warning fixes: array pointers in the unused text parser decay directly to char pointers, and explicit braces preserve the existing nested conditional in dpper. No propagation equations or constants were changed.
The GodEye wrapper exposes validated numeric elements only; no Python runtime is used.
MIT license is packaged as GodEyeCore/Resources/SGP4-LICENSE.txt.
