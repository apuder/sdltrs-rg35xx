#include "TrsKeyboardAdapter.hpp"

#include <vector>

namespace rg35xx {

TrsKeyboardAdapter::TrsKeyboardAdapter(GuestKeySink& sink) : sink_(sink)
{
}

bool TrsKeyboardAdapter::validSymbol(int symbol)
{
    /* trs_xlate_keysym() indexes the local table with the low 16 bits. */
    return symbol >= 0 && symbol <= 0x142;
}

bool TrsKeyboardAdapter::press(int owner, int symbol)
{
    if (!validSymbol(symbol) || owner_keys_.count(owner) != 0)
        return false;

    const int identity = sink_.keyIdentity(symbol);
    if (identity < 0)
        return false;
    HeldKey held = {identity, symbol};
    owner_keys_[owner] = held;
    std::map<int, KeyCount>::iterator existing = key_counts_.find(identity);
    if (existing == key_counts_.end()) {
        KeyCount count = {1, symbol};
        key_counts_[identity] = count;
        sink_.keyDown(symbol);
    } else {
        ++existing->second.count;
    }
    return true;
}

bool TrsKeyboardAdapter::release(int owner)
{
    std::map<int, HeldKey>::iterator held = owner_keys_.find(owner);
    if (held == owner_keys_.end())
        return false;

    const int identity = held->second.identity;
    owner_keys_.erase(held);
    std::map<int, KeyCount>::iterator count = key_counts_.find(identity);
    if (count != key_counts_.end()) {
        if (--count->second.count == 0) {
            const int representative = count->second.representative_symbol;
            key_counts_.erase(count);
            sink_.keyUp(representative);
        }
    }
    return true;
}

void TrsKeyboardAdapter::releaseWhere(bool synthetic_only)
{
    std::vector<int> owners;
    for (std::map<int, HeldKey>::const_iterator it = owner_keys_.begin();
         it != owner_keys_.end(); ++it) {
        if (!synthetic_only || it->first < GuestOwnerPhysicalBase)
            owners.push_back(it->first);
    }
    for (std::size_t i = 0; i < owners.size(); ++i)
        release(owners[i]);
}

void TrsKeyboardAdapter::releaseSynthetic()
{
    releaseWhere(true);
}

void TrsKeyboardAdapter::releaseAll()
{
    releaseWhere(false);
}

bool TrsKeyboardAdapter::ownerActive(int owner) const
{
    return owner_keys_.count(owner) != 0;
}

int TrsKeyboardAdapter::ownerSymbol(int owner) const
{
    std::map<int, HeldKey>::const_iterator it = owner_keys_.find(owner);
    return it == owner_keys_.end() ? -1 : it->second.symbol;
}

} // namespace rg35xx
