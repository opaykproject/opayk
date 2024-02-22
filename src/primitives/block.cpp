// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <crypto/common.h>
#include <crypto/progpow/progpow.hpp>
#include <crypto/randomx/rx.h>

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CBlockHeader::GetPoWHash(uint32_t nHeight, std::function<uint256(uint32_t)> getBlockHash) {
  uint256 thash;
  if (nHeight % 2 == 0) {
    CBlockHeader header;
    header.nVersion       = nVersion;
    header.hashPrevBlock  = hashPrevBlock;
    header.hashMerkleRoot = hashMerkleRoot;
    header.nTime          = nTime;
    header.nBits          = nBits;
    header.nNonce         = 0;
    uint256 hash = header.GetHash();
    auto context = ethash::create_epoch_context(ethash::get_epoch_number(nHeight));
    ethash::hash256 header_hash = {};
    memcpy(header_hash .bytes, hash.data(), 32);
    auto result = progpow::hash(*context, nHeight, header_hash, nNonce);
    for (int i=0; i<32; ++i)
      thash.data()[i] = result.final_hash.bytes[31-i];
  } else {
    uint256 hash = GetHash();
    uint256 seedhash = getBlockHash(rx_seedheight(nHeight));
    rx_slow_hash(BEGIN(seedhash), hash.data(), hash.size(), BEGIN(thash));
  }
  return thash;
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}

CTransactionRef CBlock::GetHogEx() const noexcept
{
    if (vtx.size() >= 2 && vtx.back()->IsHogEx()) {
        assert(!vtx.back()->vout.empty());
        return vtx.back();
    }

    return nullptr;
}
