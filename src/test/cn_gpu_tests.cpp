#include <boost/test/unit_test.hpp>

#include <crypto/pow_hash/cn_slow_hash.hpp>
#include <uint256.h>
#include <util/strencodings.h>

BOOST_AUTO_TEST_SUITE(cn_gpu_tests)

BOOST_AUTO_TEST_CASE(cn_gpu_hashtest)
{
    // Test CN/GPU hash with known inputs against expected outputs
    #define HASHCOUNT 5
    const char* inputhex[HASHCOUNT] = { "020000004c1271c211717198227392b029a64a7971931d351b387bb80db027f270411e398a07046f7d4a08dd815412a8712f874a7ebf0507e3878bd24e20a3b73fd750a667d2f451eac7471b00de6659", "0200000011503ee6a855e900c00cfdd98f5f55fffeaee9b6bf55bea9b852d9de2ce35828e204eef76acfd36949ae56d1fbe81c1ac9c0209e6331ad56414f9072506a77f8c6faf551eac7471b00389d01", "02000000a72c8a177f523946f42f22c3e86b8023221b4105e8007e59e81f6beb013e29aaf635295cb9ac966213fb56e046dc71df5b3f7f67ceaeab24038e743f883aff1aaafaf551eac7471b0166249b", "010000007824bc3a8a1b4628485eee3024abd8626721f7f870f8ad4d2f33a27155167f6a4009d1285049603888fe85a84b6c803a53305a8d497965a5e896e1a00568359589faf551eac7471b0065434e", "0200000050bfd4e4a307a8cb6ef4aef69abc5c0f2d579648bd80d7733e1ccc3fbc90ed664a7f74006cb11bde87785f229ecd366c2d4e44432832580e0608c579e4cb76f383f7f551eac7471b00c36982" };
    const char* expected[HASHCOUNT] = { "38cb9439785db86d8d20cc353506284cbdfc24eb030a1ed022d6f2362a2c34f7", "907e50ff4f25a8cfd89c1439d0667d620ea628a595fdd0335c6ee71d0169b297", "871279667f9d00214f69e3940f582aafdeb66ec87088c95090565164fee6313f", "cae561456d0b007607a40e74c60fcdffdd229ae2ada1226adfe920bff4304c16", "3c7ceb435584b1275d78eb1a3029c17c0334fc3f5c5f239e2b1b1bb9d119d683" };
    uint256 output;
    std::vector<unsigned char> inputbytes;
    for (int i = 0; i < HASHCOUNT; i++) {
        inputbytes = ParseHex(inputhex[i]);

        cn_pow_hash_v3 ctx;

        // Test hardware CN/GPU
        ctx.hardware_hash_3(inputbytes.data(), inputbytes.size(), BEGIN(output));
        BOOST_CHECK_EQUAL(output.ToString().c_str(), expected[i]);

        output.SetNull();

        // Test software CN/GPU
        ctx.software_hash_3(inputbytes.data(), inputbytes.size(), BEGIN(output));
        BOOST_CHECK_EQUAL(output.ToString().c_str(), expected[i]);
    }
}

BOOST_AUTO_TEST_SUITE_END()
