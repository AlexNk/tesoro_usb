#include "lobera_usb.hpp"

#include <cstring>
#include <chrono>

#define VENDOR_ID        0x195d

#define BATCH_SIZE       4096
#define OFFSETS_SIZE     575
#define REPEAT_SIZE      228
#define THUMB_MAX_MACRO  1024

#define KEY_CODE_DISABLE 0x8c

#define W_FINILIZE       0x14
#define R_PROFILE        0x15
#define W_PROFILE        0x14
#define R_STATUS         0x04
#define W_LIGHT_MODE     0x31
#define R_COLORS         0x33
#define W_COLORS         0x32
#define R_THUMBS_MACROS  0x51
#define W_THUMBS_MACROS  0x50
#define R_THUMB_ENABLED  0x53
#define W_THUMB_ENABLED  0x52
#define R_KEYS_OFFSETS   0x11
#define W_KEYS_OFFSETS   0x10
#define R_KEYS_DATA      0x13
#define W_KEYS_DATA      0x12
#define R_KEYS_REPEATS   0x17
#define W_KEYS_REPEATS   0x16

namespace
{
    struct offset_entry
    {
        enum struct type: uint8_t
        {
            OF_NONE  = 0x00,
            OF_SUBST = 0x10,
            OF_MACRO = 0x20,
        };

        type     op = type::OF_NONE;
        uint16_t offset;
        uint16_t len;
    };

    struct repeat_entry
    {
        uint8_t                 key;
        lobera_usb::repeat_mode mode;
    };

    //
    // Repeats block
    //
    size_t decode_repeat_entry(uint8_t const * data, size_t data_size, size_t p, repeat_entry & entry)
    {
        if ((data_size - p) < 2)
            throw std::runtime_error("Broken repeats data");

        uint8_t key = data[p];
        if (key == 0)
            return 0;

        lobera_usb::repeat_mode mode = static_cast<lobera_usb::repeat_mode>(data[p + 1]);
        switch (mode)
        {
            case lobera_usb::repeat_mode::SINGLE:
            case lobera_usb::repeat_mode::PRESS:
            case lobera_usb::repeat_mode::NEXT:
                entry.key = data[p];
                entry.mode = mode;
                return 2;
        }
        throw std::runtime_error(std::string("Unknown macro repeat mode: ") + std::to_string(data[p + 1]));
    }

    size_t encode_repeat_entry(std::pair<uint8_t, lobera_usb::key_setting> const & entry,
                               uint8_t                                           * data,
                               size_t                                              data_size,
                               size_t                                              p)
    {
        using setting_type = lobera_usb::key_setting::type;

        switch (entry.second.get_type())
        {
            case setting_type::DISABLE:
            case setting_type::SUBST:
            case setting_type::MACRO:
                if (data != nullptr)
                {
                    if ((data_size - p) < 2)
                        return 0;
                    data[p++] = entry.first;
                    data[p++] = static_cast<uint8_t>(entry.second.get_repeat_mode());
                }
                return 2;
        }
        throw std::runtime_error("Unknown key setting type: " + std::to_string(static_cast<unsigned>(entry.second.get_type())));
    }

    std::vector<repeat_entry> decode_repeat_entries(uint8_t const * data, size_t data_size)
    {
        std::vector<repeat_entry> entries;
        for (size_t p = 0; p < data_size; )
        {
            repeat_entry entry;
            size_t sz = decode_repeat_entry(data, data_size, p, entry);
            if (sz == 0)
                break;
            p += sz;
            entries.push_back(entry);
        }
        return entries;
    }

    void encode_repeat_entries(lobera_usb::keys_settings const & entries, uint8_t * data, size_t data_size)
    {
        size_t p = 0;
        for (auto const & entry: entries)
            p += encode_repeat_entry(entry, data, data_size, p);
        for (; p < data_size - 1; )
        {
            data[p++] = 0x00;
            data[p++] = 0x01;
        }
    }

