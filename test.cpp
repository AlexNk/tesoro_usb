#include <iostream>
#include <functional>
#include "lobera_usb.hpp"

#define TEST_FN(X) {#X, X}
#define TEST_CHECK(X) { if !(X) throw std::runtime_error("Fail at line " + std::to_string(__LINE__) + ": " #X " is not true"); }
#define TEST_CHECK_EQUAL(X1, X2) { if (!((X1) == (X2))) throw std::runtime_error("Fail at line " + std::to_string(__LINE__) + ": " #X1 " != " #X2); }

void run_test(std::pair<std::string, std::function<void()>> const & test)
{
    try
    {
        std::cout << "Running test: " << test.first << std::endl;
        test.second();
    }
    catch (std::exception const & e)
    {
        std::cerr << "\tError in " << test.first << ": " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "\tUnknown error in " << test.first << std::endl;
    }
}

void test_set_profile()
{
    lobera_usb l;
    l.open();

    auto profile_original = l.get_profile();

    l.set_profile(1);
    TEST_CHECK_EQUAL(l.get_profile(), 1);

    l.set_profile(2);
    TEST_CHECK_EQUAL(l.get_profile(), 2);

    l.set_profile(3);
    TEST_CHECK_EQUAL(l.get_profile(), 3);

    l.set_profile(4);
    TEST_CHECK_EQUAL(l.get_profile(), 4);

    l.set_profile(5);
    TEST_CHECK_EQUAL(l.get_profile(), 5);

    // Restore
    l.set_profile(profile_original);
}

void test_set_color()
{
    lobera_usb l;
    l.open();

    auto c0 = l.get_profile_color(0);
    auto c1 = l.get_profile_color(1);
    auto c2 = l.get_profile_color(2);
    auto c3 = l.get_profile_color(3);
    auto c4 = l.get_profile_color(4);
    auto c5 = l.get_profile_color(5);

    l.set_profile_color(0, 0xFF0000);
    l.set_profile_color(3, 0xFF00FF);

    TEST_CHECK_EQUAL(l.get_profile_color(0), 0xFF0000);
    TEST_CHECK_EQUAL(l.get_profile_color(1), c1);
    TEST_CHECK_EQUAL(l.get_profile_color(2), c2);
    TEST_CHECK_EQUAL(l.get_profile_color(3), 0xFF00FF);
    TEST_CHECK_EQUAL(l.get_profile_color(4), c4);
    TEST_CHECK_EQUAL(l.get_profile_color(5), c5);

    l.set_profile_color(0, 0x00FF00);
    l.set_profile_color(3, 0x0000FF);

    TEST_CHECK_EQUAL(l.get_profile_color(0), 0x00FF00);
    TEST_CHECK_EQUAL(l.get_profile_color(1), c1);
    TEST_CHECK_EQUAL(l.get_profile_color(2), c2);
    TEST_CHECK_EQUAL(l.get_profile_color(3), 0x0000FF);
    TEST_CHECK_EQUAL(l.get_profile_color(4), c4);
    TEST_CHECK_EQUAL(l.get_profile_color(5), c5);

    // Restore
    l.set_profile_color(0, c0);
    l.set_profile_color(3, c3);
}

void test_set_light_mode()
{
    lobera_usb l;
    l.open();

    auto original_mode = l.get_light_mode();

    l.set_light_mode(lobera_usb::light_mode::OFF);
    TEST_CHECK_EQUAL(l.get_light_mode(), lobera_usb::light_mode::OFF);

    l.set_light_mode(lobera_usb::light_mode::SINGLE);
    TEST_CHECK_EQUAL(l.get_light_mode(), lobera_usb::light_mode::SINGLE);

    l.set_light_mode(lobera_usb::light_mode::DIM);
    TEST_CHECK_EQUAL(l.get_light_mode(), lobera_usb::light_mode::DIM);

    l.set_light_mode(lobera_usb::light_mode::LOOP);
    TEST_CHECK_EQUAL(l.get_light_mode(), lobera_usb::light_mode::LOOP);

    // Restore
    l.set_light_mode(original_mode);
}


void test_set_macro()
{
    lobera_usb l;
    l.open();
    auto m1 = l.get_thumb_macro(1, 1);
    auto m2 = l.get_thumb_macro(1, 2);
    auto m3 = l.get_thumb_macro(1, 3);

    l.set_thumb_macro(1, 2, lobera_usb::macro{});
    TEST_CHECK_EQUAL(l.get_thumb_macro(1, 2), lobera_usb::macro{});

    lobera_usb::macro m = {
        lobera_usb::macro_entry::key_dn(0x1e), // push 1
        lobera_usb::macro_entry::sleep(50),    // sleep 50ms
        lobera_usb::macro_entry::key_up(0x1e), // release 1
        lobera_usb::macro_entry::sleep(50),
        lobera_usb::macro_entry::key_dn(0x1f), // push 2
        lobera_usb::macro_entry::sleep(50),
        lobera_usb::macro_entry::key_up(0x1f),
        lobera_usb::macro_entry::sleep(50),
        lobera_usb::macro_entry::key_dn(0x20), // push 3
        lobera_usb::macro_entry::sleep(50),
        lobera_usb::macro_entry::key_up(0x20),
        lobera_usb::macro_entry::repeat(3),    // repeat 3 times
    };
    l.set_thumb_macro(1, 2, m);

    TEST_CHECK_EQUAL(l.get_thumb_macro(1, 1), m1);
    TEST_CHECK_EQUAL(l.get_thumb_macro(1, 2), m);
    TEST_CHECK_EQUAL(l.get_thumb_macro(1, 3), m3);

    // restore
    l.set_thumb_macro(1, 2, m2);
}

void test_set_keys()
{
    lobera_usb l;
    l.open();

    lobera_usb::keys_settings original_settings;// = l.get_profile_buttons(4);

    l.set_profile_buttons(4, lobera_usb::keys_settings{});
    TEST_CHECK_EQUAL(l.get_profile_buttons(4), lobera_usb::keys_settings{});

    lobera_usb::keys_settings settings;
    lobera_usb::macro const m = {
        lobera_usb::macro_entry::key_dn(0x04), // push A
        lobera_usb::macro_entry::sleep(50),    // sleep 50ms
        lobera_usb::macro_entry::key_up(0x04), // release A
        lobera_usb::macro_entry::sleep(50),    // sleep 50ms
        lobera_usb::macro_entry::repeat(3),    // repeat 3 times
    };
    settings.emplace(0x1e, lobera_usb::key_setting(m,    lobera_usb::repeat_mode::SINGLE)); // key 1 - macro
    settings.emplace(0x1f, lobera_usb::key_setting(0x16, lobera_usb::repeat_mode::PRESS));  // key 2 - replace with S
    settings.emplace(0x20, lobera_usb::key_setting());                                      // key 3 - disable

    l.set_profile_buttons(4, settings);
    TEST_CHECK_EQUAL(l.get_profile_buttons(4), settings);

    // restore
    l.set_profile_buttons(4, original_settings);
}

void test_reset_config()
{
    lobera_usb l;
    l.open();
    l.reset_config();
}

int main(int argc, char const *argv[])
{
    std::vector<std::pair<std::string, std::function<void()>>> tests = {
        TEST_FN(test_set_color),
        TEST_FN(test_set_profile),
        TEST_FN(test_set_light_mode),
        TEST_FN(test_set_macro),
        TEST_FN(test_set_keys),
        //TEST_FN(test_reset_config),
    };

    for (auto const & test: tests)
        run_test(test);

    return 0;
}
