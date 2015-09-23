#ifndef __CERBERUS_TEST_UTILIIES_HPP__
#define __CERBERUS_TEST_UTILIIES_HPP__

static std::string operator*(std::string const& s, int times)
{
    std::string r;
    while (times --> 0) {
        r += s;
    }
    return std::move(r);
}

#endif /* __CERBERUS_TEST_UTILIIES_HPP__ */