    //
    // Macro
    //
    size_t decode_macro_entry(uint8_t const * data, size_t data_size, size_t p, lobera_usb::macro_entry & entry)
    {
        using macro_type = lobera_usb::macro_entry::type;

        switch (data[p])
        {
            case 0x00:
                return 0;

            case 0x84:
                if ((data_size - p) < 3)
                    throw std::runtime_error("Broken macro data");
                entry = data[p + 2]
                    ? lobera_usb::macro_entry::key_dn(data[p + 1])
                    : lobera_usb::macro_entry::key_up(data[p + 1]);
                return 3;

            case 0x86:
                if ((data_size - p) < 3)
                    throw std::runtime_error("Broken macro data");
                entry = lobera_usb::macro_entry::repeat(data[p + 1] * 0x100 + data[p + 2]);
                return 3;

            case 0x87:
                if ((data_size - p) < 3)
                    throw std::runtime_error("Broken macro data");
                entry = lobera_usb::macro_entry::sleep(data[p + 1] * 0x100 + data[p + 2]);
                return 3;
        }
        throw std::runtime_error(std::string("Unknown macro code: ") + std::to_string(data[p]));
    }

    size_t encode_macro_entry(lobera_usb::macro_entry const & entry, uint8_t * data, size_t data_size, size_t p)
    {
        using macro_type = lobera_usb::macro_entry::type;

        switch (entry.get_type())
        {
            case macro_type::KEY_DN:
            case macro_type::KEY_UP:
                if (data != nullptr)
                {
                    if ((data_size - p) < 3)
                        throw std::runtime_error("Macro is too large");
                    data[p++] = 0x84;
                    data[p++] = entry.get_key_code();
                    data[p++] = (entry.get_type() == macro_type::KEY_DN) ? 1 : 0;
                }
                return 3;

            case macro_type::REPEAT:
                if (data != nullptr)
                {
                    if ((data_size - p) < 3)
                        throw std::runtime_error("Macro is too large");
                    data[p++] = 0x86;
                    data[p++] = entry.get_repeat() >> 8;
                    data[p++] = entry.get_repeat() & 0xff;
                }
                return 3;

            case macro_type::SLEEP:
                if (data != nullptr)
                {
                    if ((data_size - p) < 3)
                        throw std::runtime_error("Macro is too large");
                    data[p++] = 0x87;
                    data[p++] = entry.get_delay() >> 8;
                    data[p++] = entry.get_delay() & 0xff;
                }
                return 3;
        }
        throw std::runtime_error(std::string("Invalid macro operation: ") + std::to_string(static_cast<unsigned>(entry.get_type())));
    }

    std::vector<lobera_usb::macro_entry> decode_macro_entries(uint8_t const * data, size_t data_size)
    {
        std::vector<lobera_usb::macro_entry> ret;
        for (size_t p = 0; p < data_size; )
        {
            lobera_usb::macro_entry entry;
            size_t sz = decode_macro_entry(data, data_size, p, entry);
            if (sz == 0)
                break;
            p += sz;
            ret.push_back(entry);
        }
        return ret;
    }

    size_t encode_macro_entries(std::vector<lobera_usb::macro_entry> const & entries, uint8_t * data, size_t data_size)
    {
        size_t p = 0;
        for (auto const & entry: entries)
        {
            size_t sz = encode_macro_entry(entry, data, data_size, p);
            if (sz == 0)
                throw std::runtime_error(std::string("Macro is too large"));
            p += sz;
        }
        return p;
    }

    //
    // Keys
    //
    lobera_usb::key_setting decode_key_setting(uint8_t                 const * data,
                                               size_t                          data_size,
                                               offset_entry            const & offset,
                                               lobera_usb::repeat_mode         repeat)
    {
        if (offset.op == offset_entry::type::OF_SUBST)
        {
            uint8_t key = data[offset.offset];
            if (key >= KEY_CODE_DISABLE)
                return lobera_usb::key_setting(repeat);
            else
                return lobera_usb::key_setting(key, repeat);
        } else
        if (offset.op == offset_entry::type::OF_MACRO)
        {
            size_t size = offset.offset + offset.len;
            if (size > data_size)
                data_size = size;
            std::vector<lobera_usb::macro_entry> macro = decode_macro_entries(data + offset.offset, offset.len);
            return lobera_usb::key_setting(macro, repeat);
        }
        throw std::runtime_error("Unknown record type: " + std::to_string(static_cast<unsigned>(offset.op)));
    }

