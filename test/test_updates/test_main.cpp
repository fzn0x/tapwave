#include "doctest_main.hpp"

#include <string>

#include "core/ota_manifest.hpp"
#include "core/semver.hpp"

using namespace tapwave;
using namespace tapwave::ota;

namespace {

const std::string kSha(64, 'a');

std::string manifest(std::string_view build) {
    return R"({"version": "0.2.0", "release_url": "https://github.com/fzn0x/tapwave/releases/tag/v0.2.0",
               "builds": {"esp32-4mb": )" +
           std::string(build) + "}}";
}

std::string image(std::string_view url, int size, std::string_view sha) {
    return R"({"url": ")" + std::string(url) + R"(", "size": )" + std::to_string(size) +
           R"(, "sha256": ")" + std::string(sha) + R"("})";
}

}  // namespace

TEST_CASE("versions parse with an optional v and build metadata") {
    const auto v = Version::parse("v1.2.3-rc.1+gabc");
    REQUIRE(v);
    CHECK(v->major == 1);
    CHECK(v->minor == 2);
    CHECK(v->patch == 3);
    CHECK(v->prerelease == std::vector<std::string>{"rc", "1"});
    CHECK(Version::parse("0.0.0-dev+gabc1234"));
}

TEST_CASE("malformed versions are rejected") {
    for (std::string_view text : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.2.x", "1.2.3-",
                                  "1.2.3-a..b", "1.2.3-a_b", "1.2.3-rc."}) {
        CHECK_FALSE_MESSAGE(Version::parse(text), text);
    }
}

TEST_CASE("versions sort by the SemVer rules") {
    auto v = [](std::string_view s) { return *Version::parse(s); };
    CHECK(v("1.0.0") > v("0.9.9"));
    CHECK(v("0.10.0") > v("0.9.0"));
    CHECK(v("1.0.0") > v("1.0.0-rc.1"));
    CHECK(v("1.0.0-rc.2") > v("1.0.0-rc.1"));
    CHECK(v("1.0.0-rc.10") > v("1.0.0-rc.9"));
    CHECK(v("1.0.0-beta") > v("1.0.0-alpha.1"));
    CHECK(v("1.0.0-alpha.1") > v("1.0.0-alpha"));
    CHECK(v("1.0.0-alpha.beta") > v("1.0.0-alpha.1"));
    CHECK(v("1.0.0+a") == v("1.0.0+b"));
}

TEST_CASE("is_newer for update checks") {
    CHECK(is_newer("0.2.0", "0.1.0"));
    CHECK_FALSE(is_newer("0.1.0", "0.1.0"));
    CHECK_FALSE(is_newer("0.1.0", "0.2.0"));
    CHECK(is_newer("0.1.1", "0.1.1-dev.5+gabc"));  // the release after a development build
    CHECK(is_newer("0.1.0", "0.0.0-dev"));
    CHECK(is_newer("0.1.0", "garbage"));
    CHECK_FALSE(is_newer("garbage", "0.1.0"));
}

TEST_CASE("the manifest gives this board's images") {
    const auto release = parse_manifest(
        manifest(R"({"firmware": )" + image("https://example.com/fw.bin", 1203456, kSha) +
                 R"(, "filesystem": )" + image("https://example.com/fs.bin", 131072, kSha) + "}"),
        "esp32-4mb");
    REQUIRE(release);
    CHECK(release->version == "0.2.0");
    CHECK(release->release_url == "https://github.com/fzn0x/tapwave/releases/tag/v0.2.0");
    CHECK(release->firmware.url == "https://example.com/fw.bin");
    CHECK(release->firmware.size == 1203456);
    REQUIRE(release->filesystem);
    CHECK(release->filesystem->size == 131072);
}

TEST_CASE("the filesystem image is optional and hashes are lowercased") {
    const auto release = parse_manifest(
        manifest(R"({"firmware": )" + image("http://lan/fw.bin", 10, std::string(64, 'B')) + "}"),
        "esp32-4mb");
    REQUIRE(release);
    CHECK_FALSE(release->filesystem);
    CHECK(release->firmware.sha256 == std::string(64, 'b'));
}

TEST_CASE("manifest errors") {
    CHECK(parse_manifest("not json", "esp32-4mb").error() == ManifestError::Json);
    CHECK(parse_manifest("[]", "esp32-4mb").error() == ManifestError::Json);
    CHECK(parse_manifest(R"({"builds": {}})", "esp32-4mb").error() ==
          ManifestError::MissingVersion);
    const std::string good = R"({"firmware": )" + image("https://x/fw.bin", 10, kSha) + "}";
    CHECK(parse_manifest(manifest(good), "esp32s3").error() == ManifestError::NoBuild);
    for (const std::string& bad :
         {image("ftp://x/fw.bin", 10, kSha), image("https://x/fw.bin", 0, kSha),
          image("https://x/fw.bin", 10, "abc"), image("https://x/fw.bin", 10, std::string(64, 'g')),
          std::string(R"({"url": "https://x"})")}) {
        CHECK_MESSAGE(
            parse_manifest(manifest(R"({"firmware": )" + bad + "}"), "esp32-4mb").error() ==
                ManifestError::BadImage,
            bad);
    }
    CHECK(parse_manifest(manifest(R"({"firmware": )" + image("https://x/a", 1, kSha) +
                                  R"(, "filesystem": {"url": "https://x/b"}})"),
                         "esp32-4mb")
              .error() == ManifestError::BadImage);
    for (auto e : {ManifestError::Json, ManifestError::MissingVersion, ManifestError::NoBuild,
                   ManifestError::BadImage}) {
        CHECK_FALSE(describe(e).empty());
    }
}
