#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "base/support/sha256.hpp"

TEST_CASE("SHA-256 produces standard lowercase hex digests") {
    CHECK(ahfl::support::sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb924"
                                           "27ae41e4649b934ca495991b7852b855");
    CHECK(ahfl::support::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223"
                                              "b00361a396177a9cb410ff61f20015ad");
}