    size_t encode_key_setting(lobera_usb::key_setting const & setting, uint8_t * data, size_t data_size, size_t p)
    {
        using setting_type = lobera_usb::key_setting::type;

        switch (setting.get_type())
        {
            case setting_type::DISABLE:
                if (data != nullptr)
                    data[p] = KEY_CODE_DISABLE;
                return 1;

            case setting_type::SUBST:
                if (data != nullptr)
                    data[p] = setting.get_subst_key();
                return 1;

            case setting_type::MACRO:
                return encode_macro_entries(setting.get_macro(), data + p, data_size - p);
        }
        throw std::runtime_error("Unknown key setting type: " + std::to_string(static_cast<unsigned>(setting.get_type())));
    }

    lobera_usb::keys_settings decode_keys_settings(std::vector<offset_entry> const & offsets,
                                                   std::vector<repeat_entry> const & repeats,
                                                   uint8_t                   const * data,
                                                   size_t                            data_size)
    {
        lobera_usb::keys_settings ret;

        auto ioff = offsets.begin(), eoff = offsets.end();
        auto irep = repeats.begin(), erep = repeats.end();
        for (; (ioff != eoff) && (irep != erep); ++ioff, ++irep)
            ret.emplace(irep->key, decode_key_setting(data, data_size, *ioff, irep->mode));

        return ret;
    }

    size_t encode_keys_settings(lobera_usb::keys_settings const & settings, uint8_t * data, size_t data_size)
    {
        size_t p = 0;
        for (auto const & setting: settings)
            p += encode_key_setting(setting.second, data, data_size, p);
        return p;
    }

    size_t calc_num_batches(size_t data_size)
    {
        size_t ret = data_size / BATCH_SIZE + (((data_size % BATCH_SIZE) > 0) ? 1 : 0);
        return (ret > 0) ? ret : 1;
    }

    uint64_t now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch() ).count();
    }

    //
    // Offsets block
    //
    size_t decode_offset_entry(uint8_t const * data, size_t data_size, size_t p, offset_entry & entry)
    {
        using of_type = offset_entry::type;

        of_type op = static_cast<of_type>(data[p]);
        switch (op)
        {
            case of_type::OF_NONE:
                return 0;

            case of_type::OF_SUBST:
            case of_type::OF_MACRO:
                if ((data_size - p) < 5)
                    throw std::runtime_error("Broken offsets data");
                entry.op     = op;
                entry.offset = data[p + 1] * 0x100 + data[p + 2];
                entry.len    = data[p + 3] * 0x100 + data[p + 4];
                return 5;
        }
        throw std::runtime_error(std::string("Unknown offset entry code: ") + std::to_string(data[p]));
    }

    size_t encode_offset_entry(std::pair<uint8_t, lobera_usb::key_setting> const & entry,
                               size_t                                            & offset,
                               uint8_t                                           * data,
                               size_t                                              data_size,
                               size_t                                              p)
    {
        using setting_type = lobera_usb::key_setting::type;
        size_t sz = encode_key_setting(entry.second, nullptr, 0, 0);
        if (sz == 0)
            return 0;

        uint8_t entry_type;
        switch (entry.second.get_type())
        {
            case setting_type::DISABLE:
            case setting_type::SUBST:
                entry_type = 0x10;
                break;
            case setting_type::MACRO:
                entry_type = 0x20;
                break;
            default:
                throw std::runtime_error(std::string("Unknown offset entry code: ") + std::to_string(static_cast<unsigned>(entry.second.get_type())));
        };

        size_t prev_offset = offset;
        offset += sz;
        if (data != nullptr)
        {
            if ((data_size - p) < 5)
                return 0;
            data[p++] = entry_type;
            data[p++] = prev_offset >> 8;
            data[p++] = prev_offset & 0xff;
            data[p++] = sz >> 8;
            data[p++] = sz & 0xff;
        }
        return 5;
    }

    std::vector<offset_entry> decode_offset_entries(uint8_t const * data, size_t data_size)
    {
        std::vector<offset_entry> entries;
        for (size_t p = 5; p < data_size; )
        {
            offset_entry entry;
            size_t sz = decode_offset_entry(data, data_size, p, entry);
            if (sz == 0)
                break;
            p += sz;
            entries.push_back(entry);
        }
        return entries;
    }

    void encode_offset_entries(lobera_usb::keys_settings const & entries, uint8_t * data, size_t data_size)
    {
        data[0] = 0x72;
        data[1] = (data_size >> 8) & 0xff;
        data[2] = data_size & 0xff;

        size_t offset = 0, p = 5;
        for (auto const & entry: entries)
        {
            size_t sz = encode_offset_entry(entry, offset, data, data_size, p);
            if (sz == 0)
                break;
            p += sz;
        }

        data[3] = (offset >> 8) & 0xff;
        data[4] = offset & 0xff;
    }
}

