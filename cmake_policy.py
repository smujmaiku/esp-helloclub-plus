# libpeer 0.0.3 declares cmake_minimum_required(VERSION 3.1), which
# CMake >= 4.0 rejects. Tell CMake to accept it under 3.5 policies.
# see: https://cmake.org/cmake/help/latest/variable/CMAKE_POLICY_VERSION_MINIMUM.html
import os

Import("env")

os.environ["CMAKE_POLICY_VERSION_MINIMUM"] = "3.5"
