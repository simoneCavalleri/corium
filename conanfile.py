from conan import ConanFile
from conan.tools.files import copy
import os

class CoriumConan(ConanFile):
    name = "corium"
    version = "1.1.0"
    license = "MIT"
    author = "Simone Cavalleri <simone.cavalleri.94@gmail.com>"
    url = "https://github.com/simoneCavalleri/corium"
    description = "High-performance, zero-heap, header-only C++20 MPSC event framework"
    topics = ("event-driven", "mpsc", "zero-heap", "header-only", "embedded", "rtos", "cpp20")
    package_type = "header-library"
    no_copy_source = True

    def export_sources(self):
        copy(self, "include/*", src=self.recipe_folder, dst=self.export_sources_folder)
        copy(self, "LICENSE", src=self.recipe_folder, dst=self.export_sources_folder)

    def package(self):
        copy(self, "*.hpp", src=os.path.join(self.source_folder, "include"), dst=os.path.join(self.package_folder, "include"))
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.set_property("cmake_file_name", "corium")
        self.cpp_info.set_property("cmake_target_name", "corium::corium")