lobera_usb::lobera_usb()
{   }

lobera_usb::~lobera_usb()
{
    close();
}

void lobera_usb::open()
{
    close();

    usb_init();

    usb_find_busses();
    usb_find_devices();

    for (struct usb_bus *bus = usb_get_busses(); bus; bus = bus->next)
    {
        for (struct usb_device *dev = bus->devices; dev; dev = dev->next)
        {
            if (dev->descriptor.idVendor == VENDOR_ID)
            {
                if ((dev->descriptor.idProduct == 0x2033) || (dev->descriptor.idProduct == 0x2034))
                {
                    h_ = usb_open(dev);
                    if (h_ == nullptr)
                        throw std::runtime_error(std::string("Error opening USB device: ") + usb_strerror());
                    return;
                }
            }
        }
    }

    throw std::runtime_error("USB device not found");
}

void lobera_usb::close()
{
    if (h_ != nullptr)
    {
        usb_close(h_);
        h_ = nullptr;
    }
}

uint8_t lobera_usb::get_profile()
{
    uint8_t mode[1] = {0};
    read_data(R_PROFILE, 0, 0, mode, sizeof(mode));
    return mode[0];
}

void lobera_usb::set_profile(uint8_t profile)
{
    if ((profile < 1) || (profile > 5))
        throw std::runtime_error("Invalid profile number");
    write_data(W_PROFILE, profile, 0, nullptr, 0, 500, 500);
}

uint8_t lobera_usb::get_brightness()
{
    uint8_t data[16] = {0};
    read_data(R_STATUS, 0, 0, data, sizeof(data));
    return data[1];
}

bool lobera_usb::get_full_nkpo()
{
    uint8_t data[16] = {0};
    read_data(R_STATUS, 0, 0, data, sizeof(data));
    return !!data[0];
}

lobera_usb::light_mode lobera_usb::get_light_mode()
{
    uint8_t data[16] = {0};
    read_data(R_STATUS, 0, 0, data, sizeof(data));
    return static_cast<light_mode>(data[4]);
}

void lobera_usb::set_light_mode(light_mode mode)
{
    write_data(W_LIGHT_MODE, static_cast<uint16_t>(mode), 0, nullptr, 0, 500, 500);
    write_data(W_FINILIZE, 0, 0);
}

uint32_t lobera_usb::get_profile_color(uint8_t profile)
{
    if (profile > 5)
        throw std::runtime_error("Invalid profile number");

    uint8_t data[18] = {0};
    read_data(R_COLORS, 0, 0, data, sizeof(data));
    return (data[profile * 3] << 16) | (data[profile * 3 + 1] << 8) | data[profile * 3 + 2];
}

void lobera_usb::set_profile_color(uint8_t profile, uint32_t rgb)
{
    if (profile > 5)
        throw std::runtime_error("Invalid profile number");

    char data[18] = {0};
    read_data(R_COLORS, 0, 0, data, sizeof(data));
    data[profile * 3]     = (rgb >> 16) & 0xFF;
    data[profile * 3 + 1] = (rgb >> 8) & 0xFF;
    data[profile * 3 + 2] = rgb & 0xFF;

    write_data(W_COLORS, 0, 0, data, sizeof(data), 500);
    write_data(W_FINILIZE, 0, 0);
}

lobera_usb::macro lobera_usb::get_thumb_macro(uint8_t profile, uint8_t thumb)
{
    if ((profile < 1) || (profile > 5))
        throw std::runtime_error("Invalid profile number");
    if ((thumb < 1) || (thumb > 3))
        throw std::runtime_error("Invalid thumb button number");

    char c = 0;
    read_data(R_THUMB_ENABLED, thumb, profile, &c, 1);
    if (c == 0)
        return {};

    uint8_t data[BATCH_SIZE] = {0};
    read_data(R_THUMBS_MACROS, 0, profile, data, sizeof(data));
    return decode_macro_entries(data + (thumb - 1) * THUMB_MAX_MACRO, THUMB_MAX_MACRO);
}

