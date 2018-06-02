from conan.packager import ConanMultiPackager


if __name__ == "__main__":
    builder = ConanMultiPackager(remotes=['https://api.bintray.com/conan/szmyd/conan-repo'])
    builder.add_common_builds()
    builder.run()
ConanMultiPackager