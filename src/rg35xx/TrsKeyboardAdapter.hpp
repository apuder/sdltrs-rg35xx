#ifndef TRS_KEYBOARD_ADAPTER_HPP
#define TRS_KEYBOARD_ADAPTER_HPP

#include <map>

namespace rg35xx {

enum GuestKeyOwner {
    GuestOwnerOverlayB = 1,
    GuestOwnerOverlayY = 2,
    GuestOwnerOverlayStart = 3,
    GuestOwnerPhysicalBase = 1000
};

class GuestKeySink {
public:
    virtual ~GuestKeySink() {}
    virtual int keyIdentity(int symbol) const { return symbol; }
    virtual void keyDown(int symbol) = 0;
    virtual void keyUp(int symbol) = 0;
};

class TrsKeyboardAdapter {
public:
    explicit TrsKeyboardAdapter(GuestKeySink& sink);

    bool press(int owner, int symbol);
    bool release(int owner);
    void releaseSynthetic();
    void releaseAll();
    bool ownerActive(int owner) const;
    int ownerSymbol(int owner) const;

    static bool validSymbol(int symbol);

private:
    struct HeldKey {
        int identity;
        int symbol;
    };
    struct KeyCount {
        unsigned int count;
        int representative_symbol;
    };

    void releaseWhere(bool synthetic_only);

    GuestKeySink& sink_;
    std::map<int, HeldKey> owner_keys_;
    std::map<int, KeyCount> key_counts_;
};

} // namespace rg35xx

#endif