void lobera_usb::set_thumb_macro(uint8_t profile, uint8_t thumb, macro const & macro)
{
    if ((profile < 1) || (profile > 5))
        throw std::runtime_error("Invalid profile number");
    if ((thumb < 1) || (thumb > 3))
        throw std::runtime_error("Invalid thumb button number");

    // Check current thumb macros state
    uint8_t macro_set[3] = {0};
    for (size_t ithumb = 1; ithumb <= 3; ++ithumb)
    {
        if (ithumb == thumb)
            macro_set[ithumb - 1] = macro.empty() ? 0 : 1;
        else
            read_data(R_THUMB_ENABLED, ithumb, profile, macro_set + ithumb - 1, 1);
    }

    // Get current thumb macros
    uint8_t data[BATCH_SIZE] = {0};
    read_data(R_THUMBS_MACROS, 0, profile, data, sizeof(data));

    // Zero data
    size_t pos = 0;
    for (size_t ithumb = 1; ithumb <= 3; ++ithumb, pos += THUMB_MAX_MACRO)
    {
        if ((macro_set[ithumb - 1] == 0) || (ithumb == thumb))
            std::memset(data + pos, 0, THUMB_MAX_MACRO);
    }
    std::memset(data + pos, 0, sizeof(data) - pos);

    // Fill macro data
    encode_macro_entries(macro, data + (thumb - 1) * THUMB_MAX_MACRO, THUMB_MAX_MACRO);

    // Apply
    write_data(W_THUMBS_MACROS, 0, profile, data, sizeof(data), 1500, 1500);
    for (uint16_t ithumb = 1; ithumb <= 3; ++ithumb)
        write_data(W_THUMB_ENABLED, ithumb | (macro_set[ithumb - 1] ? 0x0100 : 0x0000), profile, nullptr, 0, 500, 500);
}

lobera_usb::keys_settings lobera_usb::get_profile_buttons(uint8_t profile)
{
    if ((profile < 1) || (profile > 5))
        throw std::runtime_error("Invalid profile number");

    // Load offsets
    uint8_t offset_table[OFFSETS_SIZE] = {0};
    size_t sz = read_data(R_KEYS_OFFSETS, 0, profile, offset_table, sizeof(offset_table));
    if (sz != sizeof(offset_table))
        throw std::runtime_error("Invalid data retrieved");
    if ((offset_table[0] != 0x72) && (offset_table[0] != 0x00)) // 114 keys?
        throw std::runtime_error("Invalid data retrieved");
    size_t recv_size = offset_table[1] * 0x100 + offset_table[2];
    if ((recv_size != sizeof(offset_table)) && (recv_size != 0))
        throw std::runtime_error("Invalid data retrieved");

    std::vector<offset_entry> offsets = decode_offset_entries(offset_table, sizeof(offset_table));
    size_t data_size = offset_table[3] * 0x100 + offset_table[4];
    size_t num_batches = calc_num_batches(data_size);

    // Load data batches
    std::vector<uint8_t> data(num_batches * BATCH_SIZE, 0);
    for (size_t batch_num = 0; batch_num < num_batches; ++batch_num)
    {
        uint16_t index = (batch_num << 8) | profile;
        sz = read_data(R_KEYS_DATA, 0, index, data.data() + batch_num * BATCH_SIZE, BATCH_SIZE);
        if (sz != BATCH_SIZE)
            throw std::runtime_error("Invalid data retrieved");
    }

    // Load repeat mode
    uint8_t repeat_buf[REPEAT_SIZE] = {0};
    sz = read_data(R_KEYS_REPEATS, 0, profile, repeat_buf, sizeof(repeat_buf));
    if (sz != sizeof(repeat_buf))
        throw std::runtime_error("Invalid data retrieved");
    std::vector<repeat_entry> repeats = decode_repeat_entries(repeat_buf, sizeof(repeat_buf));
    if (repeats.size() != offsets.size())
        throw std::runtime_error("Invalid data retrieved");

    return decode_keys_settings(offsets, repeats, data.data(), data.size());
}

