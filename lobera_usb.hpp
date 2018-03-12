#include <usb.h>

#include <map>
#include <vector>

#include <iostream>

class lobera_usb
{
public:
    enum struct light_mode: uint8_t
    {
        OFF    = 0,
        SINGLE = 1,
        DIM    = 2,
        LOOP   = 3,
    };

    enum struct repeat_mode: uint8_t
    {
        SINGLE = 1,
        PRESS  = 2,
        NEXT   = 3,
    };

    class macro_entry
    {
    public:
        enum struct type: uint8_t
        {
            NONE,
            KEY_DN,
            KEY_UP,
            REPEAT,
            SLEEP,
        };

    private:
        macro_entry(type t, uint8_t key, uint16_t repeat, uint8_t delay)
            : type_(t)
            , key_code_(key)
            , repeat_(repeat)
            , delay_ms_(delay)
        {   }

    public:
        macro_entry()
            : type_(type::NONE)
            , key_code_(0)
            , repeat_(0)
            , delay_ms_(0)
        {   }

        static macro_entry key_dn(uint8_t key)
        {   return macro_entry(type::KEY_DN, key, 0, 0);   }

        static macro_entry key_up(uint8_t key)
        {   return macro_entry(type::KEY_UP, key, 0, 0);   }

        static macro_entry repeat(uint16_t repeat)
        {   return macro_entry(type::REPEAT, 0, repeat, 0);   }

        static macro_entry sleep(uint16_t delay_ms)
        {   return macro_entry(type::SLEEP, 0, 0, delay_ms);   }

        bool operator==(macro_entry const &r) const
        {
            return (type_     == r.type_    )
                && (key_code_ == r.key_code_)
                && (repeat_   == r.repeat_  )
                && (delay_ms_ == r.delay_ms_);
        }

        type get_type() const
        {   return type_;   }

        uint8_t get_key_code() const
        {
            if ((type_ != type::KEY_DN) && (type_ != type::KEY_UP))
                throw std::runtime_error("Invalid macro entry type");
            return key_code_;
        }

        uint16_t get_repeat() const
        {
            if (type_ != type::REPEAT)
                throw std::runtime_error("Invalid macro entry type");
            return repeat_;
        }

        uint16_t get_delay() const
        {
            if (type_ != type::SLEEP)
                throw std::runtime_error("Invalid macro entry type");
            return delay_ms_;
        }

    private:
        type     type_;
        uint8_t  key_code_;
        uint16_t delay_ms_;
        uint16_t repeat_;
    };
    typedef std::vector<macro_entry> macro;

    class key_setting
    {
    public:
        enum struct type
        {
            DISABLE,
            SUBST,
            MACRO,
        };

    public:
        key_setting(repeat_mode repeat = repeat_mode::SINGLE)
            : type_(type::DISABLE)
            , repeat_(repeat)
            , subst_(0)
        {   }

        key_setting(macro && m, repeat_mode repeat = repeat_mode::SINGLE)
            : type_(type::MACRO)
            , repeat_(repeat)
            , macro_(std::move(m))
            , subst_(0)
        {   }

        key_setting(macro const & m, repeat_mode repeat = repeat_mode::SINGLE)
            : type_(type::MACRO)
            , repeat_(repeat)
            , macro_(m)
            , subst_(0)
        {   }

        key_setting(uint8_t subst, repeat_mode repeat = repeat_mode::SINGLE)
            : type_(type::SUBST)
            , repeat_(repeat)
            , subst_(subst)
        {   }

        bool operator==(key_setting const & r) const
        {
            return (type_   == r.type_  )
                && (repeat_ == r.repeat_)
                && (macro_  == r.macro_ )
                && (subst_  == r.subst_ );
        }

        type get_type() const
        {   return type_;  }

        repeat_mode get_repeat_mode() const
        {   return repeat_; }

        uint8_t get_subst_key() const
        {
            if (type_ != type::SUBST)
                throw std::runtime_error("Invalid key setting type");
            return subst_;
        }

        macro const & get_macro() const
        {
            if (type_ != type::MACRO)
                throw std::runtime_error("Invalid key setting type");
            return macro_;
        }

    private:
        type        type_;
        repeat_mode repeat_;
        macro       macro_;
        uint8_t     subst_;
    };

    typedef std::map<uint8_t /*key*/, key_setting> keys_settings;

public:
    lobera_usb();
    virtual ~lobera_usb();

    void open();
    void close();

    uint8_t get_profile();
    void set_profile(uint8_t profile);

    uint8_t get_brightness();

    bool get_full_nkpo();

    light_mode get_light_mode();
    void set_light_mode(light_mode mode);

    uint32_t get_profile_color(uint8_t profile);
    void set_profile_color(uint8_t profile, uint32_t rgb);

    macro get_thumb_macro(uint8_t profile, uint8_t thumb);
    void set_thumb_macro(uint8_t profile, uint8_t thumb, macro const & macro);

    keys_settings get_profile_buttons(uint8_t profile);
    void set_profile_buttons(uint8_t profile, keys_settings const & settings);

    void reset_config();

private:
    size_t read_data(uint8_t    req_type,
                     uint16_t   value,
                     uint16_t   index,
                     void     * data,
                     size_t     size,
                     uint64_t   next_write_ms = 0,
                     uint64_t   next_read_ms = 0);
    void write_data(uint8_t          req_type,
                    uint16_t         value,
                    uint16_t         index,
                    void     const * data = nullptr,
                    size_t           size = 0,
                    uint64_t         next_write_ms = 0,
                    uint64_t         next_read_ms = 0);

private:
    usb_dev_handle * h_          = nullptr;
    uint64_t         next_read_  = 0;
    uint64_t         next_write_ = 0;
};
