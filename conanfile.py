#!/usr/bin/env python
# -*- coding: utf-8 -*-
from conans import ConanFile, CMake

class CPPCacheConan(ConanFile):
    name = "cpp_cache"
    version = "0.0.1-pre"

    description = "A header only set of Cache implementations."
    url = "http://github.com/szmyd/cpp_cache"
    license = "MIT"

    exports_sources = "*"

    generators = "cmake"

    build_requires = (('benchmark/[>=1.4,<2.0]@szmyd/stable'),
                      ('gunit/[>=1.0,<2]@szmyd/stable'))

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        cmake.test()

    def package(self):
        # Copy the license files
        self.copy("LICENSE", dst="licenses")
        # Copying headers
        self.copy(pattern="*.hpp", dst="include/", src="src/include", keep_path=True)