void lobera_usb::set_profile_buttons(uint8_t profile, keys_settings const & settings)
{
    if ((profile < 1) || (profile > 5))
        throw std::runtime_error("Invalid profile number");

    size_t num_batches = calc_num_batches(encode_keys_settings(settings, nullptr, 0));
    std::vector<uint8_t> data(num_batches * BATCH_SIZE, 0);
    encode_keys_settings(settings, data.data(), data.size());

    uint8_t offset_table[OFFSETS_SIZE] = {0};
    encode_offset_entries(settings, offset_table, sizeof(offset_table));

    uint8_t repeat_buf[REPEAT_SIZE] = {0};
    encode_repeat_entries(settings, repeat_buf, sizeof(repeat_buf));

    write_data(W_KEYS_OFFSETS, 0, profile, offset_table, sizeof(offset_table), 500, 500);
    for (size_t batch_num = 0; batch_num < num_batches; ++batch_num)
    {
        uint16_t index = (batch_num << 8) | profile;
        write_data(W_KEYS_DATA, 0, index, data.data() + batch_num * BATCH_SIZE, BATCH_SIZE, 4000, 4000);
    }

    write_data(W_KEYS_REPEATS, 0, profile, repeat_buf, sizeof(repeat_buf), 1000, 1000);
    write_data(W_FINILIZE, 0, 0);
}

void lobera_usb::reset_config()
{
    set_light_mode(light_mode::SINGLE);

    uint8_t default_colors[] = {
        0xff, 0x00, 0xff,
        0x00, 0x00, 0xff,
        0xff, 0x00, 0x00,
        0xff, 0xff, 0xff,
        0x00, 0xff, 0x00,
        0xff, 0xff, 0x00
    };
    write_data(W_COLORS, 0, 0, default_colors, sizeof(default_colors), 500);
    write_data(W_FINILIZE, 0, 0, nullptr, 0);

    for (uint8_t iprofile = 1; iprofile <= 5; ++iprofile)
    {
        uint8_t data[BATCH_SIZE] = {0};
        write_data(W_THUMBS_MACROS, 0, iprofile, data, sizeof(data), 2000);
        for (uint16_t ithumb = 1; ithumb <= 3; ++ithumb)
            write_data(W_THUMB_ENABLED, ithumb, iprofile, nullptr, 0, 2000);
    }

    for (uint8_t iprofile = 1; iprofile <= 5; ++iprofile)
        set_profile_buttons(iprofile, keys_settings{});
};

size_t lobera_usb::read_data(uint8_t    req_type,
                             uint16_t   value,
                             uint16_t   index,
                             void     * data,
                             size_t     size,
                             uint64_t   next_write_ms,
                             uint64_t   next_read_ms)
{
    auto now = now_ms();
    if (now < next_read_)
    {
        usleep((next_read_ - now) * 1000ull);
        now = next_read_;
    }
    next_read_  = std::max(next_read_,  now + next_read_ms);
    next_write_ = std::max(next_write_, now + next_write_ms);

    int ret = usb_control_msg(h_, 0xc0, req_type, value, index, static_cast<char *>(data), size, 5000);
    if (ret < 0)
        throw std::runtime_error(std::string("Error reading data: ") + std::to_string(ret) + " (" + usb_strerror() + ")");
    return ret;
}

void lobera_usb::write_data(uint8_t          req_type,
                            uint16_t         value,
                            uint16_t         index,
                            void     const * data,
                            size_t           size,
                            uint64_t         next_write_ms,
                            uint64_t         next_read_ms)
{
    auto now = now_ms();
    if (now < next_write_)
    {
        usleep((next_write_ - now) * 1000ull);
        now = next_write_;
    }
    next_read_  = std::max(next_read_,  now + next_read_ms);
    next_write_ = std::max(next_write_, now + next_write_ms);

    auto ret = usb_control_msg(h_, 0x40, req_type, value, index, static_cast<char *>(const_cast<void *>(data)), size, 5000);
    if (ret < 0)
        throw std::runtime_error(std::string("Error writing data: ") + std::to_string(ret) + " (" + usb_strerror() + ")");
}
